// Generator v2 calibration (spec market-generator "Readable market structure"): technical and
// fundamental reads get a small, probabilistic edge -- never a certainty -- and a structure-free
// random walk gives none. Bands are wide enough to be seed-stable and tight enough to catch a
// regression that makes the market either unreadable or predictable.

#include <doctest/doctest.h>

#include "tradelab/generator.hpp"
#include "tradelab/indicators.hpp"

using namespace tradelab;

namespace {

constexpr std::uint64_t kSeeds = 200;

struct Rate01 {
  int n = 0;
  int hits = 0;
  void add(bool hit) {
    ++n;
    hits += hit ? 1 : 0;
  }
  [[nodiscard]] double p() const { return n > 0 ? static_cast<double>(hits) / n : 0.0; }
};

double px(const Scenario& s, std::size_t i) { return static_cast<double>(s.bars[i].close.raw) / Price::kScale; }
double val(const std::optional<Price>& v) { return v ? static_cast<double>(v->raw) / Price::kScale : 0.0; }

Scenario gen(const GeneratorSpec& spec, std::uint64_t seed) {
  Scenario s;
  s.contract.tick_size = Price::from_raw(Price::kScale / 100);
  generate_market(spec, seed, s);
  return s;
}

struct RsiEdge {
  Rate01 oversold_up;      // RSI < 30 -> higher 5 bars later
  Rate01 overbought_down;  // RSI > 70 -> lower 5 bars later
};

RsiEdge rsi_edge(const GeneratorSpec& spec) {
  RsiEdge e;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Scenario s = gen(spec, seed);
    const auto last = static_cast<std::uint32_t>(s.bars.size() - 1);
    const Series r = rsi(s, last, 14);
    for (std::uint32_t t = 30; t + 5 <= last; ++t) {
      const bool up = px(s, t + 5) > px(s, t);
      if (val(r[t]) < 30) e.oversold_up.add(up);
      if (val(r[t]) > 70) e.overbought_down.add(!up);
    }
  }
  return e;
}

GeneratorSpec flat(double fair_pull, Segment seg) {
  GeneratorSpec g;
  g.fair_pull = fair_pull;
  g.squeeze_prob = 0.0;
  g.segments = {seg};
  return g;
}

}  // namespace

TEST_CASE("Calibration: a structure-free random walk gives RSI no edge") {
  const RsiEdge e = rsi_edge(flat(0.0, {.bars = 300, .vol = 0.006}));
  CHECK(e.oversold_up.p() == doctest::Approx(0.5).epsilon(0.1));
  CHECK(e.overbought_down.p() == doctest::Approx(0.5).epsilon(0.1));
}

TEST_CASE("Calibration: ranges reward fading extremes, modestly") {
  const RsiEdge e = rsi_edge(flat(0.006, {.bars = 300, .vol = 0.006, .anchor = Anchor::Level, .reversion = 0.006, .band = 0.03}));
  MESSAGE("range RSI<30 up: " << e.oversold_up.p() << "  RSI>70 down: " << e.overbought_down.p());
  CHECK(e.oversold_up.p() > 0.58);
  CHECK(e.oversold_up.p() < 0.72);
  CHECK(e.overbought_down.p() > 0.58);
  CHECK(e.overbought_down.p() < 0.72);
}

TEST_CASE("Calibration: in an uptrend, buying pullbacks works and shorting strength does not") {
  const GeneratorSpec spec =
      flat(0.006, {.bars = 300, .drift = 0.0006, .vol = 0.007, .anchor = Anchor::Trend, .reversion = 0.03, .momentum = 0.05});
  Rate01 pullback;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Scenario s = gen(spec, seed);
    const auto last = static_cast<std::uint32_t>(s.bars.size() - 1);
    const Series e = ema(s, last, 20);
    for (std::uint32_t t = 30; t + 5 <= last; ++t) {
      if (px(s, t) < val(e[t])) pullback.add(px(s, t + 5) > px(s, t));
    }
  }
  const RsiEdge r = rsi_edge(spec);
  MESSAGE("pullback up: " << pullback.p() << "  short RSI>70: " << r.overbought_down.p());
  CHECK(pullback.p() > 0.57);
  CHECK(pullback.p() < 0.72);
  CHECK(r.overbought_down.p() < 0.57);
}

TEST_CASE("Calibration: a volatility squeeze precedes a larger move whose direction leans on the news") {
  GeneratorSpec base = flat(0.006, {});
  base.segments = {{.bars = 30, .vol = 0.007, .anchor = Anchor::Level, .reversion = 0.05, .band = 0.015, .vol_decay = 0.96},
                   {.bars = 40, .drift = 0.004, .vol = 0.010, .momentum = 0.1, .random_direction = true}};
  const auto up_share = [&](std::int8_t surprise) {
    GeneratorSpec spec = base;
    if (surprise != 0) spec.events = {{{29, NewsKind::RateDecision, 1, 100, 0, surprise}, 0.2, 0.6, 0.003, 4, 1.3, 1.0}};
    Rate01 up;
    double expansion = 0.0;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
      const Scenario s = gen(spec, seed);
      const Series a = atr(s, 69, 14);
      expansion += val(a[69]) / val(a[29]);
      up.add(px(s, 69) > px(s, 30));
    }
    CHECK(expansion / kSeeds > 2.0);  // the squeeze resolves into a bigger move
    return up.p();
  };
  const double none = up_share(std::int8_t{0});
  const double good = up_share(std::int8_t{1});
  const double bad = up_share(std::int8_t{-1});
  MESSAGE("breakout up share: no news " << none << ", good " << good << ", bad " << bad);
  CHECK(none == doctest::Approx(0.5).epsilon(0.16));
  CHECK(good > 0.6);
  CHECK(good < 0.85);  // never certain
  CHECK(bad < 0.4);
  CHECK(bad > 0.15);
}

TEST_CASE("Calibration: a credible surprise moves the fair value; price follows over time") {
  GeneratorSpec spec = flat(0.006, {.bars = 100, .vol = 0.006});
  spec.events = {{{60, NewsKind::RateDecision, 1, 100, 0, 1}, 0.2, 0.6, 0.004, 4, 1.4, 1.0}};
  Rate01 later;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Scenario s = gen(spec, seed);
    later.add(px(s, 85) > px(s, 60));
  }
  MESSAGE("25 bars after a positive surprise, up share: " << later.p());
  CHECK(later.p() > 0.55);
  CHECK(later.p() < 0.8);
  // Expected news (no surprise) leaves the fair value alone.
  spec.events[0].item.expectation = 1;
  Rate01 priced_in;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Scenario s = gen(spec, seed);
    priced_in.add(px(s, 85) > px(s, 60));
  }
  CHECK(priced_in.p() < later.p());
}

TEST_CASE("Calibration: crowded longs paying high funding get squeezed more often") {
  const auto big_drops = [](double funding_bias) {
    GeneratorSpec spec = flat(0.006, {.bars = 200, .vol = 0.006, .funding_bias = funding_bias});
    spec.squeeze_prob = 0.06;
    int drops = 0;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
      const Scenario s = gen(spec, seed);
      for (std::size_t i = 1; i < s.bars.size(); ++i) drops += px(s, i) < px(s, i - 1) * 0.985 ? 1 : 0;
    }
    return drops;
  };
  const int calm = big_drops(0.0);
  const int crowded = big_drops(0.001);
  MESSAGE("bars down >1.5%: calm " << calm << ", crowded longs " << crowded);
  CHECK(crowded > calm * 3);
}

TEST_CASE("Generator v2: bars stay valid and reproducible with every structural feature on") {
  GeneratorSpec spec;
  spec.segments = {{.bars = 40, .vol = 0.006, .anchor = Anchor::Level, .reversion = 0.02, .band = 0.01, .vol_decay = 0.95},
                   {.bars = 40, .drift = 0.003, .vol = 0.012, .jump_prob = 0.1, .jump_mean = 0.03, .random_direction = true},
                   {.bars = 40, .drift = 0.001, .vol = 0.008, .funding_bias = 0.002, .anchor = Anchor::Trend, .reversion = 0.05,
                    .momentum = 0.2}};
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    const Scenario a = gen(spec, seed);
    const Scenario b = gen(spec, seed);
    for (std::size_t i = 0; i < a.bars.size(); ++i) {
      const Bar& bar = a.bars[i];
      CHECK(bar.low.raw > 0);
      CHECK(bar.low <= std::min(bar.open, bar.close));
      CHECK(bar.high >= std::max(bar.open, bar.close));
      CHECK(bar.close == b.bars[i].close);
    }
  }
}
