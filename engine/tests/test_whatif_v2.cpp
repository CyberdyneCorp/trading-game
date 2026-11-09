// Wave 1 engine extensions of add-practice-and-advanced-strategies: multi-expiry horizon analytics,
// the heatmap and the holdings' risk block (design D4), combo limit legs (D2), calendar margin (D5)
// and per-mission risk limits (D9). All report-only or per-mission data: no digest changes.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../src/c_api_run.hpp"
#include "../src/spread_margin.hpp"
#include "scenario_builder.hpp"
#include "strategy_templates.hpp"
#include "tradelab/c_api.h"
#include "tradelab/generator.hpp"
#include "tradelab/whatif.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

constexpr OrderSide kBuy = OrderSide::Buy;
constexpr OrderSide kSell = OrderSide::Sell;
constexpr OptionKind kCall = OptionKind::Call;
constexpr OptionKind kPut = OptionKind::Put;

double d(Money m) { return static_cast<double>(m.raw) / Money::kScale; }

WhatIfLeg opt(OptionKind k, OrderSide side, const char* strike, const char* entry, std::uint32_t days) {
  WhatIfLeg l;
  l.kind = k;
  l.side = side;
  l.qty = Q("1");
  l.strike = P(strike);
  l.entry = P(entry);
  l.has_entry = true;
  l.minutes_to_expiry = days * 1440;
  l.iv = parse_decimal<Vol>("0.6").value();
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

// The horizon P&L at price s through another path: the plain what-if shifted to the first expiry.
Money pnl_at_horizon(WhatIfRequest r, std::uint32_t minutes, Price s) {
  r.shift.minutes_elapsed = minutes;
  r.shift.forward = s;
  r.grid = WhatIfGrid{P("1"), P("2"), 2};
  return option_whatif(r).net.pnl;
}

// Every tick of the grid range scanned: each sign change has a breakeven on its profitable side.
void check_horizon_brute_force(const WhatIfRequest& r) {
  const WhatIfResult w = option_whatif(r);
  REQUIRE(w.error == WhatIfError::None);
  REQUIRE(w.horizon.has_value());
  const HorizonAnalytics& h = *w.horizon;
  const std::uint32_t minutes = h.expiry;
  const Price tick = P("0.01");
  std::vector<Price> expected;
  Money prev = pnl_at_horizon(r, minutes, P("70"));
  for (std::int64_t t = 7001; t <= 13000; ++t) {
    const Price s = Price::from_raw(t * tick.raw);
    const Money p = pnl_at_horizon(r, minutes, s);
    if (p.raw == 0) expected.push_back(s);
    else if (prev.raw != 0 && (p.raw > 0) != (prev.raw > 0)) expected.push_back(p.raw > 0 ? s : Price::from_raw(s.raw - tick.raw));
    prev = p;
  }
  std::vector<Price> in_range;
  for (const Price b : h.breakevens) {
    CHECK(b.raw % tick.raw == 0);
    if (b >= P("70.01") && b <= P("130")) in_range.push_back(b);
  }
  CHECK(in_range == expected);
  for (const Price b : h.breakevens) CHECK(pnl_at_horizon(r, minutes, b).raw >= 0);
}

}  // namespace

// ---- D4 horizon analytics --------------------------------------------------------------------

TEST_CASE("Horizon: calendar and diagonal breakevens match a brute-force tick scan") {
  // Long call calendar at 100 (sell 10 days, buy 40 days) and a call diagonal (sell 105 near, buy 100 far).
  const WhatIfRequest calendar = runless({opt(kCall, kSell, "100", "4.0", 10), opt(kCall, kBuy, "100", "8.0", 40)});
  const WhatIfRequest diagonal = runless({opt(kCall, kSell, "105", "2.0", 10), opt(kCall, kBuy, "100", "8.0", 40)});
  const WhatIfRequest put_calendar = runless({opt(kPut, kSell, "95", "1.8", 10), opt(kPut, kBuy, "95", "5.4", 40)});
  for (const WhatIfRequest* r : {&calendar, &diagonal, &put_calendar}) check_horizon_brute_force(*r);
  const WhatIfResult w = option_whatif(calendar);
  CHECK_FALSE(w.expiry.has_value());  // the single-expiry analytics stay null
  REQUIRE(w.horizon->breakevens.size() == 2);
  CHECK(w.horizon->expiry == 10 * 1440);
}

TEST_CASE("Horizon: a calendar or a diagonal cannot lose more than its debit; uncovered calls are unbounded above") {
  const WhatIfResult cal = option_whatif(runless({opt(kCall, kSell, "100", "4.0", 10), opt(kCall, kBuy, "100", "8.0", 40)}));
  REQUIRE(cal.horizon->max_loss.has_value());
  REQUIRE(cal.horizon->max_profit.has_value());
  CHECK_FALSE(cal.horizon->unbounded_above);
  CHECK(*cal.horizon->max_loss <= M("4"));  // the debit 8 - 4
  CHECK(*cal.horizon->max_profit > Money{});
  const WhatIfResult diag = option_whatif(runless({opt(kCall, kSell, "105", "2.0", 10), opt(kCall, kBuy, "100", "8.0", 40)}));
  REQUIRE(diag.horizon->max_loss.has_value());
  CHECK(*diag.horizon->max_loss <= M("6"));
  // The reverse (short) calendar: bounded both ways, its best case is the debit it collected.
  const WhatIfResult rev = option_whatif(runless({opt(kCall, kBuy, "100", "4.0", 10), opt(kCall, kSell, "100", "8.0", 40)}));
  CHECK_FALSE(rev.horizon->unbounded_above);
  REQUIRE(rev.horizon->max_profit.has_value());
  CHECK(*rev.horizon->max_profit <= M("4"));
  // Two near calls written against one far call: the slope above every strike is -1.
  WhatIfRequest ratio = runless({opt(kCall, kSell, "100", "4.0", 10), opt(kCall, kBuy, "100", "8.0", 40)});
  ratio.legs[0].qty = Q("2");
  const WhatIfResult r = option_whatif(ratio);
  CHECK(r.horizon->unbounded_above);
  CHECK_FALSE(r.horizon->max_loss.has_value());
  CHECK(r.horizon->max_profit.has_value());
}

TEST_CASE("Horizon: the run what-if uses the IV base published now (no look-ahead)") {
  Scenario sc;
  REQUIRE(build_mission("m17-before-expiry", 5, sc));
  RunState st = start_run(sc, 5);
  for (int b = 0; b < 3; ++b) advance(sc, st);
  std::vector<const OptionSeries*> listed;
  for (const OptionSeries& s : sc.options.series) {
    if (st.step >= s.list_step && st.step < s.expiry_step && s.kind == kCall) listed.push_back(&s);
  }
  const auto near = std::min_element(listed.begin(), listed.end(), [](auto* a, auto* b) { return a->expiry_step < b->expiry_step; });
  const OptionSeries* far = nullptr;
  for (const OptionSeries* s : listed) {
    if (s->expiry_step > (*near)->expiry_step && s->strike == (*near)->strike) far = s;
  }
  REQUIRE(far != nullptr);
  WhatIfRequest req;
  WhatIfLeg a;
  a.series = (*near)->id;
  a.side = kSell;
  a.qty = Q("1");
  WhatIfLeg b = a;
  b.series = far->id;
  b.side = kBuy;
  req.legs = {a, b};
  req.heatmap = WhatIfHeatmapSpec{P("80"), P("120"), 9, {0, 5, 40}};
  Scenario future = sc;
  for (std::size_t i = st.step + 1; i < future.bars.size(); ++i) {
    future.bars[i].close = Price::from_raw(future.bars[i].close.raw * 3 / 2);
    if (!future.options.iv_base.empty()) future.options.iv_base[i] = Vol::from_raw(future.options.iv_base[i].raw * 2);
  }
  const WhatIfResult x = run_option_whatif(sc, st, req);
  const WhatIfResult y = run_option_whatif(future, st, req);
  REQUIRE(x.horizon.has_value());
  CHECK(x.horizon->expiry == (*near)->expiry_step - st.step);
  CHECK(x.horizon->breakevens == y.horizon->breakevens);
  for (std::size_t i = 0; i < x.horizon->curve.size(); ++i) CHECK(x.horizon->curve[i].pnl == y.horizon->curve[i].pnl);
  REQUIRE(x.heatmap.has_value());
  CHECK(x.heatmap->pnl == y.heatmap->pnl);
  for (const Price be : x.horizon->breakevens) CHECK(be.raw % sc.contract.tick_size.raw == 0);
}

// ---- D4 heatmap ----------------------------------------------------------------------------------

TEST_CASE("Heatmap: time 0 equals the today curve at the same prices; capped at 61 x 25") {
  WhatIfRequest r = runless({opt(kPut, kBuy, "90", "1.0", 20), opt(kPut, kSell, "95", "2.0", 20),
                             opt(kCall, kSell, "105", "2.0", 20), opt(kCall, kBuy, "110", "1.0", 20)});
  r.grid = WhatIfGrid{P("80"), P("120"), 41};
  r.heatmap = WhatIfHeatmapSpec{P("80"), P("120"), 41, {0, 1440, 20 * 1440, 30 * 1440}};
  const WhatIfResult w = option_whatif(r);
  REQUIRE(w.heatmap.has_value());
  REQUIRE(w.heatmap->pnl.size() == 4);
  for (std::size_t i = 0; i < w.today.size(); ++i) {
    CHECK(w.heatmap->prices[i] == w.today[i].price);
    CHECK(w.heatmap->pnl[0][i] == w.today[i].pnl);
    CHECK(w.heatmap->pnl[2][i] == w.expiry->curve[i].pnl);  // at expiry: the expiry payoff
    CHECK(w.heatmap->pnl[3][i] == w.expiry->curve[i].pnl);  // past it: unchanged
  }
  WhatIfRequest big = r;
  big.heatmap->points = 62;
  CHECK(option_whatif(big).error == WhatIfError::InvalidInput);
  big = r;
  big.heatmap->times.assign(26, 0);
  CHECK(option_whatif(big).error == WhatIfError::InvalidInput);
  big = r;
  big.heatmap->lo = Price{};
  CHECK(option_whatif(big).error == WhatIfError::InvalidInput);
}

// ---- D4 holdings and the risk block, D9 limits --------------------------------------------------

namespace {

// A run with a perpetual long, a working stop and two option positions in m05.
tl_run* run_with_holdings() {
  tl_run* run = tl_run_create("m05-options-writer", 3);
  tl_advance(run, 2);
  const std::string state = tl_state(run);
  const auto series_at = [&state](std::size_t n) {
    std::size_t at = state.find("\"chain\":[");
    for (std::size_t i = 0; i <= n; ++i) at = state.find("{\"id\":", at + 1);
    return static_cast<std::uint32_t>(std::stoul(state.substr(at + 6)));
  };
  tl_set_leverage(run, 1, 2);
  tl_place_order(run, 2, 0, 0, "3", nullptr, 0);
  tl_place_option_order(run, 3, series_at(0), 0, 0, "2", nullptr);
  tl_place_option_order(run, 4, series_at(3), 1, 0, "1", nullptr);
  tl_advance(run, 1);
  return run;
}

}  // namespace

TEST_CASE("Holdings: equal to the explicit legs; the risk block is the engine's numbers") {
  tl_run* run = run_with_holdings();
  REQUIRE(run != nullptr);
  const Scenario& sc = run->scenario;
  const RunState& st = run->state;
  REQUIRE_FALSE(st.position.flat());
  REQUIRE(st.option_positions.size() == 2);
  WhatIfRequest h;
  h.holdings = true;
  const WhatIfResult a = run_option_whatif(sc, st, h);
  REQUIRE(a.error == WhatIfError::None);
  WhatIfRequest e;
  WhatIfLeg p;
  p.perp = true;
  p.side = kBuy;
  p.qty = st.position.qty;
  p.entry = st.position.entry;
  p.has_entry = true;
  e.legs.push_back(p);
  for (const OptionPosition& o : st.option_positions) {
    WhatIfLeg l;
    l.series = o.series;
    l.side = o.qty.raw > 0 ? kBuy : kSell;
    l.qty = Q(o.qty.raw > 0 ? "2" : "1");
    l.entry = o.avg_premium;
    l.has_entry = true;
    e.legs.push_back(l);
  }
  const WhatIfResult b = run_option_whatif(sc, st, e);
  REQUIRE(a.legs.size() == b.legs.size());
  CHECK(a.net.value == b.net.value);
  CHECK(a.net.pnl == b.net.pnl);
  CHECK(a.net.delta == b.net.delta);
  CHECK(a.net.gamma == b.net.gamma);
  CHECK(a.net.vega == b.net.vega);
  CHECK(a.net.theta == b.net.theta);
  for (std::size_t i = 0; i < a.today.size(); ++i) CHECK(a.today[i].pnl == b.today[i].pnl);
  REQUIRE(a.risk.has_value());
  CHECK_FALSE(b.risk.has_value());
  const RiskBlock& r = *a.risk;
  const Price close = sc.bars[st.step].close;
  CHECK(r.delta_units == a.net.delta);
  CHECK(std::abs(d(r.delta_notional) - static_cast<double>(r.delta_units.raw) / 1e8 * static_cast<double>(close.raw) / 1e8) < 1e-6);
  const double g = std::abs(static_cast<double>(a.net.gamma.raw) / 1e8);
  const double s = static_cast<double>(close.raw) / 1e8;
  CHECK(std::abs(d(r.gamma_1pct) - 0.5 * g * (0.01 * s) * (0.01 * s)) < 1e-6);
  CHECK(r.vega_point == a.net.vega);
  CHECK(r.theta_bar == a.net.theta);
  const AccountView v = account_view(sc, st);
  CHECK(r.equity == v.equity);
  CHECK(r.delta_bp == static_cast<std::int64_t>(std::llround(d(r.delta_notional) / d(v.equity) * 10000.0)));
  CHECK(r.limits.empty());  // m05 declares no risk limits
  // A long pays positive funding: the estimate's sign follows the published rate.
  if (v.next_funding_rate.raw > 0) CHECK(r.funding_next.raw < 0);
  if (v.next_funding_rate.raw < 0) CHECK(r.funding_next.raw > 0);
  tl_run_destroy(run);
}

TEST_CASE("Holdings: the perpetual's working stop goes in; an empty book still answers") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "101", "99", "100"}, {"100", "101", "99", "100"}});
  sc.funding_rate.assign(sc.bars.size(), R("0.0001"));
  Driver d(sc);
  WhatIfRequest h;
  h.holdings = true;
  const WhatIfResult empty = run_option_whatif(sc, d.state(), h);
  REQUIRE(empty.error == WhatIfError::None);
  CHECK(empty.legs.empty());
  CHECK_FALSE(empty.expiry.has_value());
  REQUIRE(empty.risk.has_value());
  CHECK(empty.risk->delta_units == Qty{});
  REQUIRE(d.order(OrderType::Market, kBuy, "10").accepted);
  d.step(1);
  REQUIRE(d.order(OrderType::StopMarket, kSell, "10", "95", true).accepted);
  const WhatIfResult w = run_option_whatif(sc, d.state(), h);
  REQUIRE(w.legs.size() == 1);
  CHECK(w.legs[0].leg.stop == P("95"));
  REQUIRE(w.expiry.has_value());
  CHECK(*w.expiry->max_loss == M("50"));  // closed at the stop
  CHECK(w.risk->delta_units == Q("10"));
  CHECK(w.risk->delta_notional == M("1000"));
  CHECK(w.risk->funding_next == M("-0.1"));  // 1,000 x 0.0001, paid by the long
  CHECK(w.risk->delta_bp == 1000);           // 1,000 of 10,000 equity
}

TEST_CASE("Risk limits: declared per mission, flagged when exceeded, in the state only when declared") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "101", "99", "100"}, {"100", "101", "99", "100"}});
  sc.mission.risk_limits.delta_bp = 1500;
  sc.mission.risk_limits.theta_bp = 5;
  Driver d(sc);
  REQUIRE(d.order(OrderType::Market, kBuy, "20").accepted);
  d.step(1);
  WhatIfRequest h;
  h.holdings = true;
  const WhatIfResult w = run_option_whatif(sc, d.state(), h);
  REQUIRE(w.risk->limits.size() == 2);
  CHECK(w.risk->limits[0].kind == RiskLimitKind::Delta);
  CHECK(w.risk->limits[0].value_bp == 2000);
  CHECK(w.risk->limits[0].exceeded);
  CHECK(w.risk->limits[1].kind == RiskLimitKind::Theta);
  CHECK_FALSE(w.risk->limits[1].exceeded);
  // A proposed hedge leg after the holdings: the totals after the change.
  WhatIfLeg hedge;
  hedge.perp = true;
  hedge.side = kSell;
  hedge.qty = Q("10");
  h.legs.push_back(hedge);
  const WhatIfResult after = run_option_whatif(sc, d.state(), h);
  CHECK(after.risk->delta_units == Q("10"));
  CHECK_FALSE(after.risk->limits[0].exceeded);

  tl_run* plain = tl_run_create("m16-range-condor", 3);
  const std::string s = tl_state(plain);
  CHECK(s.find("riskLimits") == std::string::npos);
  CHECK(s.find("calendarMargin") == std::string::npos);
  tl_run_destroy(plain);
}

TEST_CASE("What-if extensions are pure and parsed through the C ABI") {
  tl_run* run = run_with_holdings();
  const std::string before = tl_state(run);
  const std::string w = tl_run_option_whatif(run, R"({"holdings":true,"heatmap":{"lo":"80","hi":"120","points":5,"bars":[0,3]}})");
  CHECK(w.find("\"risk\":{\"deltaUnits\":") != std::string::npos);
  CHECK(w.find("\"heatmap\":{\"prices\":[\"80.00000000\"") != std::string::npos);
  CHECK(w.find("\"times\":[0,3]") != std::string::npos);
  CHECK(std::string(tl_state(run)) == before);
  CHECK(std::string(tl_run_option_whatif(run, R"({"holdings":1})")) == "{\"error\":\"invalid_input\"}");
  CHECK(std::string(tl_run_option_whatif(run, R"({"legs":[]})")) == "{\"error\":\"invalid_input\"}");
  CHECK(std::string(tl_option_whatif(R"({"forward":"100","minutesPerStep":60,"multiplier":"1","holdings":true,)"
                                     R"("legs":[{"kind":"call","side":"buy","qty":"1","strike":"100","minutesToExpiry":600,"iv":"0.5","entry":"2"}]})")) ==
        "{\"error\":\"invalid_input\"}");
  const std::string cal = tl_option_whatif(
      R"({"forward":"100","minutesPerStep":1440,"multiplier":"1","tick":"0.05","legs":[)"
      R"({"kind":"call","side":"sell","qty":"1","strike":"100","minutesToExpiry":14400,"iv":"0.6","entry":"4"},)"
      R"({"kind":"call","side":"buy","qty":"1","strike":"100","minutesToExpiry":57600,"iv":"0.6","entry":"8"}]})");
  CHECK(cal.find("\"expiry\":null") != std::string::npos);
  CHECK(cal.find("\"horizon\":{\"expiryStep\":14400,\"breakevens\":[") != std::string::npos);
  CHECK(cal.find("\"unboundedAbove\":false") != std::string::npos);
  tl_run_destroy(run);
}

// ---- D2 combo limit legs ---------------------------------------------------------------------------

TEST_CASE("Combo dry run: limit legs and each time in force match a real placement leg by leg") {
  Scenario sc;
  REQUIRE(build_mission("m16-range-condor", 9, sc));
  RunState st = start_run(sc, 9);
  advance(sc, st);
  const std::vector<ComboLeg> market = template_legs(sc, st, Template::IronCondor, "2");
  REQUIRE(market.size() == 4);
  std::size_t checked = 0;
  for (const TimeInForce tif : {TimeInForce::Gtc, TimeInForce::Bars, TimeInForce::Ioc, TimeInForce::Fok}) {
    std::vector<ComboLeg> legs = market;
    for (ComboLeg& l : legs) {
      const OptionQuote q = quote(sc, *find_series(sc, l.series), st.step, sc.bars[st.step].close);
      l.type = OrderType::Limit;
      l.premium = round_to_step(q.mid, sc.options.premium_tick, l.side == kBuy ? Rounding::Floor : Rounding::Ceil).value();
      l.tif = tif;
      l.tif_bars = tif == TimeInForce::Bars ? 3 : 0;
    }
    const RunState before = st;
    const ComboPreview p = combo_preview(sc, st, legs);
    CHECK(state_digest(st) == state_digest(before));
    RunState real = st;
    std::uint64_t seq = real.last_command_seq;
    for (std::size_t i = 0; i < legs.size(); ++i) {
      Command c;
      c.type = CommandType::PlaceOptionOrder;
      c.seq = ++seq;
      c.client_id = 1'000'000 + seq;
      c.decision_step = real.step;
      c.side = legs[i].side;
      c.qty = legs[i].qty;
      c.series = legs[i].series;
      c.order_type = OrderType::Limit;
      c.price = legs[i].premium;
      c.tif = legs[i].tif;
      c.tif_bars = legs[i].tif_bars;
      const CommandResult r = apply_command(sc, real, c);
      CHECK(r.accepted);
      CHECK(p.legs[i].accepted == r.accepted);
      CHECK(p.legs[i].reason == r.reason);
      CHECK(p.legs[i].available_after == account_view(sc, real).available);
      CHECK(p.legs[i].premium == detail::units_value(sc, legs[i].qty, legs[i].premium, Rounding::HalfEven));
      ++checked;
    }
    const Order& last = real.orders.back();
    CHECK(last.tif == tif);
  }
  CHECK(checked == 16);
  // An invalid time in force is the engine's refusal, leg by leg.
  std::vector<ComboLeg> bad = market;
  bad[0].type = OrderType::Limit;
  bad[0].premium = P("0.5");
  bad[0].tif = TimeInForce::Bars;
  bad[0].tif_bars = 100000;
  CHECK(combo_preview(sc, st, bad).legs[0].reason == Reason::InvalidTimeInForce);
}

TEST_CASE("Combo dry run: limit legs through the C ABI; a perpetual limit leg is invalid input") {
  tl_run* run = tl_run_create("m16-range-condor", 9);
  tl_advance(run, 1);
  const std::string state = tl_state(run);
  const std::size_t at = state.find("\"chain\":[{\"id\":");
  const auto series = std::stoul(state.substr(at + 15));
  const std::string req = R"({"legs":[{"series":)" + std::to_string(series) + R"(,"side":"buy","qty":"1","type":"limit","premium":"0.10","tif":"ioc"}]})";
  const std::string p = tl_combo_preview(run, req.c_str());
  CHECK(p.find("\"accepted\":true") == 1);
  CHECK(p.find("\"premium\":\"0.10000000\"") != std::string::npos);
  CHECK(std::string(tl_combo_preview(run, R"({"legs":[{"kind":"perp","side":"buy","qty":"1","type":"limit","premium":"10"}]})")) ==
        "{\"error\":\"invalid_input\"}");
  const std::string no_premium = R"({"legs":[{"series":)" + std::to_string(series) + R"(,"side":"buy","qty":"1","type":"limit"}]})";
  CHECK(std::string(tl_combo_preview(run, no_premium.c_str())) == "{\"error\":\"invalid_input\"}");
  CHECK(std::string(tl_state(run)) == state);
  tl_run_destroy(run);
}

// ---- D5 calendar margin ---------------------------------------------------------------------------

namespace {

// Flat bars at 100 and two expiries (bars 100 and 200), strikes 95/100/105, both kinds.
Scenario calendar_scenario(bool calendar) {
  Scenario s = make_scenario({{"100", "100", "100", "100"}});
  const Bar bar = s.bars[0];
  s.bars.assign(300, bar);
  s.funding_rate.assign(s.bars.size(), Rate{});
  add_options(s, 100, {"95", "100", "105"}, true);
  const std::vector<OptionSeries> near = s.options.series;
  std::uint32_t id = static_cast<std::uint32_t>(near.size()) + 1;
  for (OptionSeries x : near) {
    x.id = id++;
    x.expiry_step = 200;
    s.options.series.push_back(x);
  }
  s.options.spread_margin = true;
  s.options.calendar_margin = calendar;
  return s;
}

std::uint32_t series_of(const Scenario& sc, OptionKind k, const char* strike, std::uint32_t expiry) {
  for (const OptionSeries& s : sc.options.series) {
    if (s.kind == k && s.strike == P(strike) && s.expiry_step == expiry) return s.id;
  }
  return 0;
}

Money initial(const Scenario& sc, const std::vector<detail::Holding>& h) {
  return detail::total_initial(detail::short_requirements(sc, h, 0, P("100")));
}

}  // namespace

TEST_CASE("Calendar margin: a calendar short needs nothing, a diagonal at most its strike gap") {
  const Scenario sc = calendar_scenario(true);
  const Scenario off = calendar_scenario(false);
  const std::vector<detail::Holding> cal{{series_of(sc, kCall, "100", 100), Q("-3")}, {series_of(sc, kCall, "100", 200), Q("3")}};
  CHECK(initial(sc, cal) == Money{});
  CHECK(initial(off, cal) > M("10"));  // without the flag the near short is naked
  const std::vector<detail::Holding> diag{{series_of(sc, kCall, "100", 100), Q("-2")}, {series_of(sc, kCall, "105", 200), Q("2")}};
  CHECK(initial(sc, diag) <= M("10"));  // gap 5 x 2 units
  const std::vector<detail::Holding> put_diag{{series_of(sc, kPut, "100", 100), Q("-1")}, {series_of(sc, kPut, "95", 200), Q("1")}};
  CHECK(initial(sc, put_diag) <= M("5"));
}

TEST_CASE("Calendar margin: a long expiring before the short never covers it; pairing is order-independent") {
  const Scenario sc = calendar_scenario(true);
  const std::vector<detail::Holding> early{{series_of(sc, kCall, "100", 200), Q("-1")}, {series_of(sc, kCall, "100", 100), Q("1")}};
  const std::vector<detail::Holding> naked{{series_of(sc, kCall, "100", 200), Q("-1")}};
  CHECK(initial(sc, early) == initial(sc, naked));
  std::vector<detail::Holding> book{{series_of(sc, kCall, "100", 100), Q("-2")}, {series_of(sc, kCall, "105", 100), Q("1")},
                                    {series_of(sc, kCall, "100", 200), Q("1")},  {series_of(sc, kPut, "95", 100), Q("-1")},
                                    {series_of(sc, kPut, "95", 200), Q("1")}};
  const Money base = initial(sc, book);
  std::sort(book.begin(), book.end(), [](const detail::Holding& a, const detail::Holding& b) { return a.series > b.series; });
  CHECK(initial(sc, book) == base);
  std::rotate(book.begin(), book.begin() + 2, book.end());
  CHECK(initial(sc, book) == base);
  // Same expiry first: the 105 near call (gap 5) is taken before the far 100 (gap 0).
  const std::vector<detail::ShortRequirement> r = detail::short_requirements(sc, book, 0, P("100"));
  CHECK(r.size() == 2);
}

TEST_CASE("Calendar margin: only with spread margin; on one expiry the flag changes nothing") {
  Scenario bad = calendar_scenario(true);
  bad.options.spread_margin = false;
  CHECK(validate(bad) == ScenarioError::InvalidOptions);
  CHECK(validate(calendar_scenario(true)) == ScenarioError::Ok);
  for (const char* id : {"m13-call-spread", "m14-covered-call", "m16-range-condor", "m17-before-expiry"}) {
    Scenario sc;
    REQUIRE(build_mission(id, 4, sc));
    CHECK_FALSE(sc.options.calendar_margin);
    Scenario on = sc;
    on.options.calendar_margin = true;
    RunState st = start_run(sc, 4);
    advance(sc, st);
    const std::vector<ComboLeg> condor = template_legs(sc, st, Template::IronCondor, "1");
    std::vector<detail::Holding> h;
    for (const ComboLeg& l : condor) h.push_back({l.series, l.side == kBuy ? Q("1") : Q("-1")});
    const Price f = sc.bars[st.step].close;
    CHECK(detail::total_initial(detail::short_requirements(sc, h, st.step, f)) ==
          detail::total_initial(detail::short_requirements(on, h, st.step, f)));
  }
  tl_run* run = tl_run_create("m01-false-breakout", 1);
  CHECK(std::string(tl_state(run)).find("\"spreadMargin\":false}") != std::string::npos);
  tl_run_destroy(run);
}
