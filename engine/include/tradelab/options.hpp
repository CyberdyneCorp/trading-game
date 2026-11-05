#pragma once

// Model pricing for European options (spec: options "Model pricing", "Implied volatility model",
// "Greeks"). Black-76 on the perpetual's last close, zero rate. Floating point is confined to the
// det:: kernels and quantized immediately; inputs only use data up to the given step.

#include <cstdint>
#include <optional>

#include "tradelab/scenario.hpp"

namespace tradelab {

struct OptionQuote {
  std::uint32_t series_id = 0;
  bool listed = false;   // listed and not yet expired at this step
  Price mid;
  Price bid;             // may be zero (no bid for worthless options)
  Price ask;
  Vol iv;                // annualized implied volatility used
  // Greeks per contract unit, quantized: delta/gamma in Rate (1e-10), theta per step and vega per
  // one volatility point in Price units.
  Rate delta;
  Rate gamma;
  Price theta;
  Price vega;
  bool iv_from_realized = false;
};

const OptionSeries* find_series(const Scenario& sc, std::uint32_t id);

// Quote of `series` at decision step `step` for forward price `forward` (normally bars[step].close;
// the open of the next bar when executing). Uses only data up to `step`.
OptionQuote quote(const Scenario& sc, const OptionSeries& series, std::uint32_t step, Price forward);

// Intrinsic value per unit at settlement price s.
Price intrinsic(const OptionSeries& series, Price s);

// Settlement price for an expiry at `step` (policy v17): TWAP over the last `settlement_minutes` of
// market time ending at the expiry close, from the scenario's sub-bars for that bar when they cover
// the window on their grid (exact), else the mean of the closes of the bars covering it (the expiry
// close when the window is shorter than a bar). `approximated` is set unless the closes used tile the
// window exactly.
Price settlement_price(const Scenario& sc, std::uint32_t step, bool* approximated);

// True when an option order on `series` executing at the open of `bar` would fall inside the series'
// settlement window: from (expiry close - settlement_minutes) onward (reason `series_settling`).
bool in_settlement_window(const Scenario& sc, const OptionSeries& series, std::uint32_t bar);

}  // namespace tradelab
