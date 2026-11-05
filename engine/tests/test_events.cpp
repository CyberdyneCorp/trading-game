// G2 drawn event outcomes and intensity, G3 announced events (design D8.1; spec market-generator
// "Drawn event outcomes and intensity", "Announced events"; spec advisor EventAhead).

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <string>

#include "../src/c_api_run.hpp"
#include "tradelab/advisor.hpp"
#include "tradelab/c_api.h"
#include "tradelab/generator.hpp"

using namespace tradelab;

namespace {

constexpr std::uint64_t kSeeds = 2000;

Scenario gen(const GeneratorSpec& spec, std::uint64_t seed) {
  Scenario s;
  s.contract.tick_size = Price::from_raw(Price::kScale / 100);
  generate_market(spec, seed, s);
  return s;
}

double px(Price p) { return static_cast<double>(p.raw) / Price::kScale; }

// A 120-bar range with one rate decision at bar 80 (consensus 0).
GeneratorSpec one_release(const ScriptedEvent& ev) {
  GeneratorSpec spec;
  spec.segments = {{.bars = 120, .vol = 0.005, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.02}};
  spec.events = {ev};
  return spec;
}

ScriptedEvent decision(std::array<std::uint32_t, 5> weights, double lo = 1.0, double hi = 1.0) {
  ScriptedEvent ev;
  ev.item = {80, NewsKind::RateDecision, 0, 100, 0, 0, 20};
  ev.p_none = 0.2;
  ev.p_align = 0.6;
  ev.fair_impact = 0.03;
  ev.actual_weights = weights;
  ev.intensity_min = lo;
  ev.intensity_max = hi;
  return ev;
}

// A run over a hand-built scenario, driven through the C ABI's state view.
struct ViewRun {
  tl_run run;
  ViewRun(const Scenario& sc, std::uint64_t seed, RunOptions options = {}) {
    run.scenario = sc;
    run.state = start_run(sc, seed, options);
  }
  std::string state() { return tl_state(&run); }
  std::string advance(std::int32_t n) { return tl_advance(&run, n); }
};

Scenario release_scenario(const ScriptedEvent& ev, std::uint64_t seed) {
  Scenario sc = gen(one_release(ev), seed);
  sc.id = "test";
  sc.contract.multiplier = Qty::from_raw(Qty::kScale);
  sc.contract.lot_size = Qty::from_raw(Qty::kScale / 10);
  sc.contract.min_qty = sc.contract.lot_size;
  sc.contract.max_leverage = 10;
  sc.contract.maintenance_margin = Rate::from_raw(50'000'000);
  sc.costs.capacity_fraction = Rate::from_raw(1'000'000'000);
  sc.mission.initial_balance = Money::from_raw(10'000 * Money::kScale);
  sc.mission.leverage_cap = 5;
  sc.mission.warmup_bars = 40;
  sc.options.iv_base.clear();
  REQUIRE(validate(sc) == ScenarioError::Ok);
  return sc;
}

// The state view without its digests (which hash the whole run, identical anyway).
std::string view_at(const Scenario& sc, std::uint32_t step) {
  ViewRun r(sc, 5);
  const std::string s = step == sc.mission.warmup_bars ? r.state() : r.advance(static_cast<std::int32_t>(step - sc.mission.warmup_bars));
  return s;
}

}  // namespace

TEST_CASE("G2: drawn actuals follow their weights over 2,000 seeds, intensity stays in its range") {
  const ScriptedEvent ev = decision({100, 200, 400, 200, 100}, 0.6, 1.4);
  std::array<int, 5> seen{};
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const EventOutcome o = draw_outcome(ev, seed, 0);
    REQUIRE(o.actual >= -2);
    REQUIRE(o.actual <= 2);
    seen[static_cast<std::size_t>(o.actual + 2)] += 1;
    CHECK(o.intensity_pct >= 60);
    CHECK(o.intensity_pct <= 140);
  }
  for (std::size_t k = 0; k < 5; ++k) {
    const double observed = 1000.0 * seen[k] / kSeeds;
    CAPTURE(k);
    CHECK(std::abs(observed - ev.actual_weights[k]) <= 30.0);
  }
}

TEST_CASE("G2: an event that declares nothing keeps its actual, 100% intensity and its bars") {
  ScriptedEvent plain = decision({});
  plain.item.actual = 1;
  const EventOutcome o = draw_outcome(plain, 9, 0);
  CHECK(o.actual == 1);
  CHECK(o.intensity_pct == 100);
  // Declaring a weight only on the declared actual draws on its own stream: the market is unchanged.
  ScriptedEvent same = plain;
  same.actual_weights = {0, 0, 0, 1000, 0};
  for (std::uint64_t seed : {1U, 2U, 77U}) {
    const Scenario a = gen(one_release(plain), seed);
    const Scenario b = gen(one_release(same), seed);
    for (std::size_t i = 0; i < a.bars.size(); ++i) CHECK(a.bars[i].close == b.bars[i].close);
    CHECK(b.news.front().actual == 1);
  }
}

TEST_CASE("G2: the drawn actual is in the headline; a drawn surprise of 0 does not move the fair value") {
  const ScriptedEvent strong = decision({0, 0, 0, 0, 1000});
  CHECK(gen(one_release(strong), 3).news.front().actual == 2);
  CHECK(gen(one_release(strong), 3).news.front().tone == 1);
  // An incident declared with a negative tone that drew no damage reads neutral.
  ScriptedEvent harmless = decision({0, 0, 1000, 0, 0});
  harmless.item.kind = NewsKind::ExchangeIncident;
  harmless.item.tone = -1;
  CHECK(gen(one_release(harmless), 3).news.front().tone == 0);
  // Expected outcome: the same market as an event with no fair-value impact at all.
  ScriptedEvent expected = decision({0, 0, 1000, 0, 0});
  ScriptedEvent no_impact = expected;
  no_impact.fair_impact = 0.0;
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const Scenario a = gen(one_release(expected), seed);
    const Scenario b = gen(one_release(no_impact), seed);
    for (std::size_t i = 0; i < a.bars.size(); ++i) CHECK(a.bars[i].close == b.bars[i].close);
  }
}

TEST_CASE("G2: same headline, larger intensity, larger fair-value shift and opening gap") {
  int larger = 0;
  for (std::uint64_t seed = 1; seed <= 40; ++seed) {
    const Scenario small = gen(one_release(decision({0, 0, 0, 0, 1000}, 0.5, 0.5)), seed);
    const Scenario big = gen(one_release(decision({0, 0, 0, 0, 1000}, 1.5, 1.5)), seed);
    const double gap_small = std::log(px(small.bars[81].open) / px(small.bars[80].close));
    const double gap_big = std::log(px(big.bars[81].open) / px(big.bars[80].close));
    CHECK(small.bars[80].close == big.bars[80].close);  // identical up to the release
    larger += gap_big > gap_small ? 1 : 0;
  }
  CHECK(larger == 40);
}

TEST_CASE("G3: an event released at 90 and announced 20 bars ahead is upcoming from step 70, not 69") {
  ScriptedEvent ev = decision({100, 200, 400, 200, 100}, 0.6, 1.4);
  ev.item.release_step = 90;
  const Scenario sc = release_scenario(ev, 4);
  const std::string item = R"("upcoming":[{"step":90,"kind":"rate_decision","expectation":0}])";
  CHECK(view_at(sc, 69).find(R"("upcoming":[])") != std::string::npos);
  CHECK(view_at(sc, 70).find(item) != std::string::npos);
  CHECK(view_at(sc, 89).find(item) != std::string::npos);
  const std::string released = view_at(sc, 90);
  CHECK(released.find(R"("upcoming":[])") != std::string::npos);
  CHECK(released.find(R"("kind":"rate_decision","tone")") != std::string::npos);
}

TEST_CASE("G3: changing an announced event's actual, tone or credibility changes nothing visible before release") {
  ScriptedEvent ev = decision({});
  ev.item = {90, NewsKind::MacroData, 1, 80, 1, 1, 20};
  const Scenario base = release_scenario(ev, 6);
  for (int variant = 0; variant < 3; ++variant) {
    ScriptedEvent other = ev;
    if (variant == 0) other.item.actual = -2;
    if (variant == 1) other.item.tone = -1;
    if (variant == 2) other.item.credibility = 20;
    const Scenario alt = release_scenario(other, 6);
    for (std::uint32_t step : {40U, 69U, 70U, 80U, 89U}) {
      CAPTURE(variant);
      CAPTURE(step);
      CHECK(view_at(base, step) == view_at(alt, step));
    }
    CHECK(view_at(base, 90) != view_at(alt, 90));
  }
}

TEST_CASE("G3: the bars never depend on the announcement lead") {
  ScriptedEvent announced = decision({100, 200, 400, 200, 100}, 0.6, 1.4);
  ScriptedEvent silent = announced;
  silent.item.announce_lead = 0;
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const Scenario a = gen(one_release(announced), seed);
    const Scenario b = gen(one_release(silent), seed);
    for (std::size_t i = 0; i < a.bars.size(); ++i) {
      CHECK(a.bars[i].close == b.bars[i].close);
      CHECK(a.funding_rate[i] == b.funding_rate[i]);
    }
  }
}

TEST_CASE("G3: the advisor moves toward wait within 3 bars of an announced release, never before it is announced") {
  ScriptedEvent ev = decision({});
  ev.item.release_step = 90;
  const Scenario sc = release_scenario(ev, 8);
  auto reasons_at = [&](std::uint32_t step) {
    RunState st = start_run(sc, 1);
    st.step = step;
    int ahead = 0;
    for (const AdviceReason& r : advise(sc, st).reasons) ahead += r.code == AdviceReasonCode::EventAhead ? 1 : 0;
    return ahead;
  };
  CHECK(reasons_at(86) == 0);
  CHECK(reasons_at(87) == 1);
  CHECK(reasons_at(89) == 1);
  CHECK(reasons_at(90) == 0);
  ScriptedEvent unannounced = ev;
  unannounced.item.announce_lead = 0;
  const Scenario quiet = release_scenario(unannounced, 8);
  RunState st = start_run(quiet, 1);
  st.step = 88;
  for (const AdviceReason& r : advise(quiet, st).reasons) CHECK(r.code != AdviceReasonCode::EventAhead);
}
