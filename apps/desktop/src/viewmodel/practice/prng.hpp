// Port of apps/web/src/lib/game/prng.ts (names kept; web parity fixtures drills.json, daily.json and
// guess.json): the deterministic draws of the practice formats (add-practice-and-advanced-strategies
// D15, D16, D19). FNV-1a 32 of a text seeds mulberry32, whose outputs are unsigned 32-bit integers; the
// web computes them with Math.imul and >>> 0, so uint32_t arithmetic gives the same numbers.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace tradelab::desktop::practice {

// FNV-1a 32 of the UTF-8 bytes of `text`.
std::uint32_t fnv1a32(std::string_view text);

// mulberry32: each call returns the next unsigned 32-bit output.
class Mulberry32 {
 public:
  explicit Mulberry32(std::uint32_t seed) : a_(seed) {}
  std::uint32_t operator()();

 private:
  std::uint32_t a_;
};

// An index in [0, n): the next output modulo n (n >= 1).
inline int pick(Mulberry32& next, int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }

// Whole days from 1970-01-01 to a civil date (proleptic Gregorian; H. Hinnant's days_from_civil).
std::int64_t days_from_civil(std::int64_t y, int m, int d);
// Whole civil days from `from` to `to` (both "YYYY-MM-DD"); none for a malformed date.
std::optional<std::int64_t> days_between(std::string_view from, std::string_view to);

}  // namespace tradelab::desktop::practice
