#pragma once

// The one pricing kernel behind quote() and both what-if valuations (design D4 "Shared kernel"):
// Black-76 at a zero rate, then the quantization of OptionQuote. Doubles stay inside; callers only
// see the quantized per-unit values.

#include <cstdint>

#include "tradelab/scenario.hpp"

namespace tradelab::detail {

struct KernelInputs {
  double f;           // forward
  double k;           // strike
  double t;           // years to expiry (<= 0: at expiry)
  double sigma;       // annualized implied volatility, already floored
  double step_years;  // one decision step, for theta per step
};

// Per contract unit, quantized as OptionQuote: value Price (not tick-rounded), delta/gamma Rate,
// theta per step and vega per volatility point Price. `model` is the unrounded value, used only to
// build the quote's book.
struct UnitValuation {
  double model = 0.0;
  Price value;
  Vol iv;
  Rate delta;
  Rate gamma;
  Price theta;
  Price vega;
};

// Black-76 value and Greeks; at expiry (t <= 0) the intrinsic value, a delta of +-1 in the money
// and no time-value Greeks.
UnitValuation price_unit(const KernelInputs& in, OptionKind kind);

// The run's implied-volatility model before the 5% floor, with the base published at `base_step`
// (never a later one), the term factor at `t_years` and skew and smile at `forward`.
double raw_model_vol(const Scenario& sc, const OptionSeries& s, std::uint32_t base_step, Price forward, double t_years);

// Inputs exactly as quote() builds them at decision step `step`.
KernelInputs series_inputs(const Scenario& sc, const OptionSeries& s, std::uint32_t step, Price forward);

double step_years(const Scenario& sc);
double minutes_to_years(double minutes);
double floor_vol(double sigma);  // the model's 5% floor
double to_double(Price p);

}  // namespace tradelab::detail
