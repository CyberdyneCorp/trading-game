#pragma once

// Candlestick pattern detection over closed bars (spec indicators "Candle patterns").
// Pure integer comparisons on raw prices. A pattern is reported at the bar that completes it and
// uses only bars up to that one. Context: the "trend before" a pattern compares the close just
// before it with the close five bars earlier, against the average body of the prior 10 bars.
// Size words are relative to the prior 10 bars: "long" body >= average body, "small" <= half of
// the reference candle's body. Ids match the client cheat sheet.

#include <cstdint>
#include <string_view>
#include <vector>

#include "tradelab/scenario.hpp"

namespace tradelab {

constexpr std::uint32_t kPatternVersion = 1;

struct PatternHit {
  std::uint32_t step = 0;  // bar that completes the pattern
  std::string_view id;
  std::uint8_t bars = 1;   // pattern length
  std::int8_t bias = 0;    // +1 bullish, -1 bearish, 0 indecision
};

std::vector<PatternHit> detect_patterns(const Scenario& sc, std::uint32_t last);

}  // namespace tradelab
