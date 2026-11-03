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

// Valuation and margin of all option positions at decision step `step` (quotes at that close).
OptionBook option_book(const Scenario& sc, const RunState& st, std::uint32_t step);

CommandResult place_option_order(const Scenario& sc, RunState& st, const Command& c, Money available);

// Bar `bar` open: execute option orders against quotes at that open.
void option_open_phase(const Scenario& sc, RunState& st, std::uint32_t bar);

// Bar `bar` close: settle expiring series, then enforce maintenance margin on short options.
void option_close_phase(const Scenario& sc, RunState& st, std::uint32_t bar);

}  // namespace tradelab::detail
