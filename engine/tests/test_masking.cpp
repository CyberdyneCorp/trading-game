#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "tradelab/pack.hpp"
#include "tradelab/prng.hpp"

using namespace tradelab;

namespace {

constexpr Price kTick = Price::from_raw(Price::kScale / 100);  // 0.01
constexpr double kHalfTick = 0.005;

double px(Price p) { return static_cast<double>(p.raw) / Price::kScale; }

// A random window: price level from 0.5 to ~100,000, random-walk closes, wicks, volumes.
std::vector<Bar> random_window(std::uint64_t seed) {
  Pcg32 rng(seed, 99);
  const double level = 0.5 * std::pow(10.0, rng.next_unit() * 5.3);
  const std::int64_t grid = std::int64_t{1} << rng.next_bounded(12);  // raw granularity 1e-8 .. ~4e-5
  const auto snap = [grid](double v) { return Price::from_raw(std::max<std::int64_t>(grid, static_cast<std::int64_t>(v * 1e8) / grid * grid)); };
  std::vector<Bar> bars;
  double close = level;
  for (std::uint32_t i = 0; i < 200; ++i) {
    const double open = close * (1.0 + (rng.next_unit() - 0.5) * 0.004);
    close = open * (1.0 + (rng.next_unit() - 0.5) * 0.03);
    const double high = std::max(open, close) * (1.0 + rng.next_unit() * 0.01);
    const double low = std::min(open, close) * (1.0 - rng.next_unit() * 0.01);
    Bar b{snap(open), snap(high), snap(low), snap(close), Qty::from_raw(static_cast<std::int64_t>(rng.next_unit() * 1e12)), false};
    b.high = std::max({b.high, b.open, b.close});
    b.low = std::min({b.low, b.open, b.close});
    bars.push_back(b);
  }
  return bars;
}

void check_window(const std::vector<Bar>& in, const std::vector<Bar>& out, std::uint32_t ref) {
  const i128 ref_raw = in[ref].open.raw;
  CHECK(out[ref].open == Price::from_raw(100 * Price::kScale));  // first playable open = 100
  for (std::size_t i = 0; i < in.size(); ++i) {
    // Each price is within half a tick of the exact rebased value (declared rounding: half-even to tick).
    for (const auto& [p, m] : {std::pair{in[i].open, out[i].open}, {in[i].high, out[i].high}, {in[i].low, out[i].low}, {in[i].close, out[i].close}}) {
      const i128 err = static_cast<i128>(m.raw) * ref_raw - static_cast<i128>(p.raw) * 100 * Price::kScale;
      REQUIRE((err < 0 ? -err : err) * 2 <= static_cast<i128>(kTick.raw) * ref_raw);
    }
    CHECK(out[i].high >= std::max(out[i].open, out[i].close));
    CHECK(out[i].low <= std::min(out[i].open, out[i].close));
  }
}

// Bar-to-bar returns, wick-to-body ratios and notional volume survive within the rounding bound.
void check_geometry(const std::vector<Bar>& in, const std::vector<Bar>& out) {
  for (std::size_t i = 1; i < in.size(); ++i) {
    const double r = px(in[i].close) / px(in[i - 1].close);
    const double rm = px(out[i].close) / px(out[i - 1].close);
    const double bound = kHalfTick * (1.0 + r) / (px(out[i - 1].close) - kHalfTick) + 1e-12;
    CHECK(std::fabs(rm - r) <= bound);
  }
  for (std::size_t i = 0; i < in.size(); ++i) {
    const double body = std::fabs(px(in[i].close) - px(in[i].open));
    const double body_m = std::fabs(px(out[i].close) - px(out[i].open));
    if (body_m < 50 * px(kTick)) continue;  // ratios of near-doji bodies are not meaningful at tick size
    const double w = (px(in[i].high) - std::max(px(in[i].open), px(in[i].close))) / body;
    const double wm = (px(out[i].high) - std::max(px(out[i].open), px(out[i].close))) / body_m;
    CHECK(std::fabs(wm - w) <= 2 * kHalfTick * (1.0 + w) / (body_m - 2 * kHalfTick) + 1e-9);
    const double notional = px(in[i].open) * static_cast<double>(in[i].volume.raw);
    const double notional_m = px(out[i].open) * static_cast<double>(out[i].volume.raw);
    if (notional > 1e6) CHECK(std::fabs(notional_m / notional - 1.0) <= kHalfTick / (px(out[i].open) - kHalfTick) + 1e-6);
  }
}

}  // namespace

TEST_CASE("masking: geometry is preserved within the declared rounding (property, 200 windows)") {
  for (std::uint64_t seed = 1; seed <= 200; ++seed) {
    const auto in = random_window(seed);
    const auto ref = static_cast<std::uint32_t>(seed % 80);
    std::vector<Bar> out;
    REQUIRE(mask_bars(in, ref, kTick, &out));
    REQUIRE(out.size() == in.size());
    check_window(in, out, ref);
    check_geometry(in, out);
  }
}

TEST_CASE("masking: the factor never reads data after the first decision point") {
  auto in = random_window(7);
  std::vector<Bar> a;
  std::vector<Bar> b;
  REQUIRE(mask_bars(in, 60, kTick, &a));
  for (std::size_t i = 61; i < in.size(); ++i) in[i].close = in[i].high;  // rewrite the future
  REQUIRE(mask_bars(in, 60, kTick, &b));
  for (std::size_t i = 0; i <= 60; ++i) CHECK(a[i].close == b[i].close);
}

TEST_CASE("masking: refuses prices that would round to zero and bad references") {
  std::vector<Bar> in(3, Bar{Price::from_raw(100'000 * Price::kScale), Price::from_raw(100'000 * Price::kScale),
                             Price::from_raw(100'000 * Price::kScale), Price::from_raw(100'000 * Price::kScale), Qty{}, false});
  in[2].low = Price::from_raw(1);  // 1e-8 against a 100,000 reference: below half a tick after masking
  std::vector<Bar> out;
  CHECK_FALSE(mask_bars(in, 0, kTick, &out));
  CHECK_FALSE(mask_bars(in, 3, kTick, &out));
}
