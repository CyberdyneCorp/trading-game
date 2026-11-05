// Report-only debrief facts (engine/src/debrief.cpp): spread/slippage cost, option P&L by Greek,
// the leverage that would have kept a stop in front of liquidation, theta/IV findings.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>

#include "scenario_builder.hpp"
#include "tradelab/options.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

const Finding* find(const ScoreReport& r, FindingCode c) {
  const auto it = std::find_if(r.findings.begin(), r.findings.end(), [c](const Finding& f) { return f.code == c; });
  return it == r.findings.end() ? nullptr : &*it;
}

Scenario flat(std::size_t n, const char* px = "100") {
  Scenario s = make_scenario({{px, px, px, px}});
  const Bar bar = s.bars[0];  // a copy: assign() from one of its own elements is undefined
  s.bars.assign(n, bar);
  s.funding_rate.assign(n, Rate{});
  return s;
}

// Closes rising by `step_up` per bar from 100 (open = previous close).
Scenario rising(std::size_t n, std::int64_t step_up) {
  Scenario s = flat(n);
  for (std::size_t i = 1; i < n; ++i) {
    const Price o = s.bars[i - 1].close;
    const Price c = Price::from_raw(o.raw + step_up * Price::kScale);
    s.bars[i] = {o, c, o, c, Q("1000000"), false};
  }
  return s;
}

std::int64_t greek_sum(const GreekAttribution& g) { return g.delta.raw + g.gamma.raw + g.theta.raw + g.vega.raw + g.residual.raw; }

}  // namespace

TEST_CASE("Spread and slippage: taker fills pay half-spread + slippage, maker fills pay none") {
  Scenario sc = flat(8);
  sc.costs.half_spread = R("0.001");
  sc.costs.slippage = R("0.0005");
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");  // fills at 100.15
  d.step(2);
  d.close();  // sells at 99.85
  d.step();
  ScoreReport r = score_run(sc, d.state());
  CHECK(r.spread_cost == M("3"));            // 10 x 0.15 twice
  CHECK(r.net_pnl.raw == -r.spread_cost.raw);  // flat market, no fees: the spread is the whole loss

  d.order(OrderType::Limit, OrderSide::Buy, "10", "100");  // maker: rests, no spread
  sc.bars[4] = {P("100"), P("100"), P("99"), P("100"), Q("1000000"), false};
  d.step(2);
  REQUIRE_FALSE(d.state().position.flat());
  r = score_run(sc, d.state());
  CHECK(r.spread_cost == M("3"));
}

TEST_CASE("Spread and slippage: a stop pays it from its trigger, a gapped stop from the open") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "94", "95"},
                               {"95", "95", "95", "95"}});
  sc.costs.slippage = R("0.01");
  Driver d(sc);
  d.bracket(OrderType::Market, OrderSide::Buy, "1", nullptr, "96", nullptr);  // buy at 101
  d.step(2);                                                                  // stop at 96 -> 95.04
  REQUIRE(d.state().position.flat());
  const ScoreReport r = score_run(sc, d.state());
  CHECK(r.spread_cost == M("1.96"));  // 1 (entry) + 0.96 (stop)
}

TEST_CASE("Option spread is the distance from the mid of the fill quote") {
  Scenario sc = flat(6);
  add_options(sc, 5, {"100"});
  Driver d(sc);
  d.option(1, OrderSide::Buy, "2");
  d.step();
  const OptionQuote q = quote(sc, sc.options.series[0], 0, sc.bars[1].open);
  const ScoreReport r = score_run(sc, d.state());
  CHECK(r.spread_cost == Money::from_raw(2 * (q.ask.raw - q.mid.raw)));
}

TEST_CASE("Greek attribution: absent without option trades") {
  const Scenario sc = flat(5);
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step(4);
  CHECK_FALSE(score_run(sc, d.state()).greeks.has_value());
}

TEST_CASE("Greek attribution: a call held to expiry in a flat market is theta, recommends Options") {
  Scenario sc = flat(12);
  add_options(sc, 10, {"100"});
  Driver d(sc);
  d.plan(OrderSide::Buy, nullptr, "10", false, true);
  d.option(1, OrderSide::Buy, "1");
  d.step(11);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.greeks.has_value());
  const GreekAttribution& g = *r.greeks;
  CHECK(g.total.raw < 0);  // expired worthless: the premium is lost
  CHECK(g.total == r.trades[0].pnl);
  CHECK(g.delta.raw == 0);
  CHECK(g.gamma.raw == 0);
  CHECK(g.vega.raw == 0);
  CHECK(-2 * g.theta.raw >= -g.total.raw);  // decay explains most of the loss
  CHECK(greek_sum(g) == g.total.raw);
  const Finding* f = find(r, FindingCode::ThetaDecay);
  REQUIRE(f != nullptr);
  CHECK(f->amount.raw == -g.theta.raw);
  CHECK(find(r, FindingCode::IvCrush) == nullptr);
  CHECK(r.lesson == LessonCode::Options);
}

TEST_CASE("Greek attribution: a call in a rising market is mostly delta and sums to the option P&L") {
  Scenario sc = rising(12, 1);
  add_options(sc, 10, {"100"});
  Driver d(sc);
  d.option(1, OrderSide::Buy, "1");
  d.step(4);
  d.option(1, OrderSide::Sell, "1");  // closed before expiry
  d.step(7);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.greeks.has_value());
  const GreekAttribution& g = *r.greeks;
  CHECK(g.total.raw > 0);
  CHECK(g.delta.raw > 0);
  CHECK(g.gamma.raw > 0);  // long options are long gamma
  CHECK(g.theta.raw < 0);
  CHECK(g.delta.raw > g.total.raw / 2);
  CHECK(greek_sum(g) == g.total.raw);
  CHECK(find(r, FindingCode::ThetaDecay) == nullptr);  // a winning trade has no theta finding
}

TEST_CASE("Greek attribution: a falling IV shows up as vega and as an IV finding") {
  Scenario sc = flat(12);
  add_options(sc, 11, {"100"});
  for (std::size_t i = 2; i < sc.bars.size(); ++i) sc.options.iv_base[i] = parse_decimal<Vol>("0.2").value();
  Driver d(sc);
  d.option(1, OrderSide::Buy, "1");
  d.step(2);
  d.option(1, OrderSide::Sell, "1");
  d.step();
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.greeks.has_value());
  CHECK(r.greeks->vega.raw < 0);
  CHECK(greek_sum(*r.greeks) == r.greeks->total.raw);
  CHECK(find(r, FindingCode::IvCrush) != nullptr);
  CHECK(r.lesson == LessonCode::Options);
}

TEST_CASE("Stop beyond liquidation: the debrief names the leverage that would have kept it in front") {
  // Long 10 at 100, 20x, maintenance 1%: liquidation ~95.96, the bracket stop at 95 sits beyond it.
  // liq(L) = 100 (1 - 1/L) / 0.99 < 95 needs L < 16.8: 16x would have kept the stop in front.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "90", "92"},
                               {"92", "92", "92", "92"}});
  Driver d(sc);
  d.leverage(20);
  d.plan(OrderSide::Buy, "95", "50");
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", nullptr);
  d.step(3);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  const TradeRow& t = r.trades[0];
  CHECK(t.reason == ExitReason::Liquidation);
  CHECK(t.leverage == 20);
  CHECK(t.stop == P("95"));
  CHECK(t.liq_price > P("95"));
  CHECK(t.stop_beyond_liq);
  CHECK(t.safe_leverage == 16);
  const Finding* liq = find(r, FindingCode::Liquidated);
  REQUIRE(liq != nullptr);
  CHECK(liq->count == 16);
  const Finding* beyond = find(r, FindingCode::StopBeyondLiq);
  REQUIRE(beyond != nullptr);
  CHECK(beyond->count == 16);
  CHECK(beyond->price == P("95"));
  CHECK(beyond->requested == t.liq_price);
  CHECK(r.lesson == LessonCode::Futures2);
}

TEST_CASE("Stop in front of liquidation: no safe-leverage advice") {
  Scenario sc = flat(5);
  Driver d(sc);
  d.leverage(5);
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", nullptr);
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].stop == P("95"));
  CHECK(r.trades[0].leverage == 5);
  CHECK_FALSE(r.trades[0].stop_beyond_liq);
  CHECK(r.trades[0].safe_leverage == 0);
  CHECK(find(r, FindingCode::StopBeyondLiq) == nullptr);
}

TEST_CASE("A short with a planned invalidation beyond liquidation, no stop order") {
  // Short 10 at 100, 20x: liquidation ~103.96; invalidation 105 is beyond it. 16x keeps it in front.
  Scenario sc = flat(5);
  Driver d(sc);
  d.leverage(20);
  d.plan(OrderSide::Sell, "105", "50");
  d.order(OrderType::Market, OrderSide::Sell, "10");
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].stop == P("105"));
  CHECK(r.trades[0].stop_beyond_liq);
  CHECK(r.trades[0].safe_leverage == 16);  // liq(L) = 100 (1 + 1/L) / 1.01 > 105 needs L < 16.5
}

TEST_CASE("Debrief facts are report-only: scoring never changes the run state digest") {
  Scenario sc = flat(12);
  add_options(sc, 10, {"100"});
  Driver d(sc);
  d.option(1, OrderSide::Buy, "1");
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step(11);
  const std::string before = to_hex(state_digest(d.state()));
  (void)score_run(sc, d.state());
  CHECK(to_hex(state_digest(d.state())) == before);
}

TEST_CASE("Regression: re-leveraging an open position past its stop is judged at the leverage it carried") {
  // Long 10 at 100 with a stop at 90, entered at 2x (liquidation ~50.5), then re-leveraged to 20x
  // (liquidation ~95.96). Before: the debrief used the entry leverage and gave no stop-beyond-liquidation
  // advice although the position was liquidated in front of its stop.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "90", "92"}, {"92", "92", "92", "92"}});
  Driver d(sc);
  d.leverage(2);
  d.plan(OrderSide::Buy, "90", "100");
  REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "90", nullptr).accepted);
  d.step(2);
  REQUIRE(d.leverage(20).accepted);
  d.step(2);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  const TradeRow& t = r.trades[0];
  CHECK(t.reason == ExitReason::Liquidation);
  CHECK(t.leverage == 20);
  CHECK(t.liq_price > P("95"));
  CHECK(t.stop_beyond_liq);
  CHECK(t.safe_leverage == 9);  // liq(L) = 100 (1 - 1/L) / 0.99 < 90 needs L < 9.2
  const Finding* beyond = find(r, FindingCode::StopBeyondLiq);
  REQUIRE(beyond != nullptr);
  CHECK(beyond->count == 9);
  const Finding* liq = find(r, FindingCode::Liquidated);
  REQUIRE(liq != nullptr);
  CHECK(liq->count == 9);
}

TEST_CASE("A leverage change at the exit bar belongs to the next trade") {
  Scenario sc = flat(6);
  Driver d(sc);
  d.leverage(2);
  d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "90", nullptr);
  d.step(2);
  d.close();
  d.step(1);
  REQUIRE(d.state().position.flat());
  d.leverage(20);  // same bar as the exit fill
  d.step(1);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].leverage == 2);
  CHECK_FALSE(r.trades[0].stop_beyond_liq);
}

TEST_CASE("Regression: an open trade's net includes its unrealized P&L, like the headline result") {
  // Before: an open long in a rising market showed net = -fees + funding (pnl 0), a loss next to a
  // positive headline.
  Scenario sc = rising(6, 1);
  sc.contract.taker_fee = R("0.001");
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step(5);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  const TradeRow& t = r.trades[0];
  CHECK(t.reason == ExitReason::Open);
  CHECK(t.pnl == M("50"));  // 10 x (105 - 100)
  CHECK(t.net == Money::from_raw(t.pnl.raw - t.fees.raw));
  CHECK(t.net == r.net_pnl);
}

TEST_CASE("An open option trade's net is valued like the account equity") {
  Scenario sc = rising(8, 1);
  add_options(sc, 7, {"100"});
  Driver d(sc);
  REQUIRE(d.option(1, OrderSide::Buy, "2").accepted);
  d.step(4);
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.trades.size() == 1);
  CHECK(r.trades[0].reason == ExitReason::Open);
  CHECK(r.trades[0].pnl.raw > 0);
  CHECK(std::abs(r.trades[0].net.raw - r.net_pnl.raw) <= 1);  // only rounding differs
}

TEST_CASE("Regression: a written put hit by a gap crash is a delta and gamma loss, not residual") {
  // Before: the Greeks of the start quote charged most of a gap to the residual ("spread and others")
  // and could show the short put's crash as a delta gain. Full revaluation puts the move in delta and
  // gamma with its sign; the residual is only the spread paid at the fills.
  Scenario sc = flat(10);
  sc.bars[4] = {P("70"), P("70"), P("70"), P("70"), Q("1000000"), false};
  for (std::size_t i = 5; i < sc.bars.size(); ++i) sc.bars[i] = sc.bars[4];
  add_options(sc, 9, {"100"}, true);
  Driver d(sc);
  REQUIRE(d.option(2, OrderSide::Sell, "1").accepted);  // put 100
  d.step(5);
  REQUIRE(d.option(2, OrderSide::Buy, "1").accepted);  // bought back after the crash
  d.step();
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.greeks.has_value());
  const GreekAttribution& g = *r.greeks;
  CHECK(g.total.raw < 0);
  CHECK(g.delta.raw < 0);
  CHECK(g.gamma.raw < 0);  // a writer is short gamma
  CHECK(-10 * (g.delta.raw + g.gamma.raw) >= -9 * g.total.raw);  // the move explains the loss
  CHECK(std::llabs(g.residual.raw) <= std::llabs(r.spread_cost.raw) + 10);
  CHECK(greek_sum(g) == g.total.raw);
}

TEST_CASE("Regression: a written option loses to a rising IV: iv_rise, not iv_crush") {
  // Before: the vega loss of a writer was described as an IV crush and "buying with high IV".
  Scenario sc = flat(12);
  add_options(sc, 11, {"100"}, true);
  for (std::size_t i = 2; i < sc.bars.size(); ++i) sc.options.iv_base[i] = parse_decimal<Vol>("0.9").value();
  Driver d(sc);
  REQUIRE(d.option(1, OrderSide::Sell, "1").accepted);
  d.step(2);
  REQUIRE(d.option(1, OrderSide::Buy, "1").accepted);
  d.step();
  const ScoreReport r = score_run(sc, d.state());
  REQUIRE(r.greeks.has_value());
  CHECK(r.greeks->vega.raw < 0);
  CHECK(r.greeks->vega_written == r.greeks->vega);
  CHECK(greek_sum(*r.greeks) == r.greeks->total.raw);
  const Finding* f = find(r, FindingCode::IvRise);
  REQUIRE(f != nullptr);
  CHECK(f->amount.raw == -r.greeks->vega.raw);
  CHECK(find(r, FindingCode::IvCrush) == nullptr);
  CHECK(find(r, FindingCode::ThetaDecay) == nullptr);  // a writer earns theta
  CHECK(r.lesson == LessonCode::Options);
}

TEST_CASE("Regression: the buy-and-hold benchmark stops at the run's last bar (game over)") {
  // Before: a run that ended early was compared with prices after it ended.
  Scenario sc = rising(10, 1);
  Driver d(sc);
  d.step(4);
  CHECK(buy_and_hold_pnl(sc, d.state()) == M("399.60"));  // 99.9 x (104 - 100), no fees
  d.step(5);
  CHECK(buy_and_hold_pnl(sc, d.state()) == M("899.10"));  // to the final close, 109
  CHECK(buy_and_hold_pnl(sc, start_run(sc, 1)).raw == 0);  // nothing played yet
}

TEST_CASE("Regression: an exit is named by its order type, not by its side of the entry (scoring v10)") {
  // Before: any exit whose requested price was beyond the entry was a "target", so a trailing stop
  // or a stop moved into profit read as a take-profit in the debrief.
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "110", "100", "108"},
                               {"108", "108", "104", "105"},
                               {"105", "105", "105", "105"}});
  Driver trail(sc);
  trail.order(OrderType::Market, OrderSide::Buy, "1");
  trail.step();
  REQUIRE(trail.trailing(OrderSide::Sell, "1", "3").accepted);
  trail.step(2);  // the trail moves to 105 at the close of 108; bar 3 trades down to 104
  REQUIRE(trail.state().position.flat());
  const ScoreReport tr = score_run(sc, trail.state());
  REQUIRE(tr.trades.size() == 1);
  CHECK(tr.trades[0].pnl.raw > 0);
  CHECK(tr.trades[0].reason == ExitReason::Trailing);

  Driver stop(sc);
  stop.order(OrderType::Market, OrderSide::Buy, "1");
  stop.step(2);
  REQUIRE(stop.order(OrderType::StopMarket, OrderSide::Sell, "1", "106", true).accepted);  // above the entry
  stop.step();
  REQUIRE(stop.state().position.flat());
  const ScoreReport sr = score_run(sc, stop.state());
  REQUIRE(sr.trades.size() == 1);
  CHECK(sr.trades[0].pnl.raw > 0);
  CHECK(sr.trades[0].reason == ExitReason::Stop);

  Driver target(sc);
  target.bracket(OrderType::Market, OrderSide::Buy, "1", nullptr, nullptr, "106");
  target.step(2);
  REQUIRE(target.state().position.flat());
  const ScoreReport gr = score_run(sc, target.state());
  REQUIRE(gr.trades.size() == 1);
  CHECK(gr.trades[0].reason == ExitReason::Target);
}
