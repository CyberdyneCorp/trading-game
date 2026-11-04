// Report-only debrief facts (engine/src/debrief.cpp): spread/slippage cost, option P&L by Greek,
// the leverage that would have kept a stop in front of liquidation, theta/IV findings.

#include <doctest/doctest.h>

#include <algorithm>

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
