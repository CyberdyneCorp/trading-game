#include <doctest/doctest.h>

#include <cmath>

#include "scenario_builder.hpp"
#include "tradelab/options.hpp"
#include "tradelab/serialize.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {
double d(Price p) { return static_cast<double>(p.raw) / Price::kScale; }
double d(Rate r) { return static_cast<double>(r.raw) / Rate::kScale; }

Scenario flat(int bars, const char* px = "100") {
  Scenario s = make_scenario({{px, px, px, px}});
  const Bar bar = s.bars[0];  // copy: assign() from an element of the same vector is undefined
  s.bars.assign(static_cast<std::size_t>(bars), bar);
  s.funding_rate.assign(s.bars.size(), Rate{});
  return s;
}
}  // namespace

TEST_CASE("Black-76 ATM price matches the closed form and put-call parity holds") {
  Scenario s = flat(800);
  add_options(s, 720, {"100"});  // 720 hourly steps = 30 days from step 0
  const OptionQuote c = quote(s, s.options.series[0], 0, P("100"));
  const OptionQuote p = quote(s, s.options.series[1], 0, P("100"));
  // F = K: price = F * (2 N(sigma sqrt(T) / 2) - 1) = 5.7128 for sigma 0.5, T = 30/365.
  CHECK(d(c.mid) == doctest::Approx(5.71).epsilon(0.002));
  CHECK(c.mid == p.mid);  // parity at F = K
  CHECK(c.bid < c.mid);
  CHECK(c.ask > c.mid);
  CHECK(d(c.delta) == doctest::Approx(0.5286).epsilon(0.001));
  CHECK(d(p.delta) == doctest::Approx(d(c.delta) - 1.0).epsilon(1e-6));
  CHECK(c.vega.raw > 0);
  CHECK(c.theta.raw < 0);
  CHECK(c.gamma.raw > 0);
}

TEST_CASE("Option values move the right way") {
  Scenario s = flat(800);
  add_options(s, 720, {"100"});
  const auto call_lo = quote(s, s.options.series[0], 0, P("95"));
  const auto call_hi = quote(s, s.options.series[0], 0, P("105"));
  const auto put_lo = quote(s, s.options.series[1], 0, P("95"));
  const auto put_hi = quote(s, s.options.series[1], 0, P("105"));
  CHECK(call_hi.mid > call_lo.mid);
  CHECK(put_lo.mid > put_hi.mid);
  const auto later = quote(s, s.options.series[0], 600, P("100"));  // less time left
  CHECK(later.mid < quote(s, s.options.series[0], 0, P("100")).mid);
  CHECK_FALSE(quote(s, s.options.series[0], 720, P("100")).listed);  // expired
}

TEST_CASE("OPT04: quotes at step t ignore everything after t (realized-vol model)") {
  Scenario a = make_scenario({{"100", "101", "99", "100.5"}, {"100.5", "102", "100", "101.5"}, {"101.5", "102", "100.5", "101"},
                              {"101", "101.5", "99.5", "100"}, {"100", "100.5", "99", "99.5"}, {"99.5", "100", "98.5", "99"}});
  add_options(a, 5, {"100"});
  a.options.iv_base.clear();  // realized volatility of past closes
  Scenario b = a;
  b.bars[4] = {P("50"), P("200"), P("40"), P("190"), Q("1"), false};
  b.bars[5] = {P("190"), P("195"), P("10"), P("20"), Q("1"), false};
  const auto qa = quote(a, a.options.series[0], 3, a.bars[3].close);
  const auto qb = quote(b, b.options.series[0], 3, b.bars[3].close);
  CHECK(qa.iv_from_realized);
  CHECK(qa.mid == qb.mid);
  CHECK(qa.iv == qb.iv);
  CHECK(qa.delta == qb.delta);
}

TEST_CASE("OPT01: quote grid is bit-frozen (cross-target golden)") {
  Scenario s = flat(800);
  add_options(s, 720, {"80", "90", "100", "110", "120"});
  s.options.skew = parse_decimal<Vol>("-0.1").value();
  s.options.smile = parse_decimal<Vol>("0.3").value();
  s.options.term_slope = parse_decimal<Vol>("0.2").value();
  Writer w;
  for (const OptionSeries& ser : s.options.series) {
    for (std::uint32_t step : {0U, 100U, 400U, 700U, 719U}) {
      for (const char* f : {"85", "97.5", "100", "103.25", "118"}) {
        const OptionQuote q = quote(s, ser, step, P(f));
        w.fixed(q.mid);
        w.fixed(q.bid);
        w.fixed(q.ask);
        w.fixed(q.iv);
        w.fixed(q.delta);
        w.fixed(q.gamma);
        w.fixed(q.theta);
        w.fixed(q.vega);
      }
    }
  }
  CHECK(to_hex(w.digest()) == "14e4b85c086c1a3f5c92fd9814cfc97206da2a1da117b561c2d7bf8e455b722c");
}

TEST_CASE("Option orders fill at the next open's ask; max loss of a long call is premium + fees (OPT02)") {
  Scenario s = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                              {"100", "100", "95", "96"}, {"96", "96", "94", "95"}});
  add_options(s, 3, {"100"});
  Driver dr(s);
  const auto r = dr.option(1, OrderSide::Buy, "10");
  REQUIRE(r.accepted);
  CHECK(dr.state().option_positions.empty());  // accepted, not filled
  dr.step();
  REQUIRE(dr.state().option_positions.size() == 1);
  const Event* fill = last_of(dr.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->series == 1);
  const Money paid = Money::from_raw(-dr.state().option_cash.raw + dr.state().fees_paid.raw);
  dr.step(2);  // expires OTM at the close of bar 3 (settle 95 < 100)
  CHECK(dr.state().option_positions.empty());
  const Event* settle = last_of(dr.state(), EventType::OptionSettlement);
  REQUIRE(settle != nullptr);
  CHECK(settle->amount == Money{});
  CHECK(dr.state().wallet.raw == s.mission.initial_balance.raw - paid.raw);
}

TEST_CASE("OPT02: ITM put settles at intrinsic value") {
  Scenario s = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                              {"100", "100", "91", "92"}, {"92", "92", "92", "92"}});
  add_options(s, 2, {"100"});
  Driver dr(s);
  dr.option(2, OrderSide::Buy, "1");
  dr.step(2);
  const Event* settle = last_of(dr.state(), EventType::OptionSettlement);
  REQUIRE(settle != nullptr);
  CHECK(settle->requested == P("8"));  // intrinsic 100 - 92
  CHECK(settle->amount == M("8"));
  CHECK((settle->flags & kFlagMarkFromLastPrice) != 0);  // single-close settlement is flagged approximate
}

TEST_CASE("Writing options requires permission and margin; series must be listed") {
  Scenario s = flat(50);
  add_options(s, 40, {"100"});
  s.options.series[0].list_step = 10;
  Driver dr(s);
  CHECK(dr.option(2, OrderSide::Sell, "1").reason == Reason::ShortOptionsNotAllowed);
  CHECK(dr.option(1, OrderSide::Buy, "1").reason == Reason::SeriesNotListed);
  CHECK(dr.option(99, OrderSide::Buy, "1").reason == Reason::SeriesNotListed);
  CHECK(dr.option(2, OrderSide::Buy, "0.05").reason == Reason::InvalidQuantity);
  Scenario w = flat(50);
  add_options(w, 40, {"100"}, true);
  Driver dw(w);
  CHECK(dw.option(2, OrderSide::Sell, "1").accepted);
  CHECK(dw.option(2, OrderSide::Sell, "100000").reason == Reason::InsufficientMargin);
}

TEST_CASE("OPT03: short call margin call buys back at the ask; wallet never negative") {
  Scenario s = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                              {"100", "130", "100", "128"}, {"128", "200", "128", "190"}, {"190", "190", "190", "190"}});
  add_options(s, 4, {"100"}, true);
  s.mission.initial_balance = M("400");
  Driver dr(s);
  REQUIRE(dr.option(1, OrderSide::Sell, "10").accepted);
  dr.step(3);
  CHECK(last_of(dr.state(), EventType::MarginCall) != nullptr);
  CHECK(dr.state().option_positions.empty());
  CHECK(dr.state().wallet.raw >= 0);
}

TEST_CASE("SIM08 with options: wallet reconciles with option cash flows (randomized)") {
  for (std::uint64_t seed = 1; seed <= 25; ++seed) {
    Pcg32 rng(seed, Stream::Sandbox);
    Scenario s = make_scenario({{"100", "100", "100", "100"}});
    s.bars.clear();
    std::int64_t px = 100'00;
    for (int i = 0; i < 40; ++i) {
      const std::int64_t o = px;
      px = std::max<std::int64_t>(3000, px + static_cast<std::int64_t>(rng.next_bounded(1201)) - 600);
      auto cents = [](std::int64_t c) { return Price::from_raw(c * 1'000'000); };
      s.bars.push_back({cents(o), cents(std::max(o, px) + 50), cents(std::min(o, px) - 50), cents(px), Q("50"), false});
    }
    s.funding_rate.assign(s.bars.size(), R("0.0002"));
    s.contract.funding_interval = 5;
    add_options(s, 30, {"90", "100", "110"}, true);
    Driver dr(s, seed);
    while (dr.state().status == RunStatus::Active) {
      const auto side = rng.next_bounded(2) == 0 ? OrderSide::Buy : OrderSide::Sell;
      const auto series = 1 + rng.next_bounded(6);
      if (rng.next_bounded(3) == 0) dr.option(series, side, "1");
      if (rng.next_bounded(4) == 0) dr.order(OrderType::Market, side, "1");
      dr.step();
      const RunState& st = dr.state();
      const std::int64_t expected = s.mission.initial_balance.raw + st.realized_pnl.raw - st.fees_paid.raw +
                                    st.funding_net.raw + st.option_cash.raw;
      REQUIRE(st.wallet.raw == expected);
      REQUIRE(st.wallet.raw >= 0);
    }
  }
}
