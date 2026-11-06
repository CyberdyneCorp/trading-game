#pragma once

// Internal option trading and accounting (spec: options). Option orders never match intrabar:
// they execute at the next bar's open against a quote built from the open price and implied
// volatility published at the previous close (no lookahead, no unobserved premium path).

#include "tradelab/engine.hpp"
#include "tradelab/options.hpp"

namespace tradelab::detail {

struct OptionBook {
  Money long_value;          // long positions at bid
  Money short_value;         // short positions at ask
  Money initial_margin;      // short positions
  Money maintenance_margin;  // short positions
};

// Short-option margin of one position on its own (spec: options "Writing options").
struct ShortMargin {
  Money initial;
  Money maintenance;
};

// |contracts| x multiplier x per_unit, rounded per mode.
Money units_value(const Scenario& sc, Qty contracts, Price per_unit, Rounding mode);
// Per-leg margin of `contracts` short at quote q (forward f).
ShortMargin short_margin(const Scenario& sc, const OptionSeries& s, Qty contracts, const OptionQuote& q, Price f);
// Taker fee of an option trade: a rate of the underlying notional, capped at 12.5% of the premium.
Money option_fee(const Scenario& sc, Qty contracts, Price premium, Price underlying);

// Valuation and margin of all option positions at decision step `step` (quotes at that close).
OptionBook option_book(const Scenario& sc, const RunState& st, std::uint32_t step);

// What placing this option order now would be checked against (premium + fee to buy, the margin a
// sale adds), as place_option_order computes it.
Money option_order_requirement(const Scenario& sc, const RunState& st, const OptionSeries& s, OrderSide side, Qty qty);

// Applies a trade at the current step's quote (ask to buy, bid to sell): the combo dry run's
// after-fill estimate, on a copy of the state.
void fill_option_at_quote(const Scenario& sc, RunState& st, const OptionSeries& s, OrderSide side, Qty qty);

CommandResult place_option_order(const Scenario& sc, RunState& st, const Command& c, Money available);

// Bar `bar` open: execute option orders against quotes at that open.
void option_open_phase(const Scenario& sc, RunState& st, std::uint32_t bar);

// Bar `bar` close: settle expiring series, then enforce maintenance margin on short options.
void option_close_phase(const Scenario& sc, RunState& st, std::uint32_t bar);

}  // namespace tradelab::detail
