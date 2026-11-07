// Mission schema in the state view (task 4.1), explicit wait records (task 4.2) and the scoring v11
// risk inputs (task 4.6).

#include <doctest/doctest.h>

#include <string>

#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/checkpoint.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

bool has(const ScoreReport& r, FindingCode c) {
  for (const Finding& f : r.findings) {
    if (f.code == c) return true;
  }
  return false;
}

Scenario flat(std::size_t n) {
  Scenario s = make_scenario({{"100", "100", "100", "100"}});
  const Bar bar = s.bars[0];
  s.bars.assign(n, bar);
  s.funding_rate.assign(n, Rate{});
  return s;
}

std::vector<DecisionKind> kinds(const RunState& st) {
  std::vector<DecisionKind> out;
  for (const DecisionRecord& d : st.decisions) out.push_back(d.kind);
  return out;
}

}  // namespace

// ---- 4.2 decisions ------------------------------------------------------------------------------

TEST_CASE("Decisions: every advance records act, hold or wait for the step it leaves") {
  const Scenario sc = flat(8);
  Driver d(sc);
  d.step();  // flat, nothing sent: wait
  d.plan(OrderSide::Buy, "95", "50");
  d.step();  // a plan alone is not a trade: wait
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();  // act (fills at the next open)
  d.step();  // in a position, nothing sent: hold
  d.order(OrderType::StopMarket, OrderSide::Sell, "1", "90", true);
  d.step();  // act
  d.close();
  d.step();  // act
  d.step();  // flat again: wait
  using enum DecisionKind;
  CHECK(kinds(d.state()) == std::vector<DecisionKind>{Wait, Wait, Act, Hold, Act, Act, Wait});
  CHECK(d.state().decisions.front().step == 0);
  CHECK(d.state().decisions.back().step == 6);
}

TEST_CASE("Decisions: cancels, moves, leverage and margin are actions; advice and answers are not") {
  const Scenario sc = flat(8);
  Driver d(sc);
  d.order(OrderType::Limit, OrderSide::Buy, "1", "90");
  d.step();
  d.cancel(1);
  d.step();
  d.leverage(5);
  d.step();
  d.answer(5000);  // calibration answers are not trades (and no question is due on this bar anyway)
  d.step();
  using enum DecisionKind;
  CHECK(kinds(d.state()) == std::vector<DecisionKind>{Act, Act, Act, Wait});
}

TEST_CASE("Decisions: a held option is a position; rejected orders are not actions") {
  Scenario sc = flat(10);
  add_options(sc, 8, {"100"});
  Driver d(sc);
  CHECK_FALSE(d.order(OrderType::Market, OrderSide::Buy, "0.05").accepted);  // below the lot size
  d.step();
  d.option(1, OrderSide::Buy, "1");
  d.step();
  d.step();
  using enum DecisionKind;
  CHECK(kinds(d.state()) == std::vector<DecisionKind>{Wait, Act, Hold});
}

TEST_CASE("Decisions are not a digest input and survive a checkpoint") {
  const Scenario sc = flat(6);
  Driver d(sc);
  d.step(2);
  RunState copy = d.state();
  copy.decisions.clear();
  CHECK(state_digest(copy) == state_digest(d.state()));
  const auto bytes = save_checkpoint(d.state(), sc.id);
  const auto restored = load_checkpoint(bytes, sc.id);
  REQUIRE(restored.ok());
  CHECK(restored.value().decisions.size() == 2);
  CHECK(restored.value().decisions[1].kind == DecisionKind::Wait);
}

TEST_CASE("C ABI: the debrief lists each decision") {
  tl_run* run = tl_run_create("m01-false-breakout", 3);
  REQUIRE(run != nullptr);
  tl_advance(run, 2);
  tl_place_order(run, 1, 0, 0, "1", nullptr, 0);
  tl_advance(run, 10'000);
  const std::string d = tl_debrief(run);
  CHECK(d.find(R"("decisions":[{"step":60,"kind":"wait"},{"step":61,"kind":"wait"},{"step":62,"kind":"act"},{"step":63,"kind":"hold"})") !=
        std::string::npos);
  tl_run_destroy(run);
}

// ---- 4.1 mission rules --------------------------------------------------------------------------

TEST_CASE("Mission rules: objectives, permissions and warning distances come from the engine") {
  tl_run* m02 = tl_run_create("m02-funding-squeeze", 1);
  REQUIRE(m02 != nullptr);
  std::string s = tl_state(m02);
  CHECK(s.find(R"("missionRules":{"initialBalance":"10000.00000000","leverageCap":10,"warmupBars":50,)"
               R"("permissions":{"long":true,"short":true,"optionsBuy":false,"optionsWrite":false},)"
               R"("objectives":[{"code":"plan_every_entry","valueBp":0},{"code":"risk_per_idea","valueBp":200},{"code":"liq_distance","valueBp":1000}],)"
               R"("liqWarningBp":1000,"drawdownBudgetBp":2000,"confirmStopBeyondLiq":true,"spreadMargin":false})") != std::string::npos);
  tl_run_destroy(m02);
  tl_run* m09 = tl_run_create("m09-event-straddle", 1);
  s = tl_state(m09);
  CHECK(s.find(R"("permissions":{"long":true,"short":true,"optionsBuy":true,"optionsWrite":false})") != std::string::npos);
  tl_run_destroy(m09);
  tl_run* m06 = tl_run_create("m06-thin-book", 1);
  s = tl_state(m06);
  CHECK(s.find(R"("permissions":{"long":true,"short":false,"optionsBuy":false,"optionsWrite":false})") != std::string::npos);
  tl_run_destroy(m06);
  tl_run* m10 = tl_run_create("m10-hedged-book", 1);
  s = tl_state(m10);
  CHECK(s.find(R"("optionsBuy":true,"optionsWrite":true})") != std::string::npos);
  tl_run_destroy(m10);
  // m07 declares a 10% drawdown budget; the campaign's leverage gate shows as the run's cap.
  tl_run* m07 = tl_run_create_ex("m07-trend-trailing", 1, 0, 0, 0, "5000", 2);
  s = tl_state(m07);
  CHECK(s.find(R"("missionRules":{"initialBalance":"5000.00000000","leverageCap":2,)") != std::string::npos);
  CHECK(s.find(R"("drawdownBudgetBp":1000)") != std::string::npos);
  tl_run_destroy(m07);
}

TEST_CASE("Mission rules: every built-in mission declares objectives and valid budgets") {
  for (const MissionInfo& m : mission_catalog()) {
    Scenario sc;
    REQUIRE(build_mission(m.id, 1, sc));
    CHECK_MESSAGE(sc.mission.objectives.size() >= 3, m.id);
    CHECK(sc.mission.liq_warning_bp > 0);
    CHECK(sc.mission.drawdown_budget_bp > 0);
  }
  Scenario bad = flat(5);
  bad.mission.drawdown_budget_bp = 0;
  CHECK(validate(bad) == ScenarioError::InvalidMission);
  bad.mission.drawdown_budget_bp = 2000;
  bad.mission.objectives.push_back({static_cast<ObjectiveCode>(200), 0});
  CHECK(validate(bad) == ScenarioError::InvalidMission);
}

// ---- 4.6 risk inputs ----------------------------------------------------------------------------

TEST_CASE("Risk v11: leverage too high for the stop distance costs 10, the same trade at lower leverage does not") {
  // A long at 100 with its stop at 96 (4 away). At 10x the liquidation sits about 9 below the entry:
  // in front of the stop but closer than twice its distance (8). At 5x it sits about 19 below.
  const Scenario sc = flat(6);
  const auto run = [&sc](int lev) {
    Driver d(sc);
    d.leverage(lev);
    d.plan(OrderSide::Buy, "96", "40");
    d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "96", nullptr);
    d.step(5);
    return score_run(sc, d.state());
  };
  const ScoreReport low = run(5);
  const ScoreReport high = run(12);
  CHECK(low.risk.score == 100);
  CHECK_FALSE(has(low, FindingCode::LeverageVsStop));
  CHECK(high.risk.score == 90);
  CHECK(has(high, FindingCode::LeverageVsStop));
  CHECK(high.lesson == LessonCode::Futures2);
  CHECK(*high.risk.score <= *low.risk.score);  // leverage never raises the risk score
}

TEST_CASE("Risk v11: liquidation closer than the mission's warning distance costs 10") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "93", "93"},
                               {"93", "93", "93", "93"}, {"93", "93", "93", "93"}});
  sc.mission.liq_warning_bp = 300;
  const auto run = [&sc]() {
    Driver d(sc);
    d.leverage(10);  // liquidation about 9.1% below the entry; the close at 93 leaves about 2.3%
    d.plan(OrderSide::Buy, "80", "200");
    d.bracket(OrderType::Market, OrderSide::Buy, "1", nullptr, "80", nullptr);
    d.step(4);
    return score_run(sc, d.state());
  };
  const ScoreReport r = run();
  CHECK(has(r, FindingCode::LiqBuffer));
  sc.mission.liq_warning_bp = 100;  // a mission that only warns within 1%
  const ScoreReport loose = run();
  CHECK_FALSE(has(loose, FindingCode::LiqBuffer));
  CHECK(*r.risk.score == *loose.risk.score - 10);
}

TEST_CASE("Risk v11: drawdown is judged against the mission's budget") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "88", "88"},
                               {"88", "88", "88", "88"}});
  const auto run = [&sc]() {
    Driver d(sc);
    d.plan(OrderSide::Buy, "50", "200");
    d.bracket(OrderType::Market, OrderSide::Buy, "100", nullptr, "50", nullptr);  // a 12% fall on 100 units = 1,200 (12%)
    d.step(3);
    return score_run(sc, d.state());
  };
  const ScoreReport default_budget = run();  // 20%
  CHECK_FALSE(has(default_budget, FindingCode::DrawdownBudget));
  sc.mission.drawdown_budget_bp = 1000;      // m07's 10%
  const ScoreReport tight = run();
  CHECK(has(tight, FindingCode::DrawdownBudget));
  CHECK(*tight.risk.score == *default_budget.risk.score - 15);
}
