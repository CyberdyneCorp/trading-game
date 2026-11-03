#pragma once

// MVP indicators (spec: indicators). Integer fixed-point arithmetic, closed bars only, explicit
// warm-up: a value is absent until the indicator has seen its warm-up number of bars.
//
//   SMA(n): mean of the last n closes; warm-up n.
//   EMA(n): seeded with SMA(n) at bar n-1, then e = e + (close - e) * 2/(n+1); warm-up n.
//   RSI(n): Wilder smoothing of gains/losses seeded with their n-bar mean; warm-up n+1.
//   ATR(n): Wilder smoothing of true range seeded with its n-bar mean; warm-up n+1.

#include <cstdint>
#include <optional>
#include <vector>

#include "tradelab/scenario.hpp"

namespace tradelab {

constexpr std::uint32_t kIndicatorVersion = 1;

using Series = std::vector<std::optional<Price>>;  // index = bar; RSI is expressed 0..100 in Price units

// Every function returns values for bars 0..last (inclusive) and never reads bars after `last`.
Series sma(const Scenario& sc, std::uint32_t last, std::uint32_t period);
Series ema(const Scenario& sc, std::uint32_t last, std::uint32_t period);
Series rsi(const Scenario& sc, std::uint32_t last, std::uint32_t period);
Series atr(const Scenario& sc, std::uint32_t last, std::uint32_t period);

}  // namespace tradelab
