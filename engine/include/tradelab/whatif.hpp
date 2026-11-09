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
  // Perpetual legs: a working protective stop. The expiry analytics treat the leg as closed at the stop
  // beyond it (a long is worth max(s, stop), a short min(s, stop); no gap), so a combo built on a held
  // position reports its loss to that stop. 0 = none. The value, P&L, Greeks and "today" curve ignore it.
  Price stop;
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

// The P&L grid over price and time (design D4 of add-practice-and-advanced-strategies): lo..hi in
// `points` steps (2..61) at up to 25 times, in bars from the current step (run) or minutes elapsed
// (run-less).
struct WhatIfHeatmapSpec {
  Price lo;
  Price hi;
  std::uint32_t points = 0;
  std::vector<std::uint32_t> times;
};

struct WhatIfRequest {
  // Run-less only.
  Price forward;
  std::uint32_t minutes_per_step = 0;  // theta per step
  Qty multiplier;
  Price tick;  // horizon breakevens' price grid; 0 = 0.01 (runs use the contract's tick)
  std::vector<WhatIfLeg> legs;
  WhatIfShift shift;
  WhatIfGrid grid;
  std::optional<WhatIfHeatmapSpec> heatmap;
  bool holdings = false;  // run only: the run's positions come first, and the reply adds `risk`
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

// Option legs on different expiries (design D4 of add-practice-and-advanced-strategies): the position at
// the first expiry, legs expiring then at intrinsic value, later legs at their model value then.
struct HorizonAnalytics {
  std::uint32_t expiry = 0;  // bar (run) or minutes from now (run-less)
  std::vector<Price> breakevens;     // on the tick grid, the profitable side of each crossing
  std::optional<Money> max_profit;   // nullopt: unbounded
  std::optional<Money> max_loss;     // nullopt: unbounded above
  bool unbounded_above = false;
  std::vector<CurvePoint> curve;
};

struct WhatIfHeatmap {
  std::vector<Price> prices;
  std::vector<std::uint32_t> times;
  std::vector<std::vector<Money>> pnl;  // [time][price]
};

enum class RiskLimitKind : std::uint8_t { Delta, Gamma, Vega, Theta };

struct RiskLimitCheck {
  RiskLimitKind kind = RiskLimitKind::Delta;
  std::uint32_t limit_bp = 0;
  std::int64_t value_bp = 0;
  bool exceeded = false;
};

// Portfolio risk of the reply's legs (holdings), design D4/D9: engine numbers the clients only show.
struct RiskBlock {
  Qty delta_units;     // sum of units x delta (the perpetual's is 1), 1e-8
  Money delta_notional;  // delta_units x last close
  Money gamma_1pct;      // 1/2 x |sum of units x gamma| x (1% of the last close)^2
  Money vega_point;
  Money theta_bar;
  Money funding_next;    // the next funding payment at the published rate (positive = received)
  std::int64_t delta_bp = 0;  // signed basis points of equity; 0 when equity <= 0
  std::int64_t gamma_bp = 0;
  std::int64_t vega_bp = 0;
  std::int64_t theta_bp = 0;
  std::int64_t funding_bp = 0;
  Money equity;
  std::vector<RiskLimitCheck> limits;  // the mission's declared limits
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
  std::optional<HorizonAnalytics> horizon;  // when option legs have different expiries
  std::optional<WhatIfHeatmap> heatmap;     // when requested
  std::optional<RiskBlock> risk;            // run what-if with holdings
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
  // Design D2 of add-practice-and-advanced-strategies (option legs): a limit at `premium` with the
  // combo's time in force; market legs ignore the time in force.
  OrderType type = OrderType::Market;
  Price premium;
  TimeInForce tif = TimeInForce::Gtc;
  std::uint32_t tif_bars = 0;
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
