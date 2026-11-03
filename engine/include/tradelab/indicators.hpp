#pragma once

// Indicators (spec: indicators). Integer fixed-point arithmetic, closed bars only, explicit
// warm-up: a value is absent until the indicator has seen its warm-up number of bars.
//
//   SMA(n): mean of the last n closes; warm-up n.
//   EMA(n): seeded with SMA(n) at bar n-1, then e = e + (close - e) * 2/(n+1); warm-up n.
//   RSI(n): Wilder smoothing of gains/losses seeded with their n-bar mean; warm-up n+1.
//   ATR(n): Wilder smoothing of true range seeded with its n-bar mean; warm-up n+1.
//   VWAP(session): sum(typical * volume) / sum(volume) since the session start, typical =
//     (H+L+C)/3; resets every `session` bars counted from bar 0; warm-up 1.
//   MACD(f, s, sig): line = EMA(f) - EMA(s) (from bar s-1); signal = EMA(sig) of the line, seeded
//     with its SMA; histogram = line - signal; signal warm-up s + sig - 1.
//   ADX(n): +DM/-DM and true range Wilder-smoothed (seeded with n-bar means from bar 1);
//     +DI/-DI = 100 * DM / TR from bar n; DX = 100 * |+DI - -DI| / (+DI + -DI); ADX = DX
//     seeded with its n-bar mean, then Wilder-smoothed; ADX warm-up 2n.

#include <cstdint>
#include <optional>
#include <vector>

#include "tradelab/scenario.hpp"

namespace tradelab {

constexpr std::uint32_t kIndicatorVersion = 2;

using Series = std::vector<std::optional<Price>>;  // index = bar; RSI/DI/ADX are 0..100 in Price units

// Every function returns values for bars 0..last (inclusive) and never reads bars after `last`.
Series sma(const Scenario& sc, std::uint32_t last, std::uint32_t period);
Series ema(const Scenario& sc, std::uint32_t last, std::uint32_t period);
Series rsi(const Scenario& sc, std::uint32_t last, std::uint32_t period);
Series atr(const Scenario& sc, std::uint32_t last, std::uint32_t period);
Series vwap(const Scenario& sc, std::uint32_t last, std::uint32_t session);

struct Macd {
  Series line;
  Series signal;
  Series histogram;
};
Macd macd(const Scenario& sc, std::uint32_t last, std::uint32_t fast, std::uint32_t slow, std::uint32_t signal);

struct Adx {
  Series plus_di;
  Series minus_di;
  Series adx;
};
Adx adx(const Scenario& sc, std::uint32_t last, std::uint32_t period);

}  // namespace tradelab
