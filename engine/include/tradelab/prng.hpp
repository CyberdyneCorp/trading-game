#pragma once

// Deterministic PRNG (design D4): PCG32 (XSH-RR, O'Neill 2014) with explicit streams.
// Never use <random> distributions in the engine: their algorithms are implementation-defined.

#include <cstdint>

namespace tradelab {

// Stream ids are part of the execution policy: never renumber.
enum class Stream : std::uint64_t {
  Market = 1,    // synthetic price/volume generation
  Events = 2,    // event reaction sampling
  Slippage = 3,  // execution noise
  Sandbox = 4,   // sandbox scenario variants
  Schedule = 5,  // per-seed timing of scripted segments and events
  Regime = 6,    // Markov regime chain path (generator v3, G1)
  Outcome = 7,   // drawn event actuals and intensities (generator v4, G2)
};

class Pcg32 {
 public:
  Pcg32(std::uint64_t seed, std::uint64_t stream);
  Pcg32(std::uint64_t seed, Stream stream) : Pcg32(seed, static_cast<std::uint64_t>(stream)) {}

  static Pcg32 from_state(std::uint64_t state, std::uint64_t inc);

  std::uint32_t next_u32();
  std::uint32_t next_bounded(std::uint32_t bound);  // uniform in [0, bound), bound > 0
  double next_unit();                               // uniform in [0, 1), 53 random bits
  double next_unit_open();                          // uniform in (0, 1)
  double next_normal();                             // standard normal via Box-Muller (det kernels)

  [[nodiscard]] std::uint64_t state() const { return state_; }
  [[nodiscard]] std::uint64_t inc() const { return inc_; }

 private:
  Pcg32() = default;
  std::uint64_t state_ = 0;
  std::uint64_t inc_ = 0;
};

}  // namespace tradelab
