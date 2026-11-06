#include "viewmodel/game/whatif_lab.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "model/storage/json_writer.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr double kMinutesPerDay = 1440;

void grid_fields(storage::JsonWriter& w, double forward) {
  w.key("grid").begin_object();
  w.field("lo", to_fixed(forward * 0.7, 2)).field("hi", to_fixed(forward * 1.3, 2)).field("points", kGridPoints);
  w.end_object();
}

void academy_leg(storage::JsonWriter& w, const AcademyLeg& l) {
  w.begin_object().field("kind", leg_kind_code(l.kind)).field("side", l.buy ? "buy" : "sell").field("qty", js_number(l.qty));
  if (l.kind != LegKind::Perp) {
    w.field("strike", to_fixed(l.strike, 2));
    w.field("minutesToExpiry", static_cast<std::int64_t>(std::llround(l.days * kMinutesPerDay)));
    w.field("iv", to_fixed(l.iv_pct / 100, 4));
  }
  w.field("entry", to_fixed(l.entry, 2)).end_object();
}

int clamp_int(int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); }

std::string text_of(const json::Value& v) { return v.as_string(); }

std::optional<std::string> optional_text(const json::Value& v) {
  return v.is_null() ? std::nullopt : std::optional<std::string>(v.as_string());
}

Curve curve_from(const json::Value& v) {
  Curve out;
  for (const auto& p : v.items()) out.emplace_back(p[0].as_string(), p[1].as_string());
  return out;
}

WhatIfLeg leg_from(const json::Value& l) {
  WhatIfLeg out;
  out.kind = text_of(l["kind"]);
  out.buy = l["side"].as_string() != "sell";
  out.qty = text_of(l["qty"]);
  out.series = static_cast<int>(l["series"].as_int());
  out.expiry_step = static_cast<int>(l["expiryStep"].as_int());
  out.strike = optional_text(l["strike"]);
  out.entry = text_of(l["entry"]);
  out.value = text_of(l["value"]);
  out.iv = optional_text(l["iv"]);
  out.iv_floored = l["ivFloored"].as_bool(false);
  out.delta = text_of(l["delta"]);
  out.gamma = text_of(l["gamma"]);
  out.theta = text_of(l["theta"]);
  out.vega = text_of(l["vega"]);
  out.position_value = text_of(l["positionValue"]);
  out.pnl = text_of(l["pnl"]);
  return out;
}

WhatIfExpiry expiry_from(const json::Value& e) {
  WhatIfExpiry out;
  for (const auto& b : e["breakevens"].items()) out.breakevens.push_back(b.as_string());
  out.max_profit = optional_text(e["maxProfit"]);
  out.max_loss = optional_text(e["maxLoss"]);
  out.curve = curve_from(e["curve"]);
  return out;
}

template <typename T>
Analysis<T> failure(const std::optional<json::Value>& doc) {
  Analysis<T> out;
  out.error = doc ? (*doc)["error"].as_string("invalid_output") : "invalid_output";
  return out;
}

std::vector<ChartPoint> points(const Curve& curve, double lo, double hi, double w, double h, double max_abs) {
  std::vector<ChartPoint> out;
  out.reserve(curve.size());
  for (const auto& [p, v] : curve) {
    out.push_back({(fmt::num(p) - lo) / (hi - lo) * w, h / 2 - fmt::num(v) / max_abs * (h / 2 - 6)});
  }
  return out;
}

}  // namespace

std::string_view slider_code(Slider s) {
  switch (s) {
    case Slider::Price: return "price";
    case Slider::Time: return "time";
    case Slider::Iv: break;
  }
  return "iv";
}

// Number#toFixed rounds the exact binary value half away from zero; "%.*f" rounds it to nearest, ties
// to even. They differ only on exact binary ties (x.125 to 2 places), which the lab's inputs (cents,
// percent of a cent price) reach only through such literals; the parity fixtures cover the cases used.
std::string to_fixed(double v, int digits) {
  if (!std::isfinite(v)) return std::isnan(v) ? "NaN" : (v > 0 ? "Infinity" : "-Infinity");
  const double scale = std::pow(10.0, digits);
  const double scaled = v * scale;
  double rounded = std::nearbyint(scaled);
  if (std::fabs(scaled - std::trunc(scaled)) == 0.5) rounded = scaled > 0 ? std::ceil(scaled) : std::floor(scaled);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", digits, rounded / scale);
  std::string out = buf;
  if (out.front() == '-' && std::all_of(out.begin() + 1, out.end(), [](char c) { return c == '0' || c == '.'; })) {
    out.erase(0, 1);  // (-0.001).toFixed(2) is "-0.00" in JS, but the lab never sends negatives
  }
  return out;
}

// The shortest text that reads back as the same double (JavaScript's String(number) for the ranges used).
std::string js_number(double v) {
  if (!std::isfinite(v)) return std::isnan(v) ? "NaN" : (v > 0 ? "Infinity" : "-Infinity");
  if (v == 0) return "0";
  char buf[64];
  for (int p = 1; p <= 17; ++p) {
    std::snprintf(buf, sizeof buf, "%.*g", p, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  return buf;
}

std::string shifted_forward(double forward, int pct) { return to_fixed(forward * (1 + pct / 100.0), 2); }

std::string academy_request(const std::vector<AcademyLeg>& legs, const LabShift& shift, double forward) {
  storage::JsonWriter w;
  w.begin_object().field("forward", to_fixed(forward, 2)).field("minutesPerStep", static_cast<int>(kMinutesPerDay));
  w.field("multiplier", "1").key("legs").begin_array();
  for (const auto& l : legs) academy_leg(w, l);
  w.end_array().key("shift").begin_object();
  w.field("forward", shifted_forward(forward, shift.price));
  w.field("minutesElapsed", static_cast<std::int64_t>(std::llround(shift.time * kMinutesPerDay)));
  w.field("ivPoints", std::to_string(shift.iv)).end_object();
  grid_fields(w, forward);
  return w.end_object().text();
}

std::string run_request(const std::vector<AnalysisLeg>& legs, const LabShift& shift, double forward) {
  storage::JsonWriter w;
  w.begin_object().key("legs").raw(legs_json(legs)).key("shift").begin_object();
  w.field("forward", shifted_forward(forward, shift.price)).field("bars", shift.time);
  w.field("ivPoints", std::to_string(shift.iv)).end_object();
  grid_fields(w, forward);
  return w.end_object().text();
}

std::string run_request(const std::vector<AnalysisLeg>& legs) {
  storage::JsonWriter w;
  return w.begin_object().key("legs").raw(legs_json(legs)).end_object().text();
}

std::string combo_request(const std::vector<AnalysisLeg>& legs) { return run_request(legs); }

double academy_max_days(const std::vector<AcademyLeg>& legs) {
  std::optional<double> lowest;
  for (const auto& l : legs) {
    if (l.kind != LegKind::Perp) lowest = std::min(lowest.value_or(l.days), l.days);
  }
  return lowest ? std::max(0.0, *lowest) : 0.0;
}

int run_max_bars(const std::vector<int>& expiry_steps, int step) {
  if (expiry_steps.empty()) return 0;
  return std::max(0, *std::min_element(expiry_steps.begin(), expiry_steps.end()) - step);
}

LabShift clamp_shift(const LabShift& s, int max_time) {
  return {clamp_int(s.price, -kPriceRange, kPriceRange), clamp_int(s.time, 0, max_time), clamp_int(s.iv, -kIvRange, kIvRange)};
}

LabShift set_slider(const LabShift& s, Slider which, int value, const std::vector<Slider>& unlocked) {
  if (std::find(unlocked.begin(), unlocked.end(), which) == unlocked.end()) return s;
  LabShift out = s;
  if (which == Slider::Price) out.price = value;
  if (which == Slider::Time) out.time = value;
  if (which == Slider::Iv) out.iv = value;
  return out;
}

Analysis<WhatIfResult> whatif_from_json(const std::string& text) {
  const auto doc = json::parse(text);
  if (!doc || doc->has("error") || !doc->has("net")) return failure<WhatIfResult>(doc);
  const json::Value& d = *doc;
  WhatIfResult r;
  r.forward = d["forward"].as_string();
  r.at_expiry = d["atExpiry"].as_bool(false);
  for (const auto& l : d["legs"].items()) r.legs.push_back(leg_from(l));
  const json::Value& n = d["net"];
  r.net = {text_of(n["value"]), text_of(n["pnl"]),   text_of(n["delta"]),
           text_of(n["gamma"]), text_of(n["theta"]), text_of(n["vega"])};
  if (!d["expiry"].is_null()) r.expiry = expiry_from(d["expiry"]);
  r.today = curve_from(d["today"]);
  return {std::move(r), {}};
}

Analysis<ComboPreview> preview_from_json(const std::string& text) {
  const auto doc = json::parse(text);
  if (!doc || doc->has("error") || !doc->has("legs")) return failure<ComboPreview>(doc);
  const json::Value& d = *doc;
  ComboPreview p;
  p.accepted = d["accepted"].as_bool(false);
  for (const auto& l : d["legs"].items()) {
    p.legs.push_back({l["accepted"].as_bool(false), text_of(l["reason"]), text_of(l["quote"]), text_of(l["premium"]),
                      text_of(l["fee"]), text_of(l["required"]), text_of(l["availableAfter"])});
  }
  p.net_premium = text_of(d["netPremium"]);
  p.fees = text_of(d["fees"]);
  p.required = text_of(d["required"]);
  p.available_after = text_of(d["availableAfter"]);
  p.option_margin_after_fill = text_of(d["optionMarginAfterFill"]);
  p.position_margin_after_fill = text_of(d["positionMarginAfterFill"]);
  p.available_after_fill = text_of(d["availableAfterFill"]);
  return {std::move(p), {}};
}

std::optional<std::string> unbounded_above(const WhatIfResult& r) {
  if (!r.expiry || r.expiry->max_loss) return std::nullopt;
  if (r.expiry->breakevens.empty()) return std::nullopt;
  return r.expiry->breakevens.back();
}

std::vector<double> result_strikes(const WhatIfResult& r) {
  std::set<double> out;
  for (const auto& l : r.legs) {
    if (l.strike) out.insert(fmt::num(*l.strike));
  }
  return {out.begin(), out.end()};
}

std::optional<PayoffGeometry> payoff_geometry(const WhatIfResult& r, double w, double h, const std::vector<double>& strikes) {
  if (r.today.size() < 2) return std::nullopt;
  PayoffGeometry g;
  g.lo = fmt::num(r.today.front().first);
  g.hi = fmt::num(r.today.back().first);
  g.max_abs = 1e-9;
  const auto widen = [&g](const Curve& c) {
    for (const auto& pv : c) g.max_abs = std::max(g.max_abs, std::fabs(fmt::num(pv.second)));
  };
  widen(r.today);
  if (r.expiry) widen(r.expiry->curve);
  const auto x = [&](double p) { return (p - g.lo) / (g.hi - g.lo) * w; };
  const auto inside = [&](double p) { return p >= g.lo && p <= g.hi; };
  if (r.expiry) g.expiry = points(r.expiry->curve, g.lo, g.hi, w, h, g.max_abs);
  g.today = points(r.today, g.lo, g.hi, w, h, g.max_abs);
  g.zero_y = h / 2;  // y(0)
  g.forward_x = x(fmt::num(r.forward));
  if (r.expiry) {
    for (const auto& b : r.expiry->breakevens) {
      if (inside(fmt::num(b))) g.breakeven_x.push_back(x(fmt::num(b)));
    }
  }
  for (const double k : strikes) {
    if (inside(k)) g.strike_x.push_back(x(k));
  }
  return g;
}

}  // namespace tradelab::desktop::game
