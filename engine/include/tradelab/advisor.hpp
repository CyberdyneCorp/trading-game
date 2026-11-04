#pragma once

// The advisor (spec advisor; design D2 of add-advisor-intel-tutorials): a rule-based, honest read
// of the market at the current decision step. It reads only what the player can see at that step:
// closed bars up to it, released news (plus headlines an active fast wire delivers early), the
// published funding rate and candle patterns ending at or before it. Never fair value, segments,
// scripted reactions or any later bar.
//
// Each reason is a signed vote of fixed weight (trend, MACD, RSI and band fades 1; a pattern 1, or
// 2 in context; an early headline 2). Some reasons vote 0: the range regime (it only turns on the
// fades), crowded funding (a warning), and "news already priced" and a volatility squeeze, which
// each pull the sum one step toward wait. Long when the sum is >= +2, short when <= -2, else wait.
// Confidence is 5000 + 400 * |sum| basis points, capped at 6500: the advisor never claims to know.
// The stop is 2 x ATR(14) from the mark; the size risks 1% of equity at it (halved in a squeeze).
// A pattern is "in context" when it reverses the move that led into it (a hammer after a decline)
// and, in a trend, points the trend's way.

#include <cstdint>

#include "tradelab/engine.hpp"

namespace tradelab {

constexpr std::uint32_t kAdvisorVersion = 1;
constexpr std::uint32_t kAdviceHorizon = 5;          // debrief and realism: outcome 5 bars after the next open
constexpr std::uint32_t kAdviceConfidenceCap = 6500;  // basis points

Advice advise(const Scenario& sc, const RunState& st);

}  // namespace tradelab
