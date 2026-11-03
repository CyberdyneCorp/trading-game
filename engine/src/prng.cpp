#include "tradelab/prng.hpp"

#include "tradelab/det_math.hpp"

namespace tradelab {
namespace {
constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;
constexpr double kTwoPi = 6.28318530717958647693;
constexpr double kInv2Pow53 = 1.0 / 9007199254740992.0;  // 2^-53, exact
}  // namespace

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream) : state_(0), inc_((stream << 1U) | 1U) {
  next_u32();
  state_ += seed;
  next_u32();
}

Pcg32 Pcg32::from_state(std::uint64_t state, std::uint64_t inc) {
  Pcg32 g;
  g.state_ = state;
  g.inc_ = inc | 1U;
  return g;
}

std::uint32_t Pcg32::next_u32() {
  const std::uint64_t old = state_;
  state_ = old * kMultiplier + inc_;
  const auto xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
  const auto rot = static_cast<std::uint32_t>(old >> 59U);
  return (xorshifted >> rot) | (xorshifted << ((32U - rot) & 31U));
}

std::uint32_t Pcg32::next_bounded(std::uint32_t bound) {
  // Rejection sampling without modulo bias (PCG reference).
  const std::uint32_t threshold = (0U - bound) % bound;
  for (;;) {
    const std::uint32_t r = next_u32();
    if (r >= threshold) return r % bound;
  }
}

double Pcg32::next_unit() {
  const std::uint64_t hi = next_u32() >> 5U;  // 27 bits
  const std::uint64_t lo = next_u32() >> 6U;  // 26 bits
  return static_cast<double>((hi << 26U) | lo) * kInv2Pow53;
}

double Pcg32::next_unit_open() {
  const std::uint64_t hi = next_u32() >> 5U;
  const std::uint64_t lo = next_u32() >> 6U;
  return (static_cast<double>((hi << 26U) | lo) + 0.5) * kInv2Pow53;
}

double Pcg32::next_normal() {
  const double u1 = next_unit_open();
  const double u2 = next_unit();
  return det::sqrt(-2.0 * det::log(u1)) * det::cos(kTwoPi * u2);
}

}  // namespace tradelab
