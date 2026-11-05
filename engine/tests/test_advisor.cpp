// Advisor and advisor points (specs advisor and progression-rewards; design D1-D3, D6).

#include <doctest/doctest.h>

#include <string>

#include "scenario_builder.hpp"
#include "tradelab/advisor.hpp"
#include "tradelab/c_api.h"
#include "tradelab/checkpoint.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

bool same(const Advice& a, const Advice& b) {
  if (a.step != b.step || a.stance != b.stance || a.confidence_bp != b.confidence_bp || a.stop != b.stop ||
      a.size != b.size || a.reasons.size() != b.reasons.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.reasons.size(); ++i) {
    const AdviceReason& x = a.reasons[i];
    const AdviceReason& y = b.reasons[i];
    if (x.code != y.code || x.vote != y.vote || x.ref != y.ref) return false;
  }
  return true;
}

const AdviceReason* find_reason(const Advice& a, AdviceReasonCode code) {
  for (const AdviceReason& r : a.reasons) {
    if (r.code == code) return &r;
  }
  return nullptr;
}

Advice advice_at(const Scenario& sc, std::uint32_t step, std::uint8_t news_lead = 0) {
  RunState st = start_run(sc, 1, RunOptions{0, news_lead, false});
  st.step = step;
  return advise(sc, st);
}

// Everything after step s changes: bars, funding not yet published, news beyond the lead.
Scenario alter_future(const Scenario& sc, std::uint32_t s, std::uint32_t news_lead) {
  Scenario f = sc;
  for (std::size_t i = s + 1; i < f.bars.size(); ++i) {
    Bar& b = f.bars[i];
    b.open = Price::from_raw(b.open.raw * 3 / 2);
    b.high = Price::from_raw(b.high.raw * 2);
    b.low = Price::from_raw(b.low.raw / 2);
    b.close = Price::from_raw(b.close.raw / 3);
  }
  for (std::size_t i = s + 2; i < f.funding_rate.size(); ++i) f.funding_rate[i] = Rate::from_raw(-f.funding_rate[i].raw * 50);
  for (NewsItem& n : f.news) {
    if (n.release_step <= s + news_lead) continue;
    n.actual = static_cast<std::int8_t>(-n.actual);
    n.expectation = static_cast<std::int8_t>(n.expectation == 0 ? 2 : -n.expectation);
    n.credibility = 100;
  }
  return f;
}

Scenario mission(const char* id, std::uint64_t seed) {
  Scenario sc;
  REQUIRE(build_mission(id, seed, sc));
  return sc;
}

Command advice_request(const RunState& st, std::uint64_t seq) {
  Command c;
  c.type = CommandType::RequestAdvice;
  c.seq = seq;
  c.client_id = seq;
  c.decision_step = st.step;
  return c;
}

}  // namespace

TEST_CASE("Advisor: no look-ahead (future bars, funding and unreleased news do not change advice)") {
  int compared = 0;
  for (const MissionInfo& m : mission_catalog()) {
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
      const Scenario sc = mission(m.id.c_str(), seed);
      for (std::uint32_t s = sc.mission.warmup_bars; s + 1 < sc.bars.size(); s += 7) {
        for (const std::uint8_t lead : {std::uint8_t{0}, std::uint8_t{2}}) {
          const Scenario future = alter_future(sc, s, lead);
          CHECK(same(advice_at(sc, s, lead), advice_at(future, s, lead)));
          compared += 1;
        }
      }
    }
  }
  CHECK(compared > 500);
}

TEST_CASE("Advisor: content bounds (confidence 50-65%, stop on the protective side, 1% risk)") {
  const Scenario sc = mission("m02-funding-squeeze", 3);
  int directional = 0;
  for (std::uint32_t s = sc.mission.warmup_bars; s + 1 < sc.bars.size(); ++s) {
    const Advice a = advice_at(sc, s);
    CHECK(a.confidence_bp >= 5000);
    CHECK(a.confidence_bp <= kAdviceConfidenceCap);
    CHECK(a.step == s);
    for (const AdviceReason& r : a.reasons) CHECK(r.vote >= -2);
    if (a.stance == AdviceStance::Wait) {
      CHECK(a.stop.raw == 0);
      CHECK(a.size.raw == 0);
      continue;
    }
    directional += 1;
    const Price mark = sc.bars[s].close;
    const bool long_side = a.stance == AdviceStance::Long;
    CHECK((long_side ? a.stop < mark : a.stop > mark));
    // Risk at the stop never exceeds 1% of the starting equity (10,000).
    const std::int64_t dist = long_side ? mark.raw - a.stop.raw : a.stop.raw - mark.raw;
    const auto risk = static_cast<double>(dist) / Price::kScale * static_cast<double>(a.size.raw) / Qty::kScale;
    CHECK(risk <= 100.0 + 1e-6);
    CHECK(risk > 40.0);  // and is not far below it (lot rounding, halved in a squeeze)
  }
  CHECK(directional > 5);
}

TEST_CASE("Advisor: a range with an oversold RSI never advises a short") {
  int cases = 0;
  for (const MissionInfo& m : mission_catalog()) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
      const Scenario sc = mission(m.id.c_str(), seed);
      for (std::uint32_t s = sc.mission.warmup_bars; s + 1 < sc.bars.size(); ++s) {
        const Advice a = advice_at(sc, s);
        const AdviceReason* rsi = find_reason(a, AdviceReasonCode::RsiExtreme);
        if (find_reason(a, AdviceReasonCode::RegimeRange) == nullptr || rsi == nullptr || rsi->vote != 1) continue;
        cases += 1;
        CHECK(a.stance != AdviceStance::Short);
      }
    }
  }
  CHECK(cases > 0);
}

TEST_CASE("Advisor: a credible shock released at the previous step is already priced") {
  const Scenario sc = mission("m01-false-breakout", 4);
  const NewsItem& denial = sc.news[1];  // credible -2 surprise
  REQUIRE(denial.kind == NewsKind::RumorDenied);
  const Advice a = advice_at(sc, denial.release_step + 1);
  const AdviceReason* priced = find_reason(a, AdviceReasonCode::NewsPriced);
  REQUIRE(priced != nullptr);
  CHECK(priced->vote == 0);
  CHECK(priced->ref == static_cast<std::int32_t>(denial.release_step));
  CHECK(find_reason(a, AdviceReasonCode::NewsEarly) == nullptr);
  // The rumor (credibility 45, no surprise) is never a reason.
  CHECK(find_reason(advice_at(sc, sc.news[0].release_step), AdviceReasonCode::NewsPriced) == nullptr);
}

TEST_CASE("Advisor: a fast wire turns the coming headline into a vote; without it the headline is unseen") {
  const Scenario sc = mission("m01-false-breakout", 4);
  const std::uint32_t r = sc.news[1].release_step;
  const Advice a = advice_at(sc, r - 1, 1);
  const AdviceReason* early = find_reason(a, AdviceReasonCode::NewsEarly);
  REQUIRE(early != nullptr);
  CHECK(early->vote == -2);
  CHECK(early->ref == static_cast<std::int32_t>(r));
  CHECK(find_reason(advice_at(sc, r - 1, 0), AdviceReasonCode::NewsEarly) == nullptr);
  CHECK(find_reason(advice_at(sc, r - 2, 1), AdviceReasonCode::NewsEarly) == nullptr);
}

TEST_CASE("RequestAdvice: rationed by the allowance, logged, no time passes; NoAdviceLeft changes nothing") {
  const Scenario sc = mission("m03-volatility-shock", 5);
  RunState st = start_run(sc, 5, RunOptions{1, 0, false});
  CHECK(st.advice_left == 1);
  const CommandResult ok = apply_command(sc, st, advice_request(st, 1));
  CHECK(ok.accepted);
  REQUIRE(st.advice.size() == 1);
  CHECK(st.advice[0].step == st.step);
  CHECK(st.advice_left == 0);
  CHECK(st.step == sc.mission.warmup_bars);  // no time passed
  CHECK(st.events.empty());                  // no orders, fills or balance changes
  CHECK(st.wallet == sc.mission.initial_balance);

  const Digest before = state_digest(st);
  const CommandResult no = apply_command(sc, st, advice_request(st, 2));
  CHECK_FALSE(no.accepted);
  CHECK(no.reason == Reason::NoAdviceLeft);
  CHECK(state_digest(st) == before);
  CHECK(st.advice.size() == 1);

  RunState none = start_run(sc, 5);  // default options: no allowance
  CHECK(apply_command(sc, none, advice_request(none, 1)).reason == Reason::NoAdviceLeft);
}

TEST_CASE("RequestAdvice: checkpoints keep the options, the allowance and the advice") {
  const Scenario sc = mission("m02-funding-squeeze", 6);
  RunState st = start_run(sc, 6, RunOptions{3, 1, true});
  (void)apply_command(sc, st, advice_request(st, 1));
  advance(sc, st);
  auto restored = load_checkpoint(save_checkpoint(st, sc.id), sc.id);
  REQUIRE(restored.ok());
  CHECK(state_digest(restored.value()) == state_digest(st));
  CHECK(restored.value().options.news_lead == 1);
  CHECK(restored.value().options.calendar);
  CHECK(restored.value().advice_left == 2);
  CHECK(restored.value().advice.size() == 1);
}

// ---- advisor points (spec progression-rewards) ----------------------------------------------

namespace {
Scenario rising() {
  return make_scenario({{"100", "100", "100", "100"}, {"100", "101", "99", "100"}, {"100", "106", "100", "105"},
                        {"105", "111", "105", "110"}, {"110", "110", "110", "110"}, {"110", "110", "110", "110"}});
}

std::uint32_t points(Driver& d) { return score_run(d.scenario(), d.state()).advisor_points_earned; }
}  // namespace

TEST_CASE("Points: a planned, stopped, winning trade in a finished run earns 1 + 1 + 2") {
  const Scenario sc = rising();
  Driver d(sc);
  d.plan(OrderSide::Buy, "95", "50");
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step(5);
  REQUIRE(d.state().status == RunStatus::Finished);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].net.raw > 0);
  CHECK(r.advisor_points_earned == 4);
}

TEST_CASE("Points: an unplanned winner earns nothing; a planned trade without a stop earns nothing") {
  const Scenario sc = rising();
  Driver unplanned(sc);
  unplanned.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  unplanned.step(5);
  CHECK(points(unplanned) == 2);  // finishing only

  Driver no_stop(sc);
  no_stop.plan(OrderSide::Buy, "95", "50");
  no_stop.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, nullptr, "110");
  no_stop.step(5);
  CHECK(points(no_stop) == 2);
}

TEST_CASE("Points: a stop added after entry counts; a losing disciplined trade earns 1; cap at 6") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "94", "95"}, {"95", "95", "95", "95"}});
  Driver d(sc);
  d.plan(OrderSide::Buy, "96", "40");
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step(1);
  d.order(OrderType::StopMarket, OrderSide::Sell, "10", "96", true);
  d.step(3);
  REQUIRE(d.state().status == RunStatus::Finished);
  CHECK(points(d) == 3);  // 1 (planned, stopped loss) + 2 (finished)

  // Three disciplined winners and a finish would be 8: capped at 6.
  std::vector<BarSpec> bars;
  for (int i = 0; i < 3; ++i) {  // decide, fill at the next open, take profit on the bar after
    bars.push_back({"100", "100", "100", "100"});
    bars.push_back({"100", "100", "100", "100"});
    bars.push_back({"100", "106", "100", "105"});
  }
  bars.push_back({"100", "100", "100", "100"});
  Scenario up = make_scenario({{"100", "100", "100", "100"}});
  up.bars.clear();
  for (const BarSpec& b : bars) up.bars.push_back({P(b.o), P(b.h), P(b.l), P(b.c), Q("1000000"), false});
  up.funding_rate.assign(up.bars.size(), Rate{});
  Driver w(up);
  for (int i = 0; i < 3; ++i) {
    CHECK(w.plan(OrderSide::Buy, "95", "50").accepted);
    CHECK(w.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "105").accepted);
    w.step(3);
  }
  w.step(10);
  REQUIRE(w.state().status == RunStatus::Finished);
  const ScoreReport r = score_run(up, w.state());
  CHECK(r.trades.size() == 3);
  CHECK(advisor_points(w.state(), r.trades) == kAdvisorPointsCap);
}

TEST_CASE("Points: a bankrupt run earns no finishing bonus") {
  RunState st;
  st.status = RunStatus::Bankrupt;
  CHECK(advisor_points(st, {}) == 0);
  st.status = RunStatus::Finished;
  CHECK(advisor_points(st, {}) == 2);
}

// ---- C ABI ----------------------------------------------------------------------------------

TEST_CASE("C ABI: start options, advice in the state and the debrief, no_advice_left") {
  tl_run* run = tl_run_create_ex("m01-false-breakout", 5, 2, 0, 0, nullptr, 0);
  REQUIRE(run != nullptr);
  std::string s = tl_state(run);
  CHECK(s.find("\"advisor\":{\"version\":1,\"allowance\":2,\"left\":2,\"newsLead\":0,\"calendar\":false,\"advice\":[]}") !=
        std::string::npos);
  std::string r = tl_request_advice(run, 1);
  CHECK(r.find("\"accepted\":true") != std::string::npos);
  s = tl_state(run);
  CHECK(s.find("\"left\":1") != std::string::npos);
  CHECK(s.find("\"stance\":") != std::string::npos);
  CHECK(s.find("\"confidenceBp\":") != std::string::npos);
  CHECK(s.find("\"code\":\"") != std::string::npos);
  tl_advance(run, 3);
  CHECK(std::string(tl_request_advice(run, 2)).find("\"accepted\":true") != std::string::npos);
  CHECK(std::string(tl_request_advice(run, 3)).find("no_advice_left") != std::string::npos);
  tl_advance(run, 10000);
  const std::string d = tl_debrief(run);
  CHECK(d.find("\"score\":{\"version\":7") != std::string::npos);
  CHECK(d.find("\"advisorPointsEarned\":2") != std::string::npos);  // finished, no trades
  CHECK(d.find("\"advice\":[{\"step\":60,") != std::string::npos);
  CHECK(d.find("\"adviceHitBp\":") != std::string::npos);
  tl_run_destroy(run);
  CHECK(tl_run_create_ex("m01-false-breakout", 5, 0, 256, 0, nullptr, 0) == nullptr);
}

// Golden (design D6): m02 with {allowance 3, news_lead 1, calendar} and two pieces of advice.
TEST_CASE("Golden: scripted run with start options and advice is bit-frozen across targets") {
  tl_run* run = tl_run_create_ex("m02-funding-squeeze", 2026, 3, 1, 1, nullptr, 0);
  tl_request_advice(run, 1);
  tl_set_leverage(run, 2, 3);
  tl_record_plan(run, 3, 0, 0, 0, 1, "95", "115", 60, "100");
  tl_place_order_ex(run, 4, 0, 0, "50", nullptr, 0, "95", "115", nullptr);
  tl_advance(run, 25);
  tl_request_advice(run, 5);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(s.find("\"allowance\":3,\"left\":1,\"newsLead\":1,\"calendar\":true") != std::string::npos);
  CHECK(s.find("\"type\":\"fill\"") != std::string::npos);
  CHECK(s.substr(s.find("\"digest\"")) ==
        "\"digest\":\"bf0d684d1a7b13fa984744e742be1c5b414d12f186819bb9b7d7d95143dc79c6\","
        "\"eventsDigest\":\"c83e5525e4d03ad71d5eafcef3af65a2e1d43798c5303ec43e51cbc89e7a888f\"}");
}
