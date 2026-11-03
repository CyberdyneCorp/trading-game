#pragma once

// Internal perpetual-futures accounting (spec: perpetual-futures). Not part of the public API.
//
// Magnitudes are bounded at order acceptance (notional <= kMaxNotional), so the fixed-point
// helpers below cannot overflow afterwards; `unwrap` asserts that invariant in debug builds.

#include <cassert>

#include "tradelab/engine.hpp"

namespace tradelab::detail {

inline constexpr Money kMaxNotional = Money::from_raw(10'000'000'000LL * Money::kScale);  // 1e10 credits

template <typename T>
T unwrap(const Result<T>& r) {
  assert(r.ok() && "fixed-point invariant violated: magnitude should have been bounded at acceptance");
  return r.value();
}

Qty abs_qty(Qty q);
Qty units(const Scenario& sc, Qty contracts);  // contracts * multiplier
Money notional(const Scenario& sc, Qty contracts, Price px, Rounding mode);
Money fee_for(Money notional, Rate rate);  // rounds against the player
Money initial_margin(Money notional, std::int32_t leverage);
Money unrealized_pnl(const Scenario& sc, const Position& p, Price mark);
// Price at which isolated margin + unrealized P&L equals maintenance margin.
// Returns false when the position cannot be liquidated (e.g. fully collateralized long).
bool liquidation_price(const Scenario& sc, const Position& p, Price* out);

void push_event(RunState& st, Event ev);

struct FillSpec {
  OrderSide side = OrderSide::Buy;
  Qty qty;
  Price price;
  Price requested;
  bool maker = false;
  std::uint8_t flags = kFlagNone;
  std::uint32_t order_id = 0;
};

// Applies a fill to the position and wallet (increase, reduce or flip), charges the fee
// and emits a Fill event. Margin for the increasing part uses the run leverage.
void apply_fill(const Scenario& sc, RunState& st, const FillSpec& f);

// Closes the position at `px`, forfeiting the isolated margin (execution-policy v1).
void liquidate(const Scenario& sc, RunState& st, Price px, std::uint8_t flags);

// Cancels every active reduce-only order (position flat or flipped).
// Cancels active reduce-only orders left without a position, except `keep_order` (the order being
// filled, which ends `filled`). 0 keeps none.
void cancel_orphan_reduce_only(RunState& st, Reason reason, std::uint32_t keep_order = 0);

void apply_funding(const Scenario& sc, RunState& st, std::uint32_t bar);

void emit_order_update(RunState& st, const Order& o, Reason reason);
// Margin reserved by active orders that would increase exposure (order ids start at 1; 0 excludes none).
Money order_margin_excluding(const Scenario& sc, const RunState& st, std::uint32_t exclude_id);
// True if filling q of `o` at px leaves enough available balance for the increasing part.
bool margin_allows_fill(const Scenario& sc, const RunState& st, const Order& o, Qty q, Price px);

}  // namespace tradelab::detail
