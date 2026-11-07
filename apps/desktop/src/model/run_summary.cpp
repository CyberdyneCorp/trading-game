#include "model/run_summary.hpp"

#include <cstdio>

#include "model/storage/json_writer.hpp"

namespace tradelab::desktop {

namespace {

std::optional<std::int64_t> opt_int(const json::Value& v) {
  return v.kind() == json::Value::Kind::Number ? std::optional<std::int64_t>(v.as_int()) : std::nullopt;
}

std::optional<double> opt_double(const json::Value& v) {
  return v.kind() == json::Value::Kind::Number ? std::optional<double>(v.as_double()) : std::nullopt;
}

// An engine decimal string as a number for display arithmetic (the web's Number(text)).
double decimal(const json::Value& v) { return json::Value::make_number(v.as_string("0")).as_double(); }

std::optional<bool> opt_bool(const json::Value& v) {
  return v.kind() == json::Value::Kind::Bool ? std::optional<bool>(v.as_bool()) : std::nullopt;
}

TradeSummary trade_of(const json::Value& tr, double initial) {
  const double risk = decimal(tr["plannedRisk"]);
  const bool perp = tr["series"].as_int() == 0;
  TradeSummary t;
  t.net = decimal(tr["net"]);
  t.r_bp = opt_int(tr["rBp"]);
  if (risk > 0 && initial > 0) t.risk_pct = (risk / initial) * 100;
  t.reason = tr["reason"].as_string();
  t.respected = opt_bool(tr["planRespected"]);
  t.moved_away = perp ? opt_int(tr["stopMovedAway"]) : std::nullopt;
  t.option = !perp;
  return t;
}

void opt_field(storage::JsonWriter& w, std::string_view k, const std::optional<std::int64_t>& v) {
  if (v) w.field(k, *v);
  else w.key(k).null();
}

std::string number_text(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  return buf;
}

}  // namespace

std::optional<RunSummary> summarize(const json::Value& d) {
  const std::string status = d["status"].as_string();
  if (status != "finished" && status != "bankrupt") return std::nullopt;
  RunSummary s;
  s.mission = d["mission"].as_string();
  s.status = status;
  s.net = decimal(d["netPnl"]);
  s.initial = decimal(d["finalEquity"]) - s.net;
  s.score = opt_int(d["score"]["total"]);
  for (const json::Value& tr : d["trades"].items()) s.trades.push_back(trade_of(tr, s.initial));
  for (const json::Value& f : d["findings"].items()) s.findings.push_back({f["code"].as_string(), f["tone"].as_string()});
  return s;
}

std::string summary_json(const RunSummary& s) {
  storage::JsonWriter w;
  w.begin_object().field("v", 1).field("mission", s.mission).field("status", s.status);
  w.key("initial").raw(number_text(s.initial)).key("net").raw(number_text(s.net));
  opt_field(w, "score", s.score);
  w.key("trades").begin_array();
  for (const TradeSummary& t : s.trades) {
    w.begin_object().key("net").raw(number_text(t.net));
    opt_field(w, "rBp", t.r_bp);
    if (t.risk_pct) w.key("riskPct").raw(number_text(*t.risk_pct));
    else w.key("riskPct").null();
    w.field("reason", t.reason);
    if (t.respected) w.field("respected", *t.respected);
    else w.key("respected").null();
    opt_field(w, "movedAway", t.moved_away);
    w.field("option", t.option).end_object();
  }
  w.end_array().key("findings").begin_array();
  for (const FindingSummary& f : s.findings) w.begin_object().field("code", f.code).field("tone", f.tone).end_object();
  return w.end_array().end_object().text();
}

std::optional<RunSummary> summary_from_json(const json::Value& v) {
  if (v["v"].as_int() != 1) return std::nullopt;
  RunSummary s;
  s.mission = v["mission"].as_string();
  s.status = v["status"].as_string();
  s.initial = v["initial"].as_double();
  s.net = v["net"].as_double();
  s.score = opt_int(v["score"]);
  for (const json::Value& t : v["trades"].items()) {
    s.trades.push_back({t["net"].as_double(), opt_int(t["rBp"]), opt_double(t["riskPct"]), t["reason"].as_string(),
                        opt_bool(t["respected"]), opt_int(t["movedAway"]), t["option"].as_bool()});
  }
  for (const json::Value& f : v["findings"].items()) s.findings.push_back({f["code"].as_string(), f["tone"].as_string()});
  return s;
}

}  // namespace tradelab::desktop
