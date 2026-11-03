#include <doctest/doctest.h>

#include "scenario_builder.hpp"
#include "tradelab/indicators.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {
Scenario closes(std::initializer_list<const char*> cs) {
  Scenario s = make_scenario({{"1", "1", "1", "1"}});
  s.bars.clear();
  for (const char* c : cs) s.bars.push_back({P(c), P(c), P(c), P(c), Q("1"), false});
  s.funding_rate.assign(s.bars.size(), Rate{});
  return s;
}
}  // namespace

TEST_CASE("SMA/EMA hand-computed values and warm-up") {
  const Scenario s = closes({"10", "11", "12", "11", "10"});
  const Series m = sma(s, 4, 3);
  CHECK_FALSE(m[1].has_value());  // warming up
  CHECK(*m[2] == P("11"));
  CHECK(*m[3] == P("11.33333333"));
  CHECK(*m[4] == P("11"));
  const Series e = ema(s, 4, 3);
  CHECK_FALSE(e[1].has_value());
  CHECK(*e[2] == P("11"));   // seeded with the SMA
  CHECK(*e[3] == P("11"));   // 11 + (11 - 11) * 0.5
  CHECK(*e[4] == P("10.5")); // 11 + (10 - 11) * 0.5
}

TEST_CASE("RSI uses Wilder smoothing and needs period + 1 bars") {
  const Scenario s = closes({"10", "11", "12", "11", "10"});
  const Series r = rsi(s, 4, 2);
  CHECK_FALSE(r[1].has_value());
  CHECK(*r[2] == P("100"));  // only gains so far
  CHECK(*r[3] == P("50"));   // gain 0.5, loss 0.5
  CHECK(*r[4] == P("25"));   // gain 0.25, loss 0.75
}

TEST_CASE("ATR is the Wilder-smoothed true range including gaps") {
  Scenario s = make_scenario({{"100", "101", "99", "100"}, {"100", "102", "98", "101"}, {"105", "106", "104", "105"}, {"105", "107", "103", "104"}});
  const Series a = atr(s, 3, 2);
  CHECK_FALSE(a[1].has_value());
  CHECK(*a[2] == P("4.5"));  // TR: bar1 = 4, bar2 = max(2, 106-101, 104-101) = 5 -> mean 4.5
  CHECK(*a[3] == P("4.25")); // TR bar3 = 4 -> (4.5 + 4) / 2
}

TEST_CASE("Indicators at t ignore bars after t") {
  const Scenario s = closes({"10", "11", "12", "11", "10", "50", "1"});
  for (auto f : {&sma, &ema, &rsi, &atr}) {
    const Series cut = f(s, 4, 2);
    const Series full = f(s, 6, 2);
    for (std::size_t i = 0; i <= 4; ++i) CHECK(cut[i] == full[i]);
  }
}
