#include "model/progress_json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::progress_json {

namespace {

using storage::JsonWriter;
using Kind = json::Value::Kind;

std::string number_text(double v) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

void optional_number(JsonWriter& w, std::string_view key, const std::optional<double>& v) {
  w.key(key);
  if (v && std::isfinite(*v)) {
    w.raw(number_text(*v));
  } else {
    w.null();
  }
}

std::optional<double> number_or_none(const json::Value& v) {
  if (v.kind() != Kind::Number) return std::nullopt;
  return v.as_double();
}

std::string_view leg_result_name(LegResult r) {
  switch (r) {
    case LegResult::Bankrupt: return "bankrupt";
    case LegResult::Left: return "left";
    case LegResult::Finished: break;
  }
  return "finished";
}

LegResult leg_result_of(const std::string& s) {
  if (s == "bankrupt") return LegResult::Bankrupt;
  return s == "left" ? LegResult::Left : LegResult::Finished;
}

std::string_view status_name(CampaignStatus s) {
  switch (s) {
    case CampaignStatus::Bankrupt: return "bankrupt";
    case CampaignStatus::Complete: return "complete";
    case CampaignStatus::Abandoned: return "abandoned";
    case CampaignStatus::Active: break;
  }
  return "active";
}

CampaignStatus status_of(const std::string& s) {
  if (s == "bankrupt") return CampaignStatus::Bankrupt;
  if (s == "complete") return CampaignStatus::Complete;
  return s == "abandoned" ? CampaignStatus::Abandoned : CampaignStatus::Active;
}

void write_report(JsonWriter& w, const LegReport& r) {
  w.key("report").begin_object().key("trades").begin_array();
  for (const LegTrade& t : r.trades) {
    w.begin_object().field("net", t.net).key("rBp");
    if (t.r_bp) w.integer(*t.r_bp); else w.null();
    w.key("regime");
    if (t.regime.empty()) w.null(); else w.string(t.regime);
    w.end_object();
  }
  w.end_array().field("netPnl", r.net_pnl).field("fees", r.fees).field("spreadSlippage", r.spread_slippage);
  w.field("funding", r.funding).end_object();
}

LegReport read_report(const json::Value& v) {
  LegReport r;
  for (const json::Value& t : v["trades"].items()) {
    LegTrade trade{t["net"].as_string("0"), std::nullopt, t["regime"].as_string()};
    if (t["rBp"].kind() == Kind::Number) trade.r_bp = static_cast<int>(t["rBp"].as_int());
    r.trades.push_back(std::move(trade));
  }
  r.net_pnl = v["netPnl"].as_string("0");
  r.fees = v["fees"].as_string("0");
  r.spread_slippage = v["spreadSlippage"].as_string("0");
  r.funding = v["funding"].as_string("0");
  return r;
}

}  // namespace

std::string campaign(const CampaignRun& c) {
  JsonWriter w;
  w.begin_object().field("id", c.id).field("startedAt", c.started_at).field("capital", c.capital);
  w.field("index", c.index).field("status", status_name(c.status)).key("legs").begin_array();
  for (const CampaignLeg& leg : c.legs) {
    w.begin_object().field("runId", leg.run_id).field("missionId", leg.mission_id);
    w.field("startCapital", leg.start_capital).field("endCapital", leg.end_capital).field("result", leg_result_name(leg.result));
    if (leg.report) write_report(w, *leg.report);
    w.end_object();
  }
  w.end_array();
  if (c.order) {
    w.key("order").begin_array();
    for (const std::string& m : *c.order) w.string(m);
    w.end_array();
  }
  return w.end_object().text();
}

// campaign.ts isCampaign: an object with a string id and a legs array.
std::optional<CampaignRun> campaign_from(const json::Value& v) {
  if (v["id"].kind() != Kind::String || v["legs"].kind() != Kind::Array) return std::nullopt;
  CampaignRun c;
  c.id = v["id"].as_string();
  c.started_at = v["startedAt"].as_int();
  c.capital = v["capital"].as_string("0");
  c.index = static_cast<int>(v["index"].as_int());
  c.status = status_of(v["status"].as_string());
  for (const json::Value& l : v["legs"].items()) {
    CampaignLeg leg{l["runId"].as_string(), l["missionId"].as_string(), l["startCapital"].as_string("0"),
                    l["endCapital"].as_string("0"), leg_result_of(l["result"].as_string()), std::nullopt};
    if (l["report"].kind() == Kind::Object) leg.report = read_report(l["report"]);
    c.legs.push_back(std::move(leg));
  }
  // storage.ts withOrder: a snapshot only when every entry is a string (else the pre-v4 order).
  const json::Value& order = v["order"];
  const auto is_string = [](const json::Value& m) { return m.kind() == Kind::String; };
  if (order.kind() == Kind::Array && std::ranges::all_of(order.items(), is_string)) {
    c.order.emplace();
    for (const json::Value& m : order.items()) c.order->push_back(m.as_string());
  }
  return c;
}

std::string link(const CampaignLink& l) {
  JsonWriter w;
  return w.begin_object().field("id", l.id).field("index", l.index).end_object().text();
}

std::optional<CampaignLink> link_from(const json::Value& v) {
  if (v["id"].kind() != Kind::String) return std::nullopt;
  return CampaignLink{v["id"].as_string(), static_cast<int>(v["index"].as_int())};
}

std::string assisted(const AssistedLink& a) {
  JsonWriter w;
  w.begin_object().field("from", a.from).field("kind", a.kind);
  if (a.step) w.field("step", *a.step);
  if (!a.code.empty()) w.field("code", a.code);
  return w.end_object().text();
}

std::optional<AssistedLink> assisted_from(const json::Value& v) {
  if (v.kind() != Kind::Object) return std::nullopt;
  AssistedLink a{v["from"].as_string(), v["kind"].as_string("rewind"), std::nullopt, {}};
  if (v["step"].kind() == Kind::Number) a.step = static_cast<int>(v["step"].as_int());
  if (v["code"].kind() == Kind::String) a.code = v["code"].as_string();
  return a;
}

std::string journal(const std::vector<PlanEntry>& entries) {
  JsonWriter w;
  w.begin_array();
  for (const PlanEntry& e : entries) {
    w.begin_object().field("step", e.step).field("side", e.buy ? "buy" : "sell").field("thesis", e.thesis);
    optional_number(w, "invalidation", e.invalidation);
    optional_number(w, "target", e.target);
    w.field("confidence", e.confidence);
    optional_number(w, "plannedRisk", e.planned_risk);
    w.field("skipped", e.skipped);
    if (!e.lose_scenario.empty()) w.field("loseScenario", e.lose_scenario);
    if (!e.instrument.empty()) w.field("instrument", e.instrument);
    if (!e.combo.empty()) w.field("combo", e.combo);
    if (e.trail) w.key("trail").begin_object().field("mode", e.trail->pct ? "pct" : "price").field("value", e.trail->value).end_object();
    w.end_object();
  }
  return w.end_array().text();
}

std::vector<PlanEntry> journal_from(const json::Value& v) {
  std::vector<PlanEntry> out;
  for (const json::Value& e : v.items()) {
    if (e.kind() != Kind::Object) continue;
    PlanEntry p;
    p.step = static_cast<int>(e["step"].as_int());
    p.buy = e["side"].as_string("buy") != "sell";
    p.thesis = e["thesis"].as_string();
    p.invalidation = number_or_none(e["invalidation"]);
    p.target = number_or_none(e["target"]);
    p.confidence = static_cast<int>(e["confidence"].as_int(50));
    p.planned_risk = number_or_none(e["plannedRisk"]);
    p.skipped = e["skipped"].as_bool(false);
    p.lose_scenario = e["loseScenario"].as_string();
    p.instrument = e["instrument"].as_string();
    p.combo = e["combo"].as_string();
    if (e["trail"].kind() == Kind::Object) p.trail = PlanTrail{e["trail"]["mode"].as_string() == "pct", e["trail"]["value"].as_string()};
    out.push_back(std::move(p));
  }
  return out;
}

std::string settings(const Settings& s) {
  JsonWriter w;
  w.begin_object().field("locale", std::string(locale_code(s.locale)));
  w.key("crt").begin_object().field("enabled", s.crt.enabled).field("scanlines", s.crt.scanlines);
  w.field("bloom", s.crt.bloom).field("noise", s.crt.noise).end_object();
  w.field("colorblind", s.colorblind).key("textScale").raw(number_text(static_cast<double>(s.text_scale)));
  if (!s.review_dismissed_on.empty()) w.field("reviewDismissed", s.review_dismissed_on);
  w.field("drillTimer", s.drill_timer);
  return w.end_object().text();
}

Settings settings_from(const json::Value& v) {
  Settings s;
  s.locale = v["locale"].as_string() == "en" ? Locale::En : Locale::PtBR;
  const json::Value& crt = v["crt"];
  s.crt = CrtSettings{crt["enabled"].as_bool(true), crt["scanlines"].as_bool(true), crt["bloom"].as_bool(true),
                      crt["noise"].as_bool(true)};
  s.colorblind = v["colorblind"].as_bool(false);
  s.text_scale = static_cast<float>(v["textScale"].as_double(1.0));
  s.review_dismissed_on = v["reviewDismissed"].as_string();
  s.drill_timer = v["drillTimer"].as_bool(true);  // absent = shown (settings.svelte.ts)
  return s;
}

std::string keymap(const std::map<std::string, std::string>& map) {
  JsonWriter w;
  w.begin_object();
  for (const auto& [action, key] : map) w.field(action, key);
  return w.end_object().text();
}

std::map<std::string, std::string> keymap_from(const json::Value& v) {
  std::map<std::string, std::string> out;
  for (const auto& [action, key] : v.fields()) {
    if (key.kind() == Kind::String) out[action] = key.as_string();
  }
  return out;
}

std::string reviews(const LessonReviews& r) {
  JsonWriter w;
  w.begin_object();
  for (const auto& [id, e] : r) w.key(id).begin_object().field("box", e.box).field("due", e.due).end_object();
  return w.end_object().text();
}

namespace {

// "YYYY-MM-DD" (review.ts DAY).
bool is_day(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
  }
  return true;
}

std::optional<ReviewEntry> review_entry(const json::Value& e) {
  const json::Value& box = e["box"];
  const double b = box.as_double(0);
  if (box.kind() != Kind::Number || b < 1 || b != std::floor(b)) return std::nullopt;
  if (e["due"].kind() != Kind::String || !is_day(e["due"].as_string())) return std::nullopt;
  return ReviewEntry{static_cast<int>(std::min<double>(kReviewRetired, b)), e["due"].as_string()};
}

}  // namespace

LessonReviews reviews_from(const json::Value& v) {
  LessonReviews out;
  for (const auto& [id, e] : v.fields()) {
    if (auto entry = review_entry(e)) out.emplace(id, std::move(*entry));
  }
  return out;
}

}  // namespace tradelab::desktop::progress_json
