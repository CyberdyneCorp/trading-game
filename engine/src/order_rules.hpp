#pragma once

// Execution policy v18 (design D6, D7 of add-practice-and-advanced-strategies): time in force and
// player-built OCO links. Internal to the engine; the commands and the bar sequence call these.

#include "tradelab/engine.hpp"

namespace tradelab::detail {

// ---- time in force (D6) ----

// invalid_time_in_force when the command's time in force does not fit the order: ioc/fok on anything
// but a limit, bars with N = 0 or past the run's last bar, a market order with bars, or tifBars set
// without the bars kind. `option` selects the option rules (only market and limit exist there).
Reason tif_refusal(const Scenario& sc, const RunState& st, const Command& c, bool option);

// Copies the command's time in force onto a new order placed at `o.placed_step`.
void set_tif(Order& o, const Command& c);

// True for an order that may execute only in the next open phase (ioc, fok).
bool immediate(const Order& o);

// After the open phase of the perpetual (options = false) or of the options (true): the remainder of
// every immediate order of that book is cancelled with time_in_force.
void cancel_immediate(RunState& st, bool options);

// At the close of bar `bar`, after the close phase's fills: bars orders whose last bar it is expire
// with time_in_force.
void expire_time_in_force(RunState& st, std::uint32_t bar);

// ---- player-built OCO links (D7) ----

// LinkOco: links two working orders (refused with invalid_oco, changing nothing).
CommandResult link_oco(RunState& st, const Command& c);

// After `filled` executed `q`: its linked sibling's remainder is cancelled (oco_sibling_filled), or,
// for two reduce-only exits of the perpetual position, reduced by q.
void resolve_link(RunState& st, const Order& filled, Qty q);

// `ended` stopped working without a fill deciding the link (cancel, expiry, refusal): the sibling
// keeps working, unlinked.
void dissolve_link(RunState& st, const Order& ended);

}  // namespace tradelab::detail
