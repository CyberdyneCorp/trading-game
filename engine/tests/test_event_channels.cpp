// Event channels of generator v5 (spec market-generator "Events": jumps, spread, capacity and
// funding) and their execution side (Scenario::liquidity, costs_at).

#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>

#include "scenario_builder.hpp"
#include "tradelab/generator.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

Scenario gen(const GeneratorSpec& spec, std::uint64_t seed) {
  Scenario s;
  s.contract.tick_size = Price::from_raw(Price::kScale / 100);
  generate_market(spec, seed, s);
  return s;
}

double px(Price p) { return static_cast<double>(p.raw) / Price::kScale; }

// A 120-bar range with one credible positive surprise released at bar 60, reacting for 8 bars.
GeneratorSpec one_event(const ScriptedEvent& channels) {
  GeneratorSpec spec;
  spec.segments = {{.bars = 120, .vol = 0.005, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02}};
  ScriptedEvent ev = channels;
  ev.item = {60, NewsKind::LiquidityInflow, 1, 100, 0, 1};
  ev.duration = 8;
  spec.events = {ev};
  return spec;
}

bool same_bars(const Scenario& a, const Scenario& b, std::size_t upto) {
  for (std::size_t i = 0; i < upto; ++i) {
    const Bar& x = a.bars[i];
    const Bar& y = b.bars[i];
    if (x.open != y.open || x.high != y.high || x.low != y.low || x.close != y.close || x.volume != y.volume) return false;
  }
  return true;
}

}  // namespace

TEST_CASE("Event channels: undeclared channels leave the market and liquidity untouched") {
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    const Scenario plain = gen(one_event({}), seed);
    CHECK(plain.liquidity.empty());
    ScriptedEvent zero;
    zero.jump_prob = 0.0;
    zero.jump_size = 0.05;  // no probability: never draws
    const Scenario z = gen(one_event(zero), seed);
    CHECK(same_bars(plain, z, plain.bars.size()));
    CHECK(plain.funding_rate == z.funding_rate);
  }
}

TEST_CASE("Event channels: spread and capacity are execution-only shocks over the reaction bars") {
  ScriptedEvent ch;
  ch.spread_mult = 3.0;
  ch.capacity_mult = 0.25;
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    const Scenario plain = gen(one_event({}), seed);
    const Scenario shocked = gen(one_event(ch), seed);
    CHECK(same_bars(plain, shocked, plain.bars.size()));  // depth and spread never move prices
    CHECK(plain.funding_rate == shocked.funding_rate);
    REQUIRE(shocked.liquidity.size() == 1);
    const LiquidityShock& l = shocked.liquidity.front();
    CHECK(l.start == 61);  // from the bar after publication
    CHECK(l.bars == 8);
    CHECK(l.spread_pct == 300);
    CHECK(l.capacity_pct == 25);
  }
}

TEST_CASE("Event channels: the funding shift is published with the headline, never before it") {
  ScriptedEvent ch;
  ch.funding_shift = 0.0002;  // below squeeze_funding: the bars stay identical
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    const Scenario plain = gen(one_event({}), seed);
    const Scenario shifted = gen(one_event(ch), seed);
    CHECK(same_bars(plain, shifted, plain.bars.size()));
    for (std::size_t i = 0; i < plain.bars.size(); ++i) {
      const std::int64_t diff = shifted.funding_rate[i].raw - plain.funding_rate[i].raw;
      // The rate for bar i is published at the close of bar i-1: bars 61..68 react, so the first
      // shifted rate is the one published at the release close (bar 60).
      if (i >= 61 && i <= 68) {
        CHECK(std::llabs(diff - 2'000'000) <= 10'000);  // 0.02% within one funding print (1e-6)
      } else {
        CHECK(diff == 0);
      }
    }
  }
}

TEST_CASE("Event channels: jumps follow the sampled reaction and stay inside the reaction window") {
  ScriptedEvent ch;
  ch.p_none = 0.2;
  ch.p_align = 0.6;
  ch.jump_prob = 0.5;
  ch.jump_size = 0.03;
  double along = 0.0;
  double plain_abs = 0.0;
  double jump_abs = 0.0;
  for (std::uint64_t seed = 1; seed <= 400; ++seed) {
    const GeneratorSpec with = one_event(ch);
    const Scenario plain = gen(one_event({}), seed);
    const Scenario jumpy = gen(with, seed);
    REQUIRE(same_bars(plain, jumpy, 61));  // nothing before the reaction starts
    const int reaction = sample_reaction(with.events[0], seed, 0);
    const int dir = reaction != 0 ? reaction : 1;  // none: along the (positive) surprise
    for (std::size_t i = 61; i <= 68; ++i) {
      const double r_plain = std::log(px(plain.bars[i].close) / px(plain.bars[i - 1].close));
      const double r_jump = std::log(px(jumpy.bars[i].close) / px(jumpy.bars[i - 1].close));
      plain_abs += std::fabs(r_plain);
      jump_abs += std::fabs(r_jump);
      along += dir * (r_jump - r_plain);
    }
  }
  CHECK(jump_abs > 1.5 * plain_abs);  // about half the bars jump by 1.5%-4.5%
  CHECK(along > 0.0);
}

TEST_CASE("Event channels: execution reads the costs of the bar it fills on") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "101", "99", "100"},
                               {"100", "101", "99", "100"},
                               {"100", "101", "99", "100"},
                               {"100", "101", "99", "100"}});
  sc.costs.half_spread = R("0.001");
  sc.costs.slippage = R("0.001");
  sc.costs.capacity_fraction = R("0.000001");  // 1 contract per bar of 1,000,000
  sc.liquidity = {{2, 2, 300, 50}};             // bars 2-3: three times the spread, half the depth
  REQUIRE(validate(sc) == ScenarioError::Ok);
  CHECK(costs_at(sc, 1).half_spread == R("0.001"));
  CHECK(costs_at(sc, 2).half_spread == R("0.003"));
  CHECK(costs_at(sc, 3).slippage == R("0.003"));
  CHECK(costs_at(sc, 2).capacity_fraction == R("0.0000005"));
  CHECK(costs_at(sc, 4).capacity_fraction == R("0.000001"));

  Driver d(sc);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "0.5").accepted);
  d.step();  // bar 1: no shock, 0.2% against the player
  const Event* first = last_of(d.state(), EventType::Fill);
  REQUIRE(first != nullptr);
  CHECK(first->price == P("100.2"));
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "1").accepted);
  d.step();  // bar 2: 0.6% and only 0.5 contracts of depth
  const Event* second = last_of(d.state(), EventType::Fill);
  REQUIRE(second != nullptr);
  CHECK(second->price == P("100.6"));
  CHECK(second->qty == Q("0.5"));
}

TEST_CASE("Event channels: a liquidity shock outside the market is an invalid scenario") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "101", "99", "100"}, {"100", "101", "99", "100"}});
  sc.liquidity = {{1, 5, 200, 100}};
  CHECK(validate(sc) == ScenarioError::InvalidLiquidity);
  sc.liquidity = {{0, 1, 200, 100}};  // bar 0 is never executed on
  CHECK(validate(sc) == ScenarioError::InvalidLiquidity);
  sc.liquidity = {{1, 1, 5, 100}};  // a spread below 10% of the model
  CHECK(validate(sc) == ScenarioError::InvalidLiquidity);
  sc.liquidity = {{1, 2, 1000, 0}};
  CHECK(validate(sc) == ScenarioError::Ok);
}
