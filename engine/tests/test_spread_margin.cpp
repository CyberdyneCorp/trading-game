// Defined-risk spread margin (design D5, task E4) and the combo dry run against real placement
// (design D4, task E3).

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../src/options_exec.hpp"
#include "../src/spread_margin.hpp"
#include "scenario_builder.hpp"
#include "strategy_templates.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/whatif.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

// 300 flat hourly bars at 100 and a chain at one expiry, bar 200: ids are call/put pairs by strike.
Scenario spread_scenario(bool spread, const char* balance = "10000") {
  Scenario s = make_scenario({{"100", "100", "100", "100"}});
  const Bar bar = s.bars[0];
  s.bars.assign(300, bar);
  s.funding_rate.assign(s.bars.size(), Rate{});
  add_options(s, 200, {"90", "95", "100", "101", "102", "105", "110"}, true);
  s.options.spread_margin = spread;
  s.mission.initial_balance = M(balance);
  return s;
}

std::uint32_t id_of(const Scenario& sc, OptionKind k, const char* strike) {
  for (const OptionSeries& s : sc.options.series) {
    if (s.kind == k && s.strike == P(strike)) return s.id;
  }
  return 0;
}

constexpr OptionKind kCall = OptionKind::Call;
constexpr OptionKind kPut = OptionKind::Put;
constexpr OrderSide kBuy = OrderSide::Buy;
constexpr OrderSide kSell = OrderSide::Sell;

Money initial_of(const Scenario& sc, const std::vector<detail::Holding>& h) {
  return detail::total_initial(detail::short_requirements(sc, h, 0, P("100")));
}

detail::Holding hold(const Scenario& sc, OptionKind k, const char* strike, const char* qty) {
  return {id_of(sc, k, strike), Q(qty)};
}

}  // namespace

TEST_CASE("Spread margin: a debit spread's short needs nothing, a credit spread at most its width, a condor its larger side") {
  const Scenario sc = spread_scenario(true);
  const Scenario naked = spread_scenario(false);
  CHECK(initial_of(sc, {hold(sc, kCall, "100", "1"), hold(sc, kCall, "105", "-1")}) == Money{});
  const Money credit = initial_of(sc, {hold(sc, kCall, "105", "-2"), hold(sc, kCall, "110", "2")});
  CHECK(credit <= M("10"));
  CHECK(credit == M("10"));  // per leg is larger, so the width x units
  const std::vector<detail::Holding> condor{hold(sc, kPut, "90", "1"), hold(sc, kPut, "95", "-1"), hold(sc, kCall, "105", "-1"),
                                           hold(sc, kCall, "110", "1")};
  CHECK(initial_of(sc, condor) == M("5"));
  CHECK(initial_of(naked, condor) > M("10"));
  const std::vector<detail::ShortRequirement> per = detail::short_requirements(sc, condor, 0, P("100"));
  REQUIRE(per.size() == 2);
  CHECK(per[0].maintenance.raw + per[1].maintenance.raw == M("5").raw);
}

TEST_CASE("Spread margin: the unpaired remainder is margined per leg, pairing ignores the order of positions") {
  const Scenario sc = spread_scenario(true);
  const Money one_naked = initial_of(sc, {hold(sc, kCall, "105", "-1")});
  CHECK(initial_of(sc, {hold(sc, kCall, "105", "-2"), hold(sc, kCall, "110", "1")}) == Money::from_raw(M("5").raw + one_naked.raw));
  std::vector<detail::Holding> a{hold(sc, kCall, "100", "1"), hold(sc, kCall, "105", "-1"), hold(sc, kCall, "110", "-1"),
                                 hold(sc, kPut, "95", "-1"), hold(sc, kPut, "90", "2")};
  std::vector<detail::Holding> b(a.rbegin(), a.rend());
  CHECK(initial_of(sc, a) == initial_of(sc, b));
  // Overlapping short strikes (puts above calls) keep both sides: no condor reduction.
  const Money both = initial_of(sc, {hold(sc, kPut, "110", "-1"), hold(sc, kPut, "105", "1"), hold(sc, kCall, "95", "-1"),
                                     hold(sc, kCall, "100", "1")});
  CHECK(both == M("10"));
}

TEST_CASE("Spread margin: a sale's requirement counts this step's working buys; flag off keeps per-leg margin") {
  const Scenario sc = spread_scenario(true);
  Driver dr(sc);
  REQUIRE(dr.option(id_of(sc, kCall, "110"), kBuy, "1").accepted);
  const OptionSeries& c105 = *find_series(sc, id_of(sc, kCall, "105"));
  CHECK(detail::option_order_requirement(sc, dr.state(), c105, kSell, Q("1")) == M("5"));  // paired with the working buy
  REQUIRE(dr.option(c105.id, kSell, "1").accepted);
  dr.step();
  CHECK(dr.state().option_positions.size() == 2);
  CHECK(dr.view().option_initial_margin == M("5"));
  CHECK(detail::option_order_requirement(sc, dr.state(), c105, kSell, Q("1")) > M("5"));  // no free long left

  const Scenario off = spread_scenario(false);
  Driver dn(off);
  REQUIRE(dn.option(id_of(off, kCall, "110"), kBuy, "1").accepted);
  REQUIRE(dn.option(id_of(off, kCall, "105"), kSell, "1").accepted);
  dn.step();
  CHECK(dn.view().option_initial_margin > M("10"));
}

TEST_CASE("Spread margin: a cover that does not fill leaves the short margined naked, cancelled insufficient_margin") {
  const Scenario sc = spread_scenario(true, "8");
  Driver dr(sc);
  REQUIRE(dr.option(id_of(sc, kCall, "102"), kBuy, "1", "0.01").accepted);  // a limit far below the ask
  const CommandResult s = dr.option(id_of(sc, kCall, "101"), kSell, "1");
  REQUIRE(s.accepted);  // paired with the working buy at acceptance: requirement 1
  dr.step();
  CHECK(dr.state().option_positions.empty());
  bool cancelled = false;
  for (const Event& e : dr.state().events) {
    if (e.order_id == s.order_id && e.status == OrderStatus::Cancelled) cancelled = e.reason == Reason::InsufficientMargin;
  }
  CHECK(cancelled);
}

TEST_CASE("Combo dry run: every template matches real placement leg by leg in m05, m10 and a spread-margin market") {
  std::vector<std::pair<Scenario, std::uint32_t>> markets;
  for (const char* id : {"m05-options-writer", "m10-hedged-book"}) {
    Scenario sc;
    REQUIRE(build_mission(id, 9, sc));
    markets.emplace_back(sc, 2);
  }
  markets.emplace_back(spread_scenario(true, "60"), 0);
  std::size_t legs_checked = 0;
  for (auto& [sc, skip] : markets) {
    RunState st = start_run(sc, 9);
    for (std::uint32_t i = 0; i < skip; ++i) advance(sc, st);
    for (const Template t : kTemplates) {
      for (const char* qty : {"1", "40"}) {
        const std::vector<ComboLeg> legs = template_legs(sc, st, t, qty);
        REQUIRE_FALSE(legs.empty());
        const RunState before = st;
        const ComboPreview p = combo_preview(sc, st, legs);
        CHECK(state_digest(st) == state_digest(before));
        CHECK(events_digest(st) == events_digest(before));
        RunState real = st;
        std::uint64_t seq = real.last_command_seq;
        REQUIRE(p.legs.size() == legs.size());
        for (std::size_t i = 0; i < legs.size(); ++i) {
          Command c;
          c.type = legs[i].perp ? CommandType::PlaceOrder : CommandType::PlaceOptionOrder;
          c.seq = ++seq;
          c.client_id = 1'000'000 + seq;
          c.decision_step = real.step;
          c.side = legs[i].side;
          c.qty = legs[i].qty;
          c.series = legs[i].series;
          const CommandResult r = apply_command(sc, real, c);
          CHECK(p.legs[i].accepted == r.accepted);
          CHECK(p.legs[i].reason == r.reason);
          CHECK(p.legs[i].available_after == account_view(sc, real).available);
          ++legs_checked;
        }
      }
    }
  }
  CHECK(legs_checked > 50);
}

TEST_CASE("Combo dry run: a perpetual leg reserves margin; after-fill estimate pairs the spread") {
  const Scenario sc = spread_scenario(true);
  const RunState st = start_run(sc, 1);
  const std::vector<ComboLeg> covered = template_legs(sc, st, Template::CoveredCall, "2");
  const ComboPreview p = combo_preview(sc, st, covered);
  REQUIRE(p.accepted);
  CHECK(p.legs[0].required > Money{});
  CHECK(p.legs[0].quote == P("100"));
  CHECK(p.net_premium > Money{});  // the call is sold: a credit
  const ComboPreview condor = combo_preview(sc, st, template_legs(sc, st, Template::IronCondor, "1"));
  REQUIRE(condor.accepted);
  CHECK(condor.option_margin_after == M("5"));
  CHECK(condor.legs[2].required <= M("5"));
}
