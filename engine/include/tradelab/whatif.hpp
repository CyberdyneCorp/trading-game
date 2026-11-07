#pragma once

// Report-only what-if valuation and combo dry run (design D4 of add-academy-and-strategies).
// Nothing here mutates a run, enters a log or a digest, or reads data after the current step.

#include <cstdint>
#include <optional>
#include <vector>

#include "tradelab/engine.hpp"

namespace tradelab {

struct WhatIfLeg {
  bool perp = false;
  OptionKind kind = OptionKind::Call;
  OrderSide side = OrderSide::Buy;
  Qty qty;                     // contracts (options) or perpetual contracts
  Price entry;                 // per unit; for a run leg without one, the price it would cross now
  bool has_entry = false;
  // Run-less legs.
  Price strike;
  std::uint32_t minutes_to_expiry = 0;
  Vol iv;
  // Run legs.
  std::uint32_t series = 0;
};

struct WhatIfShift {
  Price forward;                    // zero = unchanged (the request's forward, or the run's last close)
  std::uint32_t minutes_elapsed = 0;  // run-less
  std::uint32_t bars = 0;             // run: decision steps forward
  Vol iv_points;                    // volatility points added to every option leg ("-5" = -0.05)
};

struct WhatIfGrid {
  Price lo;
  Price hi;
  std::uint32_t points = 0;  // 0 = the default: forward -30% .. +30%, 61 points
};

struct WhatIfRequest {
  // Run-less only.
  Price forward;
  std::uint32_t minutes_per_step = 0;  // theta per step
  Qty multiplier;
  std::vector<WhatIfLeg> legs;
  WhatIfShift shift;
  WhatIfGrid grid;
};

// Per unit: as OptionQuote (value Price, not tick-rounded); a perpetual leg's value is the forward
// and its delta 1. Position amounts are signed (short legs negative).
struct WhatIfLegResult {
  WhatIfLeg leg;
  Price strike;
  std::uint32_t expiry_step = 0;  // run legs
  Price value;
  Vol iv;
  bool iv_floored = false;
  Rate delta;
  Rate gamma;
  Price theta;
  Price vega;
  Money position_value;
  Money pnl;
};

struct WhatIfNet {
  Money value;
  Money pnl;
  Qty delta;  // decimal at 1e-8 (the Qty scale), sum of units x per-unit delta
  Qty gamma;
  Money theta;
  Money vega;
};

struct CurvePoint {
  Price price;
  Money pnl;
};

struct ExpiryAnalytics {
  std::vector<Price> breakevens;
  std::optional<Money> max_profit;  // nullopt: unbounded
  std::optional<Money> max_loss;    // a positive amount is a loss; nullopt: unbounded (above)
  std::vector<CurvePoint> curve;
};

enum class WhatIfError : std::uint8_t { None, InvalidInput, SeriesNotListed };

struct WhatIfResult {
  WhatIfError error = WhatIfError::None;
  std::vector<WhatIfLegResult> legs;
  WhatIfNet net;
  Price forward;
  bool at_expiry = false;  // the shift reaches the options' (common) expiry
  std::optional<ExpiryAnalytics> expiry;  // when every option leg shares one expiry
  std::vector<CurvePoint> today;          // P&L at the shifted time and IV
};

// Run-less valuation from explicit legs (the Academy): sigma = max(5%, iv + shift), time to expiry
// from minutes, a perpetual leg in units of the multiplier.
WhatIfResult option_whatif(const WhatIfRequest& req);

// The run's own valuation (the terminal lab): legs name listed series (and the perpetual), the IV
// model uses the base published at the current step with the term factor at the shifted time and
// skew and smile at the shifted forward. With no shift each leg equals the series' quote: value
// rounded to the tick = mid, Greeks bit for bit.
WhatIfResult run_option_whatif(const Scenario& sc, const RunState& st, const WhatIfRequest& req);

// ---- Combo dry run (tl_combo_preview) ----

struct ComboLeg {
  bool perp = false;
  std::uint32_t series = 0;
  OrderSide side = OrderSide::Buy;
  Qty qty;
};

struct ComboLegResult {
  bool accepted = false;
  Reason reason = Reason::None;
  Price quote;           // the ask or bid it would cross now (the last close for the perpetual)
  Money premium;         // units x quote (options)
  Money fee;
  Money required;        // what the command checks: premium + fee, short margin, or perp margin + fee
  Money available_after; // the account's available balance after the placement
};

struct ComboPreview {
  std::vector<ComboLegResult> legs;
  bool accepted = false;     // every leg accepted
  Money net_premium;         // credit > 0, debit < 0 (accepted legs)
  Money fees;
  Money required;
  Money available_after;
  // Estimate after the accepted legs fill at today's quotes (execution uses the next open).
  Money option_margin_after;
  Money position_margin_after;
  Money available_after_fill;
};

ComboPreview combo_preview(const Scenario& sc, const RunState& st, const std::vector<ComboLeg>& legs);

}  // namespace tradelab
