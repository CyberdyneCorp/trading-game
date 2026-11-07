// What-if valuation and combo dry run (design D4 of add-academy-and-strategies, tasks E2 and E3).

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "scenario_builder.hpp"
#include "../src/c_api_run.hpp"
#include "strategy_templates.hpp"
#include "tradelab/c_api.h"
#include "tradelab/generator.hpp"
#include "tradelab/options.hpp"
#include "tradelab/whatif.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

double d(Price p) { return static_cast<double>(p.raw) / Price::kScale; }
double d(Money m) { return static_cast<double>(m.raw) / Money::kScale; }
double d(Qty q) { return static_cast<double>(q.raw) / Qty::kScale; }

WhatIfLeg opt(OptionKind k, OrderSide side, const char* strike, const char* entry, const char* qty = "1") {
  WhatIfLeg l;
  l.kind = k;
  l.side = side;
  l.qty = Q(qty);
  l.strike = P(strike);
  l.entry = P(entry);
  l.has_entry = true;
  l.minutes_to_expiry = 30 * 1440;
  l.iv = parse_decimal<Vol>("0.6").value();
  return l;
}

WhatIfLeg perp(OrderSide side, const char* entry, const char* qty = "1") {
  WhatIfLeg l;
  l.perp = true;
  l.side = side;
  l.qty = Q(qty);
  l.entry = P(entry);
  l.has_entry = true;
  return l;
}

WhatIfRequest runless(std::vector<WhatIfLeg> legs) {
  WhatIfRequest r;
  r.forward = P("100");
  r.minutes_per_step = 1440;
  r.multiplier = Q("1");
  r.legs = std::move(legs);
  return r;
}

constexpr OrderSide kBuy = OrderSide::Buy;
constexpr OrderSide kSell = OrderSide::Sell;
constexpr OptionKind kCall = OptionKind::Call;
constexpr OptionKind kPut = OptionKind::Put;

// Expiry P&L in doubles, independent of the engine's fixed-point analytics.
double brute_pnl(const std::vector<WhatIfLeg>& legs, double s) {
  double total = 0.0;
  for (const WhatIfLeg& l : legs) {
    const double k = d(l.strike);
    const double stop = d(l.stop);
    const double held = stop <= 0.0 ? s : (l.side == kBuy ? std::max(s, stop) : std::min(s, stop));
    const double pay = l.perp ? held : (l.kind == kCall ? std::max(0.0, s - k) : std::max(0.0, k - s));
    total += (l.side == kBuy ? 1.0 : -1.0) * d(l.qty) * (pay - d(l.entry));
  }
  return total;
}

// Breakevens, bounds and their unboundedness against a fine grid (0.001 steps up to 3x the top strike).
void check_against_brute_force(const std::vector<WhatIfLeg>& legs) {
  const WhatIfResult r = option_whatif(runless(legs));
  REQUIRE(r.error == WhatIfError::None);
  REQUIRE(r.expiry.has_value());
  double top = 100.0;
  for (const WhatIfLeg& l : legs) top = std::max({top, d(l.strike), d(l.entry), d(l.stop)});
  const double hi = 3.0 * top;
  double lo_pnl = 1e300;
  double hi_pnl = -1e300;
  int crossings = 0;
  double prev = brute_pnl(legs, 0.0);
  for (double s = 0.0; s <= hi; s += 0.001) {
    const double p = brute_pnl(legs, s);
    lo_pnl = std::min(lo_pnl, p);
    hi_pnl = std::max(hi_pnl, p);
    if ((p > 0) != (prev > 0) && std::abs(p) > 1e-9 && std::abs(prev) > 1e-9) ++crossings;
    prev = p;
  }
  const double rising = brute_pnl(legs, hi + 1.0) - brute_pnl(legs, hi);
  for (const Price b : r.expiry->breakevens) CHECK(std::abs(brute_pnl(legs, d(b))) < 1e-6);
  CHECK(static_cast<int>(r.expiry->breakevens.size()) >= crossings);
  if (rising > 1e-9) CHECK_FALSE(r.expiry->max_profit.has_value());
  else CHECK(std::abs(d(*r.expiry->max_profit) - hi_pnl) < 1e-6);
  if (rising < -1e-9) CHECK_FALSE(r.expiry->max_loss.has_value());
  else CHECK(std::abs(d(*r.expiry->max_loss) + lo_pnl) < 1e-6);
}

}  // namespace

TEST_CASE("What-if: iron condor 90/95/105/110 for a 3 credit has breakevens 92 and 108, max loss 2, max profit 3") {
  for (const char* qty : {"1", "2"}) {
    const WhatIfResult r = option_whatif(runless({opt(kPut, kBuy, "90", "0.5", qty), opt(kPut, kSell, "95", "2", qty),
                                                  opt(kCall, kSell, "105", "2", qty), opt(kCall, kBuy, "110", "0.5", qty)}));
    REQUIRE(r.expiry.has_value());
    REQUIRE(r.expiry->breakevens.size() == 2);
    CHECK(r.expiry->breakevens[0] == P("92"));
    CHECK(r.expiry->breakevens[1] == P("108"));
    const std::int64_t n = Q(qty).raw / Qty::kScale;
    CHECK(r.expiry->max_loss->raw == 2 * n * Money::kScale);
    CHECK(r.expiry->max_profit->raw == 3 * n * Money::kScale);
  }
}

TEST_CASE("What-if: the seven templates and random leg sets agree with a brute-force expiry payoff") {
  check_against_brute_force({opt(kCall, kBuy, "100", "4"), opt(kCall, kSell, "105", "2")});              // bull call
  check_against_brute_force({opt(kPut, kBuy, "100", "4"), opt(kPut, kSell, "95", "2")});                 // bear put
  check_against_brute_force({perp(kBuy, "100"), opt(kCall, kSell, "105", "2")});                         // covered call
  check_against_brute_force({perp(kBuy, "100"), opt(kPut, kBuy, "95", "2")});                            // protective put
  check_against_brute_force({opt(kCall, kBuy, "100", "4"), opt(kPut, kBuy, "100", "3.5")});              // straddle
  check_against_brute_force({opt(kCall, kSell, "105", "2"), opt(kPut, kSell, "95", "2")});               // short strangle
  check_against_brute_force({opt(kPut, kBuy, "90", "0.5"), opt(kPut, kSell, "95", "2"), opt(kCall, kSell, "105", "2"),
                             opt(kCall, kBuy, "110", "0.5")});                                           // iron condor
  std::uint64_t x = 0x9E3779B97F4A7C15ULL;
  const auto next = [&x](int m) {
    x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<int>((x >> 33U) % static_cast<std::uint64_t>(m));
  };
  for (int t = 0; t < 200; ++t) {
    std::vector<WhatIfLeg> legs;
    const int n = 1 + next(4);
    std::vector<std::string> text;
    for (int i = 0; i < n; ++i) {
      const std::string k = std::to_string(80 + next(41));
      const std::string e = std::to_string(next(12)) + "." + std::to_string(next(100));
      const std::string q = std::to_string(1 + next(3));
      legs.push_back(opt(next(2) == 0 ? kCall : kPut, next(2) == 0 ? kBuy : kSell, k.c_str(), e.c_str(), q.c_str()));
    }
    if (next(3) == 0) legs.push_back(perp(next(2) == 0 ? kBuy : kSell, "100"));
    if (next(2) == 0 && legs.back().perp) legs.back().stop = P(std::to_string(85 + next(31)).c_str());
    check_against_brute_force(legs);
  }
}

TEST_CASE("What-if: at expiry the value is intrinsic and the time-value Greeks are zero; IV shifts are floored at 5%") {
  WhatIfRequest r = runless({opt(kCall, kBuy, "100", "4"), opt(kPut, kSell, "95", "2")});
  r.shift.forward = P("108");
  r.shift.minutes_elapsed = 30 * 1440;
  const WhatIfResult e = option_whatif(r);
  CHECK(e.at_expiry);
  CHECK(e.legs[0].value == P("8"));
  CHECK(e.legs[0].delta.raw == Rate::kScale);
  CHECK(e.legs[1].value == P("0"));
  for (const WhatIfLegResult& l : e.legs) {
    CHECK(l.gamma.raw == 0);
    CHECK(l.theta.raw == 0);
    CHECK(l.vega.raw == 0);
  }
  CHECK(e.net.pnl == M("6"));  // +4 on the call, +2 kept on the put
  REQUIRE(e.today.size() == e.expiry->curve.size());
  for (std::size_t i = 0; i < e.today.size(); ++i) CHECK(e.today[i].pnl == e.expiry->curve[i].pnl);

  WhatIfRequest low = runless({opt(kCall, kBuy, "100", "4")});
  low.legs[0].iv = parse_decimal<Vol>("0.08").value();
  low.shift.iv_points = parse_decimal<Vol>("-5").value();
  const WhatIfResult f = option_whatif(low);
  CHECK(f.legs[0].iv_floored);
  CHECK(f.legs[0].iv == parse_decimal<Vol>("0.05").value());
}

TEST_CASE("What-if: a perpetual leg is worth the forward with delta 1; a short call's loss is unbounded") {
  const WhatIfResult r = option_whatif(runless({perp(kBuy, "100", "2")}));
  CHECK(r.net.delta == Q("2"));
  CHECK(r.expiry->breakevens == std::vector<Price>{P("100")});
  CHECK_FALSE(r.expiry->max_profit.has_value());
  CHECK(*r.expiry->max_loss == M("200"));  // prices stop at 0
  const WhatIfResult s = option_whatif(runless({opt(kCall, kSell, "105", "2"), opt(kPut, kSell, "95", "2")}));
  CHECK_FALSE(s.expiry->max_loss.has_value());
  CHECK(*s.expiry->max_profit == M("4"));
  CHECK(s.expiry->breakevens == std::vector<Price>{P("91"), P("109")});
}

TEST_CASE("What-if: a held perpetual's stop bounds the combo's loss at expiry (covered call, protective put, short)") {
  // Covered call on a long of 48 from 100 with its stop at 97: the loss is the stop's less the premium.
  WhatIfLeg held = perp(kBuy, "100", "48");
  held.stop = P("97");
  const WhatIfResult cc = option_whatif(runless({held, opt(kCall, kSell, "105", "2", "48")}));
  REQUIRE(cc.expiry.has_value());
  CHECK(*cc.expiry->max_loss == M("48"));     // (100 - 97 - 2) x 48
  CHECK(*cc.expiry->max_profit == M("336"));  // (105 - 100 + 2) x 48
  CHECK(cc.expiry->breakevens == std::vector<Price>{P("98")});
  const WhatIfResult naked = option_whatif(runless({perp(kBuy, "100", "48"), opt(kCall, kSell, "105", "2", "48")}));
  CHECK(*naked.expiry->max_loss == M("4704"));  // without the stop: (100 - 2) x 48, down to a price of 0
  CHECK(cc.net.pnl == naked.net.pnl);           // value and P&L today ignore the stop
  // Protective put above its stop: the stop fires first, the premium is lost.
  const WhatIfResult pp = option_whatif(runless({held, opt(kPut, kBuy, "95", "2", "48")}));
  CHECK(*pp.expiry->max_loss == M("240"));  // (100 - 97 + 2) x 48
  // A short stopped above is bounded above.
  WhatIfLeg s = perp(kSell, "100", "2");
  s.stop = P("103");
  const WhatIfResult sh = option_whatif(runless({s}));
  CHECK(*sh.expiry->max_loss == M("6"));
  CHECK(*sh.expiry->max_profit == M("200"));
  check_against_brute_force({held, opt(kCall, kSell, "105", "2", "48")});
  check_against_brute_force({s, opt(kCall, kSell, "105", "2"), opt(kPut, kBuy, "95", "1")});
  // Through the C ABI; a stop on an option leg is refused.
  const std::string ok = tl_option_whatif(
      R"({"forward":"100","minutesPerStep":1440,"multiplier":"1","legs":[{"kind":"perp","side":"buy","qty":"48",)"
      R"("entry":"100","stop":"97"},{"kind":"call","side":"sell","qty":"48","strike":"105","minutesToExpiry":43200,)"
      R"("iv":"0.6","entry":"2"}]})");
  CHECK(ok.find(R"("maxLoss":"48.00000000")") != std::string::npos);
  WhatIfRequest bad = runless({opt(kCall, kBuy, "100", "4")});
  bad.legs[0].stop = P("90");
  CHECK(option_whatif(bad).error == WhatIfError::InvalidInput);
}

TEST_CASE("What-if: legs on different expiries have no expiry analytics; bad input is refused") {
  WhatIfRequest r = runless({opt(kCall, kBuy, "100", "4"), opt(kCall, kSell, "105", "2")});
  r.legs[1].minutes_to_expiry = 60 * 1440;
  const WhatIfResult w = option_whatif(r);
  CHECK(w.error == WhatIfError::None);
  CHECK_FALSE(w.expiry.has_value());
  CHECK_FALSE(w.today.empty());
  CHECK(option_whatif(runless({})).error == WhatIfError::InvalidInput);
  WhatIfRequest bad = runless({opt(kCall, kBuy, "100", "4")});
  bad.grid.points = 1;
  CHECK(option_whatif(bad).error == WhatIfError::InvalidInput);
  CHECK(std::string(tl_option_whatif("{\"legs\":[]}")) == "{\"error\":\"invalid_input\"}");
  CHECK(std::string(tl_option_whatif("not json")) == "{\"error\":\"invalid_input\"}");
}

TEST_CASE("OPT05: the run what-if with no shift equals quote() for every series of every mission at 5 steps") {
  std::size_t compared = 0;
  for (const MissionInfo& m : mission_catalog()) {
    Scenario sc;
    REQUIRE(build_mission(m.id, 11, sc));
    if (sc.options.series.empty()) continue;
    RunState st = start_run(sc, 11);
    for (int k = 0; k < 5; ++k) {
      for (const OptionSeries& s : sc.options.series) {
        if (st.step < s.list_step || st.step >= s.expiry_step) continue;
        const OptionQuote q = quote(sc, s, st.step, sc.bars[st.step].close);
        WhatIfRequest req;
        WhatIfLeg l;
        l.series = s.id;
        l.qty = Q("1");
        req.legs.push_back(l);
        const WhatIfResult r = run_option_whatif(sc, st, req);
        REQUIRE(r.error == WhatIfError::None);
        const WhatIfLegResult& x = r.legs[0];
        INFO(m.id, " step ", st.step, " series ", s.id);
        CHECK(round_to_step(x.value, sc.options.premium_tick, Rounding::HalfEven).value() == q.mid);
        CHECK(x.iv == q.iv);
        CHECK(x.delta == q.delta);
        CHECK(x.gamma == q.gamma);
        CHECK(x.theta == q.theta);
        CHECK(x.vega == q.vega);
        CHECK(x.leg.entry == q.ask);
        ++compared;
      }
      for (int b = 0; b < 9 && st.status == RunStatus::Active; ++b) advance(sc, st);
    }
  }
  CHECK(compared > 500);
}

TEST_CASE("OPT06: the run what-if at step s ignores bars and the IV base after s") {
  for (const char* id : {"m05-options-writer", "m09-event-straddle", "m04-options-buyer"}) {
    Scenario sc;
    if (!build_mission(id, 5, sc)) continue;
    RunState st = start_run(sc, 5);
    for (int b = 0; b < 3; ++b) advance(sc, st);
    Scenario future = sc;
    for (std::size_t i = st.step + 1; i < future.bars.size(); ++i) {
      Bar& bar = future.bars[i];
      bar.open = Price::from_raw(bar.open.raw * 3 / 2);
      bar.high = Price::from_raw(bar.high.raw * 3 / 2);
      bar.low = Price::from_raw(bar.low.raw * 3 / 2);
      bar.close = Price::from_raw(bar.close.raw * 3 / 2);
      if (!future.options.iv_base.empty()) future.options.iv_base[i] = Vol::from_raw(future.options.iv_base[i].raw * 2);
    }
    for (const OptionSeries& s : sc.options.series) {
      if (st.step < s.list_step || st.step >= s.expiry_step) continue;
      WhatIfRequest req;
      WhatIfLeg l;
      l.series = s.id;
      l.qty = Q("1");
      req.legs.push_back(l);
      for (const std::uint32_t bars : {0U, 3U, 1000U}) {
        req.shift.bars = bars;
        req.shift.iv_points = parse_decimal<Vol>(bars == 3 ? "4" : "-2").value();
        const WhatIfResult a = run_option_whatif(sc, st, req);
        const WhatIfResult b = run_option_whatif(future, st, req);
        CHECK(a.legs[0].value == b.legs[0].value);
        CHECK(a.legs[0].delta == b.legs[0].delta);
        CHECK(a.net.pnl == b.net.pnl);
        REQUIRE(a.today.size() == b.today.size());
        for (std::size_t i = 0; i < a.today.size(); ++i) CHECK(a.today[i].pnl == b.today[i].pnl);
      }
    }
  }
}

TEST_CASE("What-if and combo dry run are pure: state JSON, digests and the event log are unchanged") {
  tl_run* run = tl_run_create("m05-options-writer", 3);
  REQUIRE(run != nullptr);
  tl_advance(run, 2);
  const std::string before = tl_state(run);
  const Scenario& sc = run->scenario;
  const auto legs = test::template_legs(sc, run->state, test::Template::IronCondor, "1");
  REQUIRE(legs.size() == 4);
  std::string req = "{\"legs\":[";
  for (std::size_t i = 0; i < legs.size(); ++i) {
    req += (i ? "," : "") + std::string("{\"series\":") + std::to_string(legs[i].series) + ",\"side\":\"" +
           (legs[i].side == kBuy ? "buy" : "sell") + "\",\"qty\":\"1\"}";
  }
  req += "],\"shift\":{\"bars\":2,\"ivPoints\":\"3\"}}";
  const std::string w = tl_run_option_whatif(run, req.c_str());
  CHECK(w.find("\"breakevens\":[\"") != std::string::npos);
  const std::string p = tl_combo_preview(run, req.c_str());
  CHECK(p.find("\"accepted\":true") == 1);
  CHECK(std::string(tl_state(run)) == before);
  CHECK(std::string(tl_run_option_whatif(run, "{\"legs\":[{\"series\":99999,\"side\":\"buy\",\"qty\":\"1\"}]}")) ==
        "{\"error\":\"series_not_listed\"}");
  CHECK(std::string(tl_combo_preview(run, "{\"legs\":[]}")) == "{\"error\":\"invalid_input\"}");
  CHECK(std::string(tl_state(run)) == before);
  tl_run_destroy(run);
}
