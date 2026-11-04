#include <doctest/doctest.h>

#include "scenario_builder.hpp"
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
}  // namespace

TEST_CASE("EDU02: waiting the whole mission keeps risk at 100 and plan not applicable") {
  const Scenario sc = flat(10);
  Driver d(sc);
  d.step(9);
  const ScoreReport r = score_run(sc, d.state());
  CHECK(r.risk.score == 100);
  CHECK_FALSE(r.plan.score.has_value());
  CHECK(r.total == 100);
  CHECK(has(r, FindingCode::Waited));
  CHECK(r.trades.empty());
}

TEST_CASE("EDU01: a planned, protected, stopped-out loss is credited as good process") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}, {"100", "100", "94", "95"}, {"95", "95", "95", "95"}});
  Driver d(sc);
  d.plan(OrderSide::Buy, "95", "50");
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  CHECK(r.risk.score == 100);
  CHECK(r.plan.score == 100);
  CHECK(has(r, FindingCode::GoodProcessLoss));
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].reason == ExitReason::Stop);
  CHECK(r.trades[0].plan_respected == true);
  CHECK(r.trades[0].r_bp == -10000);  // lost exactly the planned 50 credits = -1R
}

TEST_CASE("Liquidation dominates risk and recommends Futures II") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "80", "85"}, {"85", "85", "85", "85"}});
  Driver d(sc);
  d.leverage(10);
  d.plan(OrderSide::Buy, nullptr, "100");
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step(3);
  const ScoreReport r = score_run(sc, d.state());
  CHECK(r.risk.score <= 45);
  CHECK(has(r, FindingCode::Liquidated));
  CHECK(r.lesson == LessonCode::Futures2);
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].reason == ExitReason::Liquidation);
}

TEST_CASE("Profit without a plan or stop is flagged as possibly luck") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "111", "100", "110"}, {"110", "110", "110", "110"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step(3);
  const ScoreReport r = score_run(sc, d.state());
  CHECK(has(r, FindingCode::LuckyWin));
  CHECK(has(r, FindingCode::NoPlan));
  CHECK(has(r, FindingCode::NoStop));
  CHECK(r.plan.score == 0);
  CHECK(r.lesson == LessonCode::Risk);
}

TEST_CASE("Calibration: questions only on schedule, Brier over at least three resolved answers") {
  Scenario sc = flat(60);
  for (std::size_t i = 0; i < sc.bars.size(); ++i) {  // rising closes: "above the threshold" resolves true
    const auto px = Price::from_raw(static_cast<std::int64_t>((100 + i) * Price::kScale));
    sc.bars[i] = {px, px, px, px, Q("1"), false};
  }
  Driver d(sc);
  CHECK(question_due(sc, d.state()));
  CHECK(d.answer(9000).accepted);
  CHECK(d.answer(9000).reason == Reason::NoQuestionDue);  // already answered at this step
  d.step();
  CHECK(d.answer(5000).reason == Reason::NoQuestionDue);  // off schedule
  d.step(14);
  CHECK(d.answer(9000).accepted);   // step 15
  d.step(15);
  CHECK(d.answer(0, true).accepted);  // step 30: skipped
  d.step(15);
  CHECK(d.answer(9000).accepted);   // step 45
  d.step(14);                        // to the last bar (59)
  const ScoreReport r = score_run(sc, d.state());
  CHECK(r.resolved_answers == 3);    // skipped one not scored
  CHECK(r.brier_bp == 100);          // (0.9 - 1)^2 = 0.01
  CHECK(r.calibration.score == 98);  // 100 * (1 - 2 * 0.01)
}

TEST_CASE("Plans and answers survive checkpoints and are part of the state digest") {
  const Scenario sc = flat(20);
  Driver a(sc);
  Driver b(sc);
  a.plan(OrderSide::Buy, "95", "50");
  CHECK(to_hex(state_digest(a.state())) != to_hex(state_digest(b.state())));
}

TEST_CASE("Regression: an options trade is an entry and a debrief trade settled at expiry") {
  // Before: scoring only looked at perpetual fills, so buying a put read "you did not trade".
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "91", "92"}, {"92", "92", "92", "92"}});
  add_options(sc, 2, {"100"});
  Driver d(sc);
  d.option(2, OrderSide::Buy, "1");  // series 2: the 100 put
  d.step(2);
  const ScoreReport r = score_run(sc, d.state());
  CHECK(r.entries == 1);
  CHECK_FALSE(has(r, FindingCode::Waited));
  CHECK_FALSE(has(r, FindingCode::NoStop));  // a bought option's premium is its maximum loss
  REQUIRE(r.trades.size() == 1);
  const TradeRow& t = r.trades[0];
  CHECK(t.series == 2);
  CHECK_FALSE(t.is_call);
  CHECK(t.strike == P("100"));
  CHECK(t.expiry_step == 2);
  CHECK(t.reason == ExitReason::Expiry);
  CHECK(t.exit_avg == P("8"));  // settlement value per contract (intrinsic 100 - 92)
  CHECK(t.pnl == Money::from_raw((P("8").raw - t.entry_avg.raw)));
  CHECK_FALSE(t.plan_respected.has_value());
}

TEST_CASE("Regression: a skipped plan gives a trade no planned risk and no R multiple") {
  // Before: the client's risk estimate stored with a skipped plan was used as the trade's 1R.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "102"}, {"102", "102", "102", "102"}});
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "50", true).accepted);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step(2);
  d.close();
  d.step();
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].planned_risk.raw == 0);
  CHECK_FALSE(r.trades[0].r_bp.has_value());
}

TEST_CASE("Regression: a flip splits its fee between the closed and the opened trade") {
  // Before: selling 20 against a long of 10 charged the whole fee to the long and none to the short.
  Scenario sc = flat(8);
  sc.contract.taker_fee = R("0.001");
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step(2);
  d.order(OrderType::Market, OrderSide::Sell, "20");
  d.step(2);
  d.close();
  d.step();
  std::vector<Money> fee;
  for (const Event& e : d.state().events) {
    if (e.type == EventType::Fill) fee.push_back(e.amount);
  }
  REQUIRE(fee.size() == 3);
  REQUIRE(fee[1].raw > 0);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 2);
  CHECK(r.trades[0].fees.raw == fee[0].raw + fee[1].raw / 2);
  CHECK(r.trades[1].fees.raw == fee[1].raw / 2 + fee[2].raw);
}

TEST_CASE("Regression: trade net includes the funding paid while it was open") {
  // Before: funding was left out, so the trades did not add up to the mission result.
  Scenario sc = flat(12);
  sc.contract.taker_fee = R("0.001");
  sc.contract.funding_interval = 2;
  sc.funding_rate.assign(sc.bars.size(), R("0.001"));
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step(6);
  d.order(OrderType::Market, OrderSide::Sell, "20");
  d.step(4);
  d.close();
  d.step();
  const RunState& st = d.state();
  REQUIRE(st.funding_net.raw != 0);
  const ScoreReport r = score_run(sc, st);
  REQUIRE(r.trades.size() == 2);
  CHECK(r.trades[0].funding.raw < 0);  // the long paid
  CHECK(r.trades[1].funding.raw > 0);  // the short received
  CHECK(r.trades[0].net.raw + r.trades[1].net.raw == r.net_pnl.raw);
}

TEST_CASE("Regression: always answering 50% is neutral calibration, not a bonus") {
  // Before: 100 * (1 - Brier) gave 75 points for answers that carry no information.
  const Scenario sc = flat(80);
  Driver d(sc);
  std::uint32_t answered = 0;
  while (d.state().status == RunStatus::Active) {
    if (d.answer(5000).accepted) answered += 1;
    d.step();
  }
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(answered >= 3);
  REQUIRE(r.resolved_answers >= 3);
  CHECK(r.brier_bp == 2500);
  CHECK(r.calibration.score == 50);
}

TEST_CASE("Regression: a written option bought back by a margin call exits as margin_call, not manual") {
  // Before: the forced buy-back has no order, so the debrief read it as a manual close.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"60", "60", "50", "50"}, {"50", "50", "50", "50"}, {"50", "50", "50", "50"}});
  add_options(sc, 4, {"100"}, true);
  Driver d(sc);
  REQUIRE(d.option(2, OrderSide::Sell, "200").accepted);  // write the 100 put
  d.step(3);
  bool called = false;
  for (const Event& e : d.state().events) called |= e.type == EventType::MarginCall;
  REQUIRE(called);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE_FALSE(r.trades.empty());
  CHECK(r.trades[0].reason == ExitReason::MarginCall);
}
