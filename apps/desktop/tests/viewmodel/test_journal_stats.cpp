// Journal statistics (add-run-sharing-and-journal-stats D7): every case of
// tests/fixtures/web/journalStats.json (the web's journalStats.ts and runSummary.ts) through the C++
// port, compared field by field (numbers with the parity tolerance), plus the summary's JSON round trip.
#include <doctest/doctest.h>

#include <string>

#include "support/web_fixture.hpp"
#include "viewmodel/journal_stats.hpp"
#include "model/content.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::test_support;

namespace {

using storage::JsonWriter;

void num(JsonWriter& w, std::string_view k, const std::optional<double>& v) {
  if (!v) w.key(k).null();
  else {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", *v);
    w.key(k).raw(buf);
  }
}

void core(JsonWriter& w, const CoreStats& c) {
  w.begin_object().field("runs", c.runs).field("trades", c.trades).field("wins", c.wins);
  num(w, "winRate", c.win_rate);
  w.field("withR", c.with_r).field("withoutR", c.without_r);
  num(w, "expectancyR", c.expectancy_r);
  num(w, "avgWinR", c.avg_win_r);
  num(w, "avgLossR", c.avg_loss_r);
  w.key("rBuckets").begin_array();
  for (int b : c.r_buckets) w.integer(b);
  w.end_array().field("stopsJudged", c.stops_judged).field("stopsRespected", c.stops_respected);
  num(w, "respectedRate", c.respected_rate);
  w.field("stopsReported", c.stops_reported).field("movedAway", c.moved_away);
  num(w, "movedAwayRate", c.moved_away_rate);
  w.field("riskTrades", c.risk_trades);
  num(w, "avgRiskPct", c.avg_risk_pct);
  w.field("riskBelow1", c.risk_below1).field("risk1to2", c.risk_1to2).field("riskAbove2", c.risk_above2);
  w.field("oversizedRuns", c.oversized_runs).field("liquidations", c.liquidations).field("marginCalls", c.margin_calls).end_object();
}

void groups(JsonWriter& w, const std::vector<GroupRow>& rows) {
  w.begin_array();
  for (const GroupRow& g : rows) {
    w.begin_object().field("key", g.key).field("runs", g.runs).field("trades", g.trades);
    num(w, "winRate", g.win_rate);
    num(w, "expectancyR", g.expectancy_r);
    w.end_object();
  }
  w.end_array();
}

json::Value parsed(const JsonWriter& w) { return *json::parse(w.text()); }

// Structural equality: same keys and kinds, numbers within the parity tolerance (null = none).
void same(const json::Value& got, const json::Value& want, const std::string& path) {
  INFO(path);
  if (want.kind() == json::Value::Kind::Number || got.kind() == json::Value::Kind::Number) {
    REQUIRE(got.kind() == want.kind());
    CHECK(same_number(got.as_double(), want));
    return;
  }
  REQUIRE(got.kind() == want.kind());
  if (want.kind() == json::Value::Kind::Array) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) same(got[i], want[i], path + "[" + std::to_string(i) + "]");
  } else if (want.kind() == json::Value::Kind::Object) {
    REQUIRE(got.fields().size() == want.fields().size());
    for (const auto& [k, v] : want.fields()) same(got[k], v, path + "." + k);
  } else {
    CHECK(dump(got) == dump(want));
  }
}

std::vector<StatsRun> runs_of(const json::Value& a) {
  std::vector<StatsRun> out;
  for (const json::Value& r : a.items()) {
    out.push_back({r["runId"].as_string(), r["missionId"].as_string(), r["createdAt"].as_int(), *summary_from_json(r["summary"])});
  }
  return out;
}

void trend_json(JsonWriter& w, const Trend& t) {
  w.begin_object().key("recent");
  if (t.recent) core(w, *t.recent);
  else w.null();
  w.key("previous");
  if (t.previous) core(w, *t.previous);
  else w.null();
  w.key("points").begin_array();
  for (const TrendPoint& p : t.points) {
    w.begin_object().field("runId", p.run_id).field("missionId", p.mission_id).field("createdAt", p.created_at).field("trades", p.trades);
    num(w, "expectancyR", p.expectancy_r);
    num(w, "respectedRate", p.respected_rate);
    if (p.score) w.field("score", *p.score);
    else w.key("score").null();
    w.end_object();
  }
  w.end_array().end_object();
}

std::map<std::string, Case> ports(const WebContent& c) {
  return {
      {"rBucket", [](const json::Value& a, const json::Value& o) { CHECK(r_bucket(a[0].as_double()) == o.as_int()); }},
      {"coreStats",
       [](const json::Value& a, const json::Value& o) {
         JsonWriter w;
         core(w, core_stats(runs_of(a[0])));
         same(parsed(w), o, "core");
       }},
      {"mistakes",
       [](const json::Value& a, const json::Value& o) {
         JsonWriter w;
         w.begin_array();
         for (const MistakeRow& m : mistakes(runs_of(a[0]))) {
           w.begin_object().field("code", m.code).field("tone", m.tone).field("runs", m.runs).field("lastAt", m.last_at).field("repeats", m.repeats);
           w.field("lesson", m.lesson).end_object();
         }
         same(parsed(w.end_array()), o, "mistakes");
       }},
      {"byMission",
       [](const json::Value& a, const json::Value& o) {
         JsonWriter w;
         groups(w, by_mission(runs_of(a[0])));
         same(parsed(w), o, "byMission");
       }},
      {"byStage",
       [&c](const json::Value& a, const json::Value& o) {
         JsonWriter w;
         groups(w, by_stage(c, runs_of(a[0])));
         same(parsed(w), o, "byStage");
       }},
      {"trend",
       [](const json::Value& a, const json::Value& o) {
         JsonWriter w;
         trend_json(w, trend(runs_of(a[0])));
         same(parsed(w), o, "trend");
       }},
      {"journalStats",
       [&c](const json::Value& a, const json::Value& o) {
         const auto mission = a[1].is_null() ? std::nullopt : std::optional<std::string>(a[1].as_string());
         const JournalStats s = journal_stats(c, runs_of(a[0]), mission);
         CHECK(s.core.trades == o["trades"].as_int());
         CHECK(s.core.runs == o["runs"].as_int());
         CHECK(s.too_few == o["tooFew"].as_bool());
       }},
      {"summarize",
       [](const json::Value& a, const json::Value& o) {
         const auto s = summarize(a[0]);
         if (o.is_null()) {
           CHECK_FALSE(s.has_value());
           return;
         }
         REQUIRE(s.has_value());
         same(*json::parse(summary_json(*s)), o, "summary");
       }},
  };
}

}  // namespace

TEST_CASE("web parity: journalStats.ts and runSummary.ts") { replay("journalStats", ports(web_content())); }
