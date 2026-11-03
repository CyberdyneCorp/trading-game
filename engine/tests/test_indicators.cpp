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

TEST_CASE("VWAP weights the typical price by volume and resets each session") {
  Scenario s = make_scenario({{"10", "12", "8", "10"}, {"20", "22", "18", "20"}, {"3", "6", "3", "3"}});
  s.bars[0].volume = Q("1");
  s.bars[1].volume = Q("3");
  s.bars[2].volume = Q("2");
  const Series v = vwap(s, 2, 2);
  CHECK(*v[0] == P("10"));
  CHECK(*v[1] == P("17.5"));  // (10 * 1 + 20 * 3) / 4
  CHECK(*v[2] == P("4"));     // new session: typical (6 + 3 + 3) / 3
}

TEST_CASE("MACD line, signal and histogram with their warm-ups") {
  const Scenario s = closes({"10", "11", "12", "11", "10"});
  const Macd m = macd(s, 4, 1, 2, 2);  // EMA(1) is the close itself
  CHECK_FALSE(m.line[0].has_value());
  CHECK(*m.line[1] == P("0.5"));          // 11 - 10.5
  CHECK(*m.line[2] == P("0.5"));          // 12 - 11.5
  CHECK(*m.line[3] == P("-0.16666667"));  // 11 - 11.16666667
  CHECK_FALSE(m.signal[1].has_value());   // signal warm-up: slow + signal - 1 bars
  CHECK(*m.signal[2] == P("0.5"));        // seeded with the mean of the first two lines
  CHECK(*m.histogram[2] == P("0"));
  for (std::size_t i = 2; i <= 4; ++i) CHECK(m.histogram[i]->raw == m.line[i]->raw - m.signal[i]->raw);
}

TEST_CASE("ADX: directional indicators from bar n, ADX after 2n bars") {
  const Scenario s = make_scenario({{"9", "10", "8", "9"}, {"11", "12", "9", "11"}, {"12", "13", "10", "12"},
                                    {"9", "12", "8", "9"}, {"8", "11", "7", "8"}});
  const Adx a = adx(s, 4, 2);
  CHECK_FALSE(a.plus_di[1].has_value());
  CHECK(*a.plus_di[2] == P("50"));  // +DM mean 1.5 over TR mean 3
  CHECK(*a.minus_di[2] == P("0"));
  CHECK_FALSE(a.adx[2].has_value());
  CHECK(*a.plus_di[3] == P("21.42857143"));
  CHECK(*a.minus_di[3] == P("28.57142857"));
  CHECK(*a.adx[3] == P("57.14285714"));  // mean of DX 100 and 14.28571429
  CHECK(*a.adx[4] == P("51.2987013"));   // Wilder: (57.14285714 + 45.45454545) / 2
}

TEST_CASE("Indicators at t ignore bars after t") {
  const Scenario s = closes({"10", "11", "12", "11", "10", "50", "1"});
  for (auto f : {&sma, &ema, &rsi, &atr}) {
    const Series cut = f(s, 4, 2);
    const Series full = f(s, 6, 2);
    for (std::size_t i = 0; i <= 4; ++i) CHECK(cut[i] == full[i]);
  }
}

TEST_CASE("Composite indicators at t ignore bars after t") {
  const Scenario s = make_scenario({{"9", "10", "8", "9"}, {"11", "12", "9", "11"}, {"12", "13", "10", "12"}, {"9", "12", "8", "9"},
                                    {"8", "11", "7", "8"}, {"40", "60", "30", "50"}, {"2", "3", "1", "2"}});
  const auto same = [](const Series& a, const Series& b) {
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
  };
  same(vwap(s, 4, 3), vwap(s, 6, 3));
  same(macd(s, 4, 2, 3, 2).histogram, macd(s, 6, 2, 3, 2).histogram);
  same(adx(s, 4, 2).adx, adx(s, 6, 2).adx);
}
