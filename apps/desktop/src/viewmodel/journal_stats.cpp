#include "viewmodel/journal_stats.hpp"

#include <algorithm>
#include <map>
#include <numeric>

#include "model/storage/json_writer.hpp"
#include "viewmodel/curriculum.hpp"

namespace tradelab::desktop {

namespace {

std::optional<double> mean(const std::vector<double>& xs) {
  if (xs.empty()) return std::nullopt;
  return std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
}

std::optional<double> ratio(int n, int d) { return d > 0 ? std::optional<double>(static_cast<double>(n) / d) : std::nullopt; }

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

int count_if_trades(const std::vector<TradeSummary>& ts, bool (*p)(const TradeSummary&)) {
  return static_cast<int>(std::count_if(ts.begin(), ts.end(), p));
}

void r_stats(const std::vector<TradeSummary>& closed, CoreStats& c) {
  std::vector<double> rs, wins, losses;
  for (const TradeSummary& t : closed) {
    if (!t.r_bp) continue;
    const double r = static_cast<double>(*t.r_bp) / 10000.0;
    rs.push_back(r);
    (r > 0 ? wins : losses).push_back(r);
    c.r_buckets[static_cast<std::size_t>(r_bucket(r))] += 1;
  }
  c.with_r = static_cast<int>(rs.size());
  c.without_r = static_cast<int>(closed.size()) - c.with_r;
  c.expectancy_r = mean(rs);
  c.avg_win_r = mean(wins);
  c.avg_loss_r = mean(losses);
}

void stop_stats(const std::vector<TradeSummary>& closed, const std::vector<TradeSummary>& all, CoreStats& c) {
  for (const TradeSummary& t : closed) {
    if (!t.respected) continue;
    c.stops_judged += 1;
    c.stops_respected += *t.respected ? 1 : 0;
  }
  for (const TradeSummary& t : all) {
    if (!t.moved_away) continue;
    c.stops_reported += 1;
    c.moved_away += *t.moved_away > 0 ? 1 : 0;
  }
  c.respected_rate = ratio(c.stops_respected, c.stops_judged);
  c.moved_away_rate = ratio(c.moved_away, c.stops_reported);
}

void risk_stats(const std::vector<TradeSummary>& all, CoreStats& c) {
  std::vector<double> risks;
  for (const TradeSummary& t : all) {
    if (!t.risk_pct) continue;
    const double r = *t.risk_pct;
    risks.push_back(r);
    c.risk_below1 += r < 1 ? 1 : 0;
    c.risk_1to2 += r >= 1 && r <= 2 ? 1 : 0;
    c.risk_above2 += r > 2 ? 1 : 0;
  }
  c.risk_trades = static_cast<int>(risks.size());
  c.avg_risk_pct = mean(risks);
}

bool has_finding(const RunSummary& s, std::string_view code) {
  return std::any_of(s.findings.begin(), s.findings.end(), [&](const FindingSummary& f) { return f.code == code; });
}

using KeysOf = std::vector<std::string> (*)(const StatsRun&, const WebContent*);

std::vector<GroupRow> group_by(const std::vector<StatsRun>& runs, const WebContent* c, KeysOf keys) {
  std::vector<std::pair<std::string, std::vector<StatsRun>>> groups;  // first-seen order, as a JS Map
  for (const StatsRun& r : runs) {
    for (const std::string& k : keys(r, c)) {
      auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == k; });
      if (it == groups.end()) it = groups.insert(groups.end(), {k, {}});
      it->second.push_back(r);
    }
  }
  std::vector<GroupRow> out;
  for (const auto& [key, rs] : groups) {
    const CoreStats s = core_stats(rs);
    out.push_back({key, s.runs, s.trades, s.win_rate, s.expectancy_r});
  }
  return out;
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

int r_bucket(double r) {
  constexpr double kUpper[] = {-2, -1, 0, 1, 2};
  for (int i = 0; i < 5; ++i) {
    if (r <= kUpper[i]) return i;
  }
  return 5;
}

CoreStats core_stats(const std::vector<StatsRun>& runs) {
  CoreStats c;
  std::vector<TradeSummary> all;
  for (const StatsRun& r : runs) all.insert(all.end(), r.summary.trades.begin(), r.summary.trades.end());
  std::vector<TradeSummary> closed;
  std::copy_if(all.begin(), all.end(), std::back_inserter(closed), [](const TradeSummary& t) { return t.reason != "open"; });
  c.runs = static_cast<int>(runs.size());
  c.trades = static_cast<int>(closed.size());
  c.wins = count_if_trades(closed, [](const TradeSummary& t) { return t.net > 0; });
  c.win_rate = ratio(c.wins, c.trades);
  r_stats(closed, c);
  stop_stats(closed, all, c);
  risk_stats(all, c);
  c.oversized_runs = static_cast<int>(std::count_if(runs.begin(), runs.end(), [](const StatsRun& r) { return has_finding(r.summary, "oversized"); }));
  c.liquidations = count_if_trades(closed, [](const TradeSummary& t) { return t.reason == "liquidation"; });
  c.margin_calls = count_if_trades(closed, [](const TradeSummary& t) { return t.reason == "margin_call"; });
  return c;
}

std::optional<std::string> finding_lesson(const std::string& code) {
  static const std::map<std::string, std::string, std::less<>> lessons = {
      {"liquidated", "futures-margin"},   {"stop_beyond_liq", "futures-margin"}, {"leverage_vs_stop", "futures-margin"},
      {"liq_buffer", "futures-margin"},   {"no_stop", "risk-sizing"},            {"oversized", "risk-sizing"},
      {"bankrupt", "risk-sizing"},        {"drawdown_budget", "risk-expectancy"}, {"no_plan", "mind-plan"},
      {"lucky_win", "mind-plan"},         {"margin_called", "options-writing"},  {"wrote_options", "options-writing"},
      {"theta_decay", "options-greeks"},  {"iv_crush", "options-iv"},            {"iv_rise", "options-iv"},
      {"funding_cost", "futures-funding"}, {"gap_slippage", "orders-execution"}, {"capacity", "orders-execution"},
  };
  const auto it = lessons.find(code);
  return it == lessons.end() ? std::nullopt : std::optional<std::string>(it->second);
}

std::vector<MistakeRow> mistakes(const std::vector<StatsRun>& runs) {
  std::vector<MistakeRow> rows;  // first-seen order, then sorted (stable, as Array.prototype.sort)
  for (const StatsRun& r : runs) {
    std::vector<std::pair<std::string, std::string>> codes;  // distinct codes of this run, last tone wins (a JS Map)
    for (const FindingSummary& f : r.summary.findings) {
      if (f.tone != "bad" && f.tone != "warn") continue;
      auto it = std::find_if(codes.begin(), codes.end(), [&](const auto& c) { return c.first == f.code; });
      if (it == codes.end()) codes.emplace_back(f.code, f.tone);
      else it->second = f.tone;
    }
    for (const auto& [code, tone] : codes) {
      auto row = std::find_if(rows.begin(), rows.end(), [&](const MistakeRow& m) { return m.code == code; });
      if (row == rows.end()) row = rows.insert(rows.end(), MistakeRow{code, tone, 0, 0, false, finding_lesson(code)});
      row->runs += 1;
      row->last_at = std::max(row->last_at, r.created_at);
      row->repeats = row->runs >= 2;
    }
  }
  std::stable_sort(rows.begin(), rows.end(), [](const MistakeRow& a, const MistakeRow& b) {
    return a.runs != b.runs ? a.runs > b.runs : a.last_at > b.last_at;
  });
  return rows;
}

std::vector<GroupRow> by_mission(const std::vector<StatsRun>& runs) {
  return group_by(runs, nullptr, [](const StatsRun& r, const WebContent*) { return std::vector<std::string>{r.mission_id}; });
}

std::vector<GroupRow> by_stage(const WebContent& c, const std::vector<StatsRun>& runs) {
  return group_by(runs, &c, [](const StatsRun& r, const WebContent* content) {
    std::vector<std::string> stages = stages_of(*content, r.mission_id);
    return stages.empty() ? std::vector<std::string>{kPracticeStage} : stages;
  });
}

Trend trend(const std::vector<StatsRun>& runs) {
  Trend t;
  const std::size_t n = runs.size();
  const std::size_t w = kTrendWindow;
  const std::vector<StatsRun> recent(runs.begin() + static_cast<std::ptrdiff_t>(n > w ? n - w : 0), runs.end());
  const std::size_t prev_end = n > w ? n - w : 0;
  const std::size_t prev_start = n > 2 * w ? n - 2 * w : 0;
  const std::vector<StatsRun> previous(runs.begin() + static_cast<std::ptrdiff_t>(prev_start), runs.begin() + static_cast<std::ptrdiff_t>(prev_end));
  if (!recent.empty()) t.recent = core_stats(recent);
  if (!previous.empty()) t.previous = core_stats(previous);
  for (const StatsRun& r : runs) {
    const CoreStats c = core_stats({r});
    t.points.push_back({r.run_id, r.mission_id, r.created_at, c.trades, c.expectancy_r, c.respected_rate, r.summary.score});
  }
  return t;
}

JournalStats journal_stats(const WebContent& c, const std::vector<StatsRun>& all, const std::optional<std::string>& mission) {
  std::vector<StatsRun> runs;
  std::copy_if(all.begin(), all.end(), std::back_inserter(runs), [&](const StatsRun& r) { return !mission || r.mission_id == *mission; });
  JournalStats s;
  s.core = core_stats(runs);
  s.mistakes = mistakes(runs);
  s.by_mission = by_mission(runs);
  s.by_stage = by_stage(c, runs);
  s.trend = trend(runs);
  s.too_few = s.core.trades < c.report_min_trades;
  return s;
}

}  // namespace tradelab::desktop
