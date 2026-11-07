// C ABI of the report-only what-if valuation and the combo dry run (design D4 of
// add-academy-and-strategies): JSON in, JSON out, no run state changed.

#include <string>
#include <string_view>

#include "c_api_run.hpp"
#include "json_in.hpp"
#include "json_out.hpp"
#include "tradelab/whatif.hpp"

using tradelab::detail::JsonOut;
using tradelab::detail::JsonValue;

namespace tradelab {
namespace {

using Kind = JsonValue::Kind;

// Optional decimal field: absent or null leaves `out` unchanged; false when malformed.
template <typename F>
bool read_dec(const JsonValue& o, std::string_view key, F* out, bool* present = nullptr) {
  const JsonValue* v = o.find(key);
  if (v == nullptr || v->is(Kind::Null)) return true;
  if (!v->is(Kind::String)) return false;
  const auto r = parse_decimal<F>(v->text);
  if (!r.ok()) return false;
  *out = r.value();
  if (present != nullptr) *present = true;
  return true;
}

bool read_uint(const JsonValue& o, std::string_view key, std::uint32_t* out) {
  const JsonValue* v = o.find(key);
  if (v == nullptr) return true;
  if (!v->is(Kind::Int) || v->integer < 0 || v->integer > 0xFFFFFFFFLL) return false;
  *out = static_cast<std::uint32_t>(v->integer);
  return true;
}

std::string text_of(const JsonValue& o, std::string_view key) {
  const JsonValue* v = o.find(key);
  return v != nullptr && v->is(Kind::String) ? v->text : std::string();
}

bool read_side(const JsonValue& o, OrderSide* side) {
  const std::string s = text_of(o, "side");
  *side = s == "sell" ? OrderSide::Sell : OrderSide::Buy;
  return s == "buy" || s == "sell";
}

// "call", "put" or "perp" (a run leg without "kind" is an option named by "series").
bool read_kind(const JsonValue& o, WhatIfLeg& l, bool run) {
  const std::string k = text_of(o, "kind");
  l.perp = k == "perp";
  l.kind = k == "put" ? OptionKind::Put : OptionKind::Call;
  if (run) return k.empty() || k == "perp";
  return k == "call" || k == "put" || k == "perp";
}

bool read_leg(const JsonValue& o, WhatIfLeg& l, bool run) {
  if (!o.is(Kind::Object) || !read_kind(o, l, run) || !read_side(o, &l.side)) return false;
  bool ok = read_dec(o, "qty", &l.qty) && read_dec(o, "entry", &l.entry, &l.has_entry) && read_dec(o, "stop", &l.stop);
  if (run) return ok && (l.perp || (read_uint(o, "series", &l.series) && l.series != 0));
  ok = ok && read_dec(o, "strike", &l.strike) && read_dec(o, "iv", &l.iv) && read_uint(o, "minutesToExpiry", &l.minutes_to_expiry);
  return ok && (l.has_entry || run);
}

bool read_legs(const JsonValue& root, std::vector<WhatIfLeg>& out, bool run) {
  const JsonValue* legs = root.find("legs");
  if (legs == nullptr || !legs->is(Kind::Array)) return false;
  for (const JsonValue& o : legs->items) {
    WhatIfLeg l;
    if (!read_leg(o, l, run)) return false;
    out.push_back(l);
  }
  return true;
}

bool read_shift_grid(const JsonValue& root, WhatIfRequest& r) {
  if (const JsonValue* s = root.find("shift"); s != nullptr) {
    if (!s->is(Kind::Object)) return false;
    const bool ok = read_dec(*s, "forward", &r.shift.forward) && read_dec(*s, "ivPoints", &r.shift.iv_points) &&
                    read_uint(*s, "minutesElapsed", &r.shift.minutes_elapsed) && read_uint(*s, "bars", &r.shift.bars);
    if (!ok) return false;
  }
  if (const JsonValue* g = root.find("grid"); g != nullptr) {
    if (!g->is(Kind::Object)) return false;
    return read_dec(*g, "lo", &r.grid.lo) && read_dec(*g, "hi", &r.grid.hi) && read_uint(*g, "points", &r.grid.points);
  }
  return true;
}

bool parse_request(const char* json, WhatIfRequest& r, bool run) {
  if (json == nullptr) return false;
  const detail::JsonParse p = detail::parse_json(json);
  if (!p.ok || !p.value.is(Kind::Object)) return false;
  const JsonValue& root = p.value;
  if (!read_legs(root, r.legs, run) || !read_shift_grid(root, r)) return false;
  if (run) return true;
  return read_dec(root, "forward", &r.forward) && read_dec(root, "multiplier", &r.multiplier) &&
         read_uint(root, "minutesPerStep", &r.minutes_per_step);
}

void dec_or_null(JsonOut& j, std::string_view key, const std::optional<Money>& v) {
  j.key(key);
  if (v) j.dec(*v); else j.null();
}

void write_curve(JsonOut& j, std::string_view key, const std::vector<CurvePoint>& curve) {
  j.key(key).begin_arr();
  for (const CurvePoint& c : curve) j.begin_arr().dec(c.price).dec(c.pnl).end_arr();
  j.end_arr();
}

void write_leg(JsonOut& j, const WhatIfLegResult& r) {
  const WhatIfLeg& l = r.leg;
  j.begin_obj().field("kind", l.perp ? "perp" : json_name(l.kind)).field("side", json_name(l.side)).field("qty", l.qty);
  j.field("series", l.series).field("expiryStep", r.expiry_step).key("strike");
  if (l.perp) j.null(); else j.dec(r.strike);
  j.field("entry", l.entry).field("value", r.value).key("iv");
  if (l.perp) j.null(); else j.dec(r.iv);
  j.field("ivFloored", r.iv_floored).field("delta", r.delta).field("gamma", r.gamma).field("theta", r.theta);
  j.field("vega", r.vega).field("positionValue", r.position_value).field("pnl", r.pnl).end_obj();
}

void write_expiry(JsonOut& j, const std::optional<ExpiryAnalytics>& e) {
  j.key("expiry");
  if (!e) {
    j.null();
    return;
  }
  j.begin_obj().key("breakevens").begin_arr();
  for (const Price b : e->breakevens) j.dec(b);
  j.end_arr();
  dec_or_null(j, "maxProfit", e->max_profit);
  dec_or_null(j, "maxLoss", e->max_loss);
  write_curve(j, "curve", e->curve);
  j.end_obj();
}

const char* error_text(WhatIfError e) {
  return e == WhatIfError::SeriesNotListed ? "{\"error\":\"series_not_listed\"}" : "{\"error\":\"invalid_input\"}";
}

std::string whatif_json(const WhatIfResult& r) {
  if (r.error != WhatIfError::None) return error_text(r.error);
  JsonOut j;
  j.begin_obj().field("forward", r.forward).field("atExpiry", r.at_expiry).key("legs").begin_arr();
  for (const WhatIfLegResult& l : r.legs) write_leg(j, l);
  j.end_arr().key("net").begin_obj().field("value", r.net.value).field("pnl", r.net.pnl).field("delta", r.net.delta);
  j.field("gamma", r.net.gamma).field("theta", r.net.theta).field("vega", r.net.vega).end_obj();
  write_expiry(j, r.expiry);
  write_curve(j, "today", r.today);
  j.end_obj();
  return j.text();
}

bool parse_combo(const char* json, std::vector<ComboLeg>& out) {
  WhatIfRequest r;
  if (!parse_request(json, r, true) || r.legs.empty() || r.legs.size() > 8) return false;
  for (const WhatIfLeg& l : r.legs) out.push_back({l.perp, l.series, l.side, l.qty});
  return true;
}

std::string combo_json(const ComboPreview& p) {
  JsonOut j;
  j.begin_obj().field("accepted", p.accepted).key("legs").begin_arr();
  for (const ComboLegResult& r : p.legs) {
    j.begin_obj().field("accepted", r.accepted).field("reason", json_name(r.reason)).field("quote", r.quote);
    j.field("premium", r.premium).field("fee", r.fee).field("required", r.required);
    j.field("availableAfter", r.available_after).end_obj();
  }
  j.end_arr().field("netPremium", p.net_premium).field("fees", p.fees).field("required", p.required);
  j.field("availableAfter", p.available_after).field("optionMarginAfterFill", p.option_margin_after);
  j.field("positionMarginAfterFill", p.position_margin_after).field("availableAfterFill", p.available_after_fill);
  j.end_obj();
  return j.text();
}

}  // namespace
}  // namespace tradelab

using namespace tradelab;

extern "C" {

const char* tl_option_whatif(const char* request_json) {
  static std::string buffer;
  WhatIfRequest req;
  buffer = parse_request(request_json, req, false) ? whatif_json(option_whatif(req)) : error_text(WhatIfError::InvalidInput);
  return buffer.c_str();
}

const char* tl_run_option_whatif(tl_run* run, const char* request_json) {
  WhatIfRequest req;
  run->buffer = parse_request(request_json, req, true) ? whatif_json(run_option_whatif(run->scenario, run->state, req))
                                                       : error_text(WhatIfError::InvalidInput);
  return run->buffer.c_str();
}

const char* tl_combo_preview(tl_run* run, const char* legs_json) {
  std::vector<ComboLeg> legs;
  run->buffer = parse_combo(legs_json, legs) ? combo_json(combo_preview(run->scenario, run->state, legs))
                                             : error_text(WhatIfError::InvalidInput);
  return run->buffer.c_str();
}

}  // extern "C"
