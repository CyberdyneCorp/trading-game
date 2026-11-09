#include <doctest/doctest.h>

#include <algorithm>

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

// Regression (playtest): a stop at the plan's invalidation that a gap filled 3% lower was listed as
// "plan violated", next to a plan-coherence score of 100.
TEST_CASE("Scoring v12: a stop at the invalidation filled through a gap keeps the plan") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}, {"92", "93", "91", "92"}, {"92", "92", "92", "92"}});
  Driver d(sc);
  d.plan(OrderSide::Buy, "95", "50");
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", nullptr);
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].reason == ExitReason::Stop);
  CHECK(r.trades[0].exit_avg.raw < Price::from_raw(9'400'000'000).raw);  // filled at the gap, beyond 1%
  CHECK(r.trades[0].plan_respected == true);

  // A stop placed beyond the invalidation is still a broken plan.
  Driver wide(sc);
  wide.plan(OrderSide::Buy, "97", "50");
  wide.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", nullptr);
  wide.step(4);
  const ScoreReport w = score_run(sc, wide.state());
  REQUIRE(w.trades.size() == 1);
  CHECK(w.trades[0].plan_respected == false);
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

TEST_CASE("Regression: a plan whose order was rejected is not an oversized entry") {
  // Before: an option order for 100,000 contracts was refused (not enough balance), but its plan
  // stayed and the debrief warned that "an entry risked ~126000 cr" (-20 risk) for a trade that never
  // happened. The retry with a small size is the entry that counts.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  add_options(sc, 3, {"100"}, false);
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "126000", true, true).accepted);  // sent without a plan
  CHECK_FALSE(d.option(1, OrderSide::Buy, "100000").accepted);
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "150", false, true).accepted);
  REQUIRE(d.option(1, OrderSide::Buy, "10").accepted);
  d.step(3);
  const ScoreReport r = score_run(sc, d.state());
  CHECK_FALSE(has(r, FindingCode::Oversized));
  CHECK(r.risk.score == 100);
}

TEST_CASE("An accepted oversized entry is still flagged, also when sent without a plan") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "900", true).accepted);  // skipped plan keeps the ticket's estimate
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step(2);
  const ScoreReport r = score_run(sc, d.state());
  CHECK(has(r, FindingCode::Oversized));
}

TEST_CASE("Regression: an option margin call costs like a liquidation and is never 'good process'") {
  // Before: writing 200 puts with a plan, margin-called for a large loss, kept risk at 100 and the
  // debrief said "a loss with good process ... that is risk management working".
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"60", "60", "50", "50"}, {"50", "50", "50", "50"}, {"50", "50", "50", "50"}});
  add_options(sc, 4, {"100"}, true);
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Sell, nullptr, "150", false, true).accepted);
  REQUIRE(d.option(2, OrderSide::Sell, "200").accepted);
  d.step(3);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.net_pnl.raw < 0);
  CHECK(r.risk.score <= 60);
  CHECK_FALSE(has(r, FindingCode::GoodProcessLoss));
  CHECK(has(r, FindingCode::MarginCalled));
  CHECK(r.lesson == LessonCode::Risk);
}

TEST_CASE("Regression: a loss is not 'good process' when one of the trades had no plan") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "94", "95"},
                               {"95", "95", "95", "95"}, {"95", "100", "95", "100"}, {"100", "100", "90", "90"}, {"90", "90", "90", "90"}});
  Driver d(sc);
  d.plan(OrderSide::Buy, "95", "50");
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step(4);
  REQUIRE(d.state().position.flat());
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "91", "120");  // no plan this time
  d.step(2);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 2);
  REQUIRE(r.net_pnl.raw < 0);
  CHECK_FALSE(has(r, FindingCode::GoodProcessLoss));
}

TEST_CASE("Regression: a planned naked option write that loses is not 'limited risk' good process") {
  // Before: writing puts with a plan and holding to a loss (no margin call) said "every trade had a
  // plan and limited risk (stop or premium)", although a writer's loss is not limited by the premium.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "95", "95"},
                               {"95", "95", "92", "92"}, {"92", "92", "92", "92"}});
  add_options(sc, 4, {"100"}, true);
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Sell, nullptr, "100", false, true).accepted);
  REQUIRE(d.option(2, OrderSide::Sell, "5").accepted);
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.net_pnl.raw < 0);
  REQUIRE_FALSE(has(r, FindingCode::MarginCalled));
  CHECK_FALSE(has(r, FindingCode::GoodProcessLoss));
}

TEST_CASE("Regression: a planned, protected win is credited as good process") {
  // Before: a profitable run with full process had no finding at all ("What happened" was empty).
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}, {"100", "111", "100", "110"}, {"110", "110", "110", "110"}});
  Driver d(sc);
  d.plan(OrderSide::Buy, "95", "50");
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.net_pnl.raw > 0);
  CHECK(has(r, FindingCode::GoodProcessWin));
  CHECK_FALSE(has(r, FindingCode::LuckyWin));
  CHECK_FALSE(has(r, FindingCode::GoodProcessLoss));
}

TEST_CASE("A bought option with a plan that wins is good process too") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "106", "100", "106"},
                               {"106", "112", "106", "112"}, {"112", "112", "112", "112"}});
  add_options(sc, 4, {"100"});
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "30", false, true).accepted);
  REQUIRE(d.option(1, OrderSide::Buy, "2").accepted);
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.net_pnl.raw > 0);
  CHECK(has(r, FindingCode::GoodProcessWin));
}

TEST_CASE("Regression: a plan recorded on an entry's fill bar belongs to the next order, not that trade") {
  // Before: the trade took the last plan recorded up to its entry fill step. Flipping on the bar
  // the long filled gave the long the short's plan ("violated" against 110) and left the short with
  // no plan, no R and no advisor points.
  Scenario sc = flat(8);
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, "95", "50").accepted);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);  // fills at step 1
  d.step();
  REQUIRE(d.plan(OrderSide::Sell, "110", "60").accepted);  // recorded at step 1, the long's fill bar
  REQUIRE(d.order(OrderType::Market, OrderSide::Sell, "20").accepted);  // flips at step 2
  d.step(2);
  d.close();
  d.step();
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 2);
  CHECK(r.trades[0].is_long);
  CHECK(r.trades[0].planned_risk == M("50"));
  CHECK(r.trades[0].plan_respected == true);
  CHECK(r.trades[0].stop == P("95"));
  CHECK_FALSE(r.trades[1].is_long);
  CHECK(r.trades[1].planned_risk == M("60"));
  CHECK(r.trades[1].r_bp.has_value());
  CHECK(r.trades[1].plan_respected == true);
}

TEST_CASE("Regression: two option legs sent on the same bar each keep their own plan") {
  // Before: the call took the put's plan (the later one), so its R used the put's small risk.
  Scenario sc = flat(6);
  add_options(sc, 5, {"100"});
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "27", false, true).accepted);
  REQUIRE(d.option(1, OrderSide::Buy, "1").accepted);  // call
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "5.1", false, true).accepted);
  REQUIRE(d.option(2, OrderSide::Buy, "1").accepted);  // put
  d.step(5);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 2);
  for (const TradeRow& t : r.trades) CHECK(t.planned_risk == (t.series == 1 ? M("27") : M("5.1")));
}

TEST_CASE("Regression: an add's plan adds its risk to the trade instead of replacing the first plan") {
  // Before: adding on the first fill's bar replaced the trade's plan, so R used only the add's risk.
  Scenario sc = flat(6);
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, "95", "50").accepted);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step();
  REQUIRE(d.plan(OrderSide::Buy, "90", "30").accepted);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step(2);
  d.close();
  d.step();
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].planned_risk == M("80"));
  CHECK(r.trades[0].stop == P("95"));  // the opening plan's invalidation
}

TEST_CASE("Regression: a margin-call shortfall beyond the balance is reported, so the debrief adds up") {
  // Before: the wallet was floored at 0 and the shortfall vanished: the result was -10,000 while
  // the option P&L and the trade rows were thousands of credits worse, with no line between them.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"30", "30", "20", "20"}, {"20", "20", "20", "20"}, {"20", "20", "20", "20"}});
  add_options(sc, 4, {"100"}, true);
  Driver d(sc);
  REQUIRE(d.option(2, OrderSide::Sell, "300").accepted);  // write the 100 put
  d.step(3);
  const RunState& st = d.state();
  REQUIRE(st.insurance_absorbed.raw > 0);
  const ScoreReport r = score_run(sc, st);
  std::int64_t nets = 0;
  for (const TradeRow& t : r.trades) nets += t.net.raw;
  REQUIRE(nets < r.net_pnl.raw);
  CHECK(r.written_off.raw > 0);
  CHECK(nets + r.written_off.raw == r.net_pnl.raw);
  CHECK(r.written_off == st.insurance_absorbed);
}

TEST_CASE("No shortfall is reported when the fund covered nothing") {
  Scenario sc = flat(6);
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step(5);
  CHECK(score_run(sc, d.state()).written_off.raw == 0);
}

TEST_CASE("Regression: a profitable planned naked option write is never praised as limited risk") {
  // Before: no good-process credit, but no finding either: "What happened" was empty.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "104", "100", "104"},
                               {"104", "108", "104", "108"}, {"108", "108", "108", "108"}});
  add_options(sc, 4, {"100"}, true);
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Sell, nullptr, "100", false, true).accepted);
  REQUIRE(d.option(2, OrderSide::Sell, "5").accepted);  // put 100, expires worthless
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.net_pnl.raw > 0);
  CHECK_FALSE(has(r, FindingCode::GoodProcessWin));
  CHECK(has(r, FindingCode::WroteOptions));
}

namespace {
Scenario spread_scenario() {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "104", "100", "104"},
                               {"104", "108", "104", "108"}, {"108", "108", "108", "108"}, {"108", "108", "108", "108"}});
  add_options(sc, 5, {"100", "101"}, true);  // ids: 1 C100, 2 P100, 3 C101, 4 P101 (both with a bid)
  return sc;
}

bool filled(const RunState& st, std::uint32_t series, OrderSide side) {
  return std::any_of(st.events.begin(), st.events.end(),
                     [&](const Event& e) { return e.type == EventType::Fill && e.series == series && e.side == side; });
}
}  // namespace

TEST_CASE("Regression (v13): a bull call spread's short call is covered, not naked writing") {
  // Before: any written option raised wrote_options, so the m13 debrief told a spread player
  // "you wrote options uncovered" and withheld the good-process credit.
  const Scenario sc = spread_scenario();
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, nullptr, "100", false, true).accepted);
  REQUIRE(d.option(1, OrderSide::Buy, "5").accepted);   // long C100 first
  REQUIRE(d.option(3, OrderSide::Sell, "5").accepted);  // short C101, same expiry
  d.step(5);
  REQUIRE(filled(d.state(), 3, OrderSide::Sell));
  const ScoreReport r = score_run(sc, d.state());
  CHECK_FALSE(has(r, FindingCode::WroteOptions));
}

TEST_CASE("v13: a short call covered by a long perpetual (covered call) is not naked writing") {
  const Scenario sc = spread_scenario();
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "5");
  REQUIRE(d.option(3, OrderSide::Sell, "5").accepted);
  d.step(5);
  REQUIRE(filled(d.state(), 3, OrderSide::Sell));
  CHECK_FALSE(has(score_run(sc, d.state()), FindingCode::WroteOptions));
}

TEST_CASE("v13: closing the long that covered a short leaves it naked") {
  const Scenario sc = spread_scenario();
  Driver d(sc);
  REQUIRE(d.option(1, OrderSide::Buy, "5").accepted);
  REQUIRE(d.option(3, OrderSide::Sell, "5").accepted);
  d.step(1);
  REQUIRE(d.option(1, OrderSide::Sell, "5").accepted);  // the cover is sold, the short stays
  d.step(4);
  CHECK(has(score_run(sc, d.state()), FindingCode::WroteOptions));
}

TEST_CASE("v13: a short put is not covered by a long call, nor by more shorts than longs") {
  const Scenario sc = spread_scenario();
  Driver d(sc);
  REQUIRE(d.option(1, OrderSide::Buy, "5").accepted);
  REQUIRE(d.option(4, OrderSide::Sell, "5").accepted);  // short P101: another kind
  d.step(5);
  CHECK(has(score_run(sc, d.state()), FindingCode::WroteOptions));
  Driver e(sc);
  REQUIRE(e.option(1, OrderSide::Buy, "2").accepted);
  REQUIRE(e.option(3, OrderSide::Sell, "5").accepted);  // 3 of 5 uncovered
  e.step(5);
  CHECK(has(score_run(sc, e.state()), FindingCode::WroteOptions));
}

namespace {
// spread_scenario's chain expires at bar 5; a near chain at bar 3 adds ids 5 C100 and 6 P100.
Scenario two_expiry_scenario() {
  Scenario sc = spread_scenario();
  sc.options.series.push_back({5, OptionKind::Call, P("100"), 0, 3});
  sc.options.series.push_back({6, OptionKind::Put, P("100"), 0, 3});
  return sc;
}
}  // namespace

TEST_CASE("Regression (v14, wave 2 cross-check): a calendar's near short is covered by the later long") {
  // Before: only a long of the same expiry covered, so m21's taught calendar (and an m17 calendar)
  // read "you wrote options uncovered" and lost the good-process credit.
  const Scenario sc = two_expiry_scenario();
  Driver d(sc);
  REQUIRE(d.option(1, OrderSide::Buy, "5").accepted);   // long C100, bar 5
  REQUIRE(d.option(5, OrderSide::Sell, "5").accepted);  // short C100, bar 3
  d.step(5);
  REQUIRE(filled(d.state(), 5, OrderSide::Sell));
  CHECK_FALSE(has(score_run(sc, d.state()), FindingCode::WroteOptions));
  // The reverse (the long expires first) leaves the far short naked.
  Driver e(sc);
  REQUIRE(e.option(5, OrderSide::Buy, "5").accepted);
  REQUIRE(e.option(1, OrderSide::Sell, "5").accepted);
  e.step(5);
  CHECK(has(score_run(sc, e.state()), FindingCode::WroteOptions));
  // One far long covers one near short: a second short on the same kind is naked.
  Driver f(sc);
  REQUIRE(f.option(1, OrderSide::Buy, "5").accepted);
  REQUIRE(f.option(5, OrderSide::Sell, "5").accepted);
  REQUIRE(f.option(3, OrderSide::Sell, "5").accepted);  // short C101, bar 5
  f.step(5);
  CHECK(has(score_run(sc, f.state()), FindingCode::WroteOptions));
}

TEST_CASE("Regression (v14, wave 2 cross-check): a short put covered by a short perpetual is not naked writing") {
  // Before: m19's hedged carry (call bought, put written, perpetual shorted) read as naked writing.
  const Scenario sc = spread_scenario();
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Sell, "5");
  REQUIRE(d.option(2, OrderSide::Sell, "5").accepted);  // short P100
  d.step(5);
  REQUIRE(filled(d.state(), 2, OrderSide::Sell));
  CHECK_FALSE(has(score_run(sc, d.state()), FindingCode::WroteOptions));
  Driver e(sc);
  e.order(OrderType::Market, OrderSide::Buy, "5");  // a long perpetual covers calls, not puts
  REQUIRE(e.option(2, OrderSide::Sell, "5").accepted);
  e.step(5);
  CHECK(has(score_run(sc, e.state()), FindingCode::WroteOptions));
}

TEST_CASE("Regression: a profit where one trade had no plan says so (findings never empty)") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "106", "100", "105"},
                               {"105", "105", "105", "105"}, {"105", "111", "105", "110"}, {"110", "110", "110", "110"}});
  Driver d(sc);
  d.plan(OrderSide::Buy, "95", "50");
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "105");
  d.step(2);
  REQUIRE(d.state().position.flat());
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "100", "110");  // no plan this time
  d.step(3);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 2);
  REQUIRE(r.net_pnl.raw > 0);
  CHECK_FALSE(has(r, FindingCode::GoodProcessWin));
  CHECK(has(r, FindingCode::LuckyWin));
  CHECK_FALSE(r.findings.empty());
}
