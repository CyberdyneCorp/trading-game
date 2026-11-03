#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <initializer_list>
#include <string_view>

#include "tradelab/candle_patterns.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/indicators.hpp"

using namespace tradelab;

namespace {

using Ohlc = std::initializer_list<double>;

Bar bar(double o, double h, double l, double c) {
  const auto p = [](double v) { return Price::from_raw(static_cast<std::int64_t>(v * Price::kScale)); };
  return {p(o), p(h), p(l), p(c), Qty::from_raw(Qty::kScale), false};
}

// 15 quiet bars around 60, then a 5-bar trend (down or up), then the pattern bars.
Scenario with_context(int trend, std::initializer_list<Ohlc> pattern) {
  Scenario s;
  for (int i = 0; i < 15; ++i) s.bars.push_back(i % 2 ? bar(61, 62, 59, 60) : bar(60, 62, 59, 61));
  double close = 60;
  for (int i = 0; i < 5; ++i) {
    const double next = close + trend * 3.0;
    s.bars.push_back(bar(close, std::max(close, next) + 0.5, std::min(close, next) - 0.5, next));
    close = next;
  }
  for (const Ohlc& c : pattern) {
    const auto* v = c.begin();
    s.bars.push_back(bar(v[0], v[1], v[2], v[3]));
  }
  return s;
}

bool found(const Scenario& s, std::string_view id) {
  const auto last = static_cast<std::uint32_t>(s.bars.size() - 1);
  const auto hits = detect_patterns(s, last);
  return std::any_of(hits.begin(), hits.end(), [&](const PatternHit& h) { return h.id == id && h.step == last; });
}

}  // namespace

// Shapes are the client cheat sheet's drawings: the reference and the detector must agree.
TEST_CASE("Cheat-sheet shapes are detected in their usual context") {
  CHECK(found(with_context(-1, {{40, 46, 8, 45}}), "hammer"));
  CHECK(found(with_context(1, {{62, 92, 57, 58}}), "shooting-star"));
  CHECK(found(with_context(-1, {{42, 44, 30, 32}, {29, 52, 27, 50}}), "bullish-engulfing"));
  CHECK(found(with_context(1, {{60, 72, 58, 70}, {73, 75, 52, 54}}), "bearish-engulfing"));
  CHECK(found(with_context(-1, {{50, 52, 30, 32}, {26, 46, 24, 44}}), "piercing-line"));
  CHECK(found(with_context(-1, {{60, 62, 34, 36}, {30, 34, 24, 28}, {33, 58, 31, 56}}), "morning-star"));
  CHECK(found(with_context(1, {{44, 70, 42, 68}, {74, 80, 72, 76}, {70, 72, 44, 46}}), "evening-star"));
  CHECK(found(with_context(-1, {{22, 38, 20, 36}, {32, 52, 30, 50}, {46, 66, 44, 64}}), "three-white-soldiers"));
  CHECK(found(with_context(-1, {{60, 62, 30, 32}, {36, 46, 34, 44}, {44, 66, 42, 64}}), "three-inside-up"));
  CHECK(found(with_context(-1, {{46, 48, 38, 40}, {37, 56, 35, 54}, {54, 68, 52, 66}}), "three-outside-up"));
  CHECK(found(with_context(-1, {{60, 62, 38, 40}, {31, 34, 28, 31}, {40, 60, 39, 58}}), "bullish-abandoned-baby"));
  CHECK(found(with_context(1, {{30, 52, 28, 50}, {48, 49, 43, 44}, {45, 46, 40, 41}, {42, 44, 37, 38}, {40, 62, 39, 60}}),
              "rising-three-methods"));
  CHECK(found(with_context(-1, {{70, 72, 48, 50}, {52, 57, 51, 56}, {55, 60, 54, 59}, {58, 63, 57, 62}, {60, 61, 36, 38}}),
              "falling-three-methods"));
}

TEST_CASE("The same shape means different things in different contexts") {
  const Scenario after_rally = with_context(1, {{40, 46, 8, 45}});
  CHECK_FALSE(found(after_rally, "hammer"));
  CHECK(found(after_rally, "hanging-man"));
  CHECK_FALSE(found(with_context(0, {{42, 44, 30, 32}, {29, 52, 27, 50}}), "bullish-engulfing"));  // no prior decline
}

TEST_CASE("Quiet bars produce no reversal patterns") {
  const Scenario s = with_context(0, {{60, 62, 59, 61}, {61, 62, 59, 60}});
  for (const PatternHit& h : detect_patterns(s, static_cast<std::uint32_t>(s.bars.size() - 1))) CHECK(h.bias == 0);
}

TEST_CASE("Patterns at t use only bars up to t") {
  Scenario s = with_context(-1, {{40, 46, 8, 45}});
  const auto t = static_cast<std::uint32_t>(s.bars.size() - 1);
  const auto before = detect_patterns(s, t);
  s.bars.push_back(bar(45, 90, 1, 2));  // a wild next bar
  const auto after = detect_patterns(s, t);
  REQUIRE(before.size() == after.size());
  for (std::size_t k = 0; k < before.size(); ++k) CHECK(before[k].id == after[k].id);
}

// The cheat sheet and README quote these hit rates; keep them true for our markets.
// A hit = the close 5 bars after the pattern moved in the pattern's direction. 300 markets.
struct HitRate {
  int n = 0;
  int wins = 0;
  [[nodiscard]] double rate() const { return static_cast<double>(wins) / n; }
};

enum class Filter : std::uint8_t { All, RsiExtreme, Kicker };

bool rsi_extreme(const Series& r, const PatternHit& h) {
  const std::optional<Price>& v = r[h.step];
  if (!v.has_value()) return false;
  const std::int64_t level = h.bias > 0 ? 30 : 70;
  return h.bias > 0 ? v->raw < level * Price::kScale : v->raw > level * Price::kScale;
}

bool keep(Filter f, const Series& r, const PatternHit& h) {
  if (f == Filter::RsiExtreme) return rsi_extreme(r, h);
  if (f == Filter::Kicker) return h.id.ends_with("kicker");
  return true;
}

HitRate pattern_hit_rate(Filter f) {
  HitRate out;
  for (const MissionInfo& m : mission_catalog()) {
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
      Scenario s;
      REQUIRE(build_mission(m.id, seed, s));
      const auto last = static_cast<std::uint32_t>(s.bars.size() - 1);
      const Series r = rsi(s, last, 14);
      for (const PatternHit& h : detect_patterns(s, last)) {
        if (h.bias == 0 || h.step + 5 > last || !keep(f, r, h)) continue;
        const bool up = s.bars[h.step + 5].close > s.bars[h.step].close;
        ++out.n;
        out.wins += (h.bias > 0) == up ? 1 : 0;
      }
    }
  }
  return out;
}

TEST_CASE("Calibration: isolated candle patterns are barely better than a coin flip") {
  const HitRate h = pattern_hit_rate(Filter::All);
  MESSAGE("pattern hit rate over 5 bars: " << h.rate() << " (n=" << h.n << ")");
  CHECK(h.n > 1000);
  CHECK(h.rate() > 0.45);
  CHECK(h.rate() < 0.60);
}

TEST_CASE("Calibration: an extreme RSI adds little to a candle pattern") {
  const HitRate h = pattern_hit_rate(Filter::RsiExtreme);
  MESSAGE("pattern + RSI extreme hit rate: " << h.rate() << " (n=" << h.n << ")");
  CHECK(h.n > 100);
  CHECK(h.rate() > 0.45);
  CHECK(h.rate() < 0.62);
}

TEST_CASE("Calibration: kickers carry news information") {
  const HitRate h = pattern_hit_rate(Filter::Kicker);
  MESSAGE("kicker hit rate: " << h.rate() << " (n=" << h.n << ")");
  CHECK(h.n > 20);
  CHECK(h.rate() > 0.60);
}
