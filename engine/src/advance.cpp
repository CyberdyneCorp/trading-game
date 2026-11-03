// One simulation step (spec: simulation-core "Fixed step sequence", order-execution
// "Conservative OHLC intrabar policy", perpetual-futures liquidation/funding/bankruptcy).
//
// Bar b = step + 1 is processed in this order:
//   1. open: liquidation through a gap, gap-triggered stops/limits, market orders at the open
//   2. intrabar exits of the existing position, adverse levels first (stop vs liquidation by
//      distance from the open), then favorable take-profits
//   3. intrabar entries (resting limits/stops); a position entered intrabar is then checked
//      against the adverse extreme without crediting favorable exits (conservative)
//   4. close: funding at boundaries, liquidation at the close, metrics, finish/bankruptcy

#include <algorithm>

#include "accounting.hpp"

namespace tradelab {
namespace {

using detail::abs_qty;

struct BarCtx {
  const Scenario& sc;
  RunState& st;
  const Bar& bar;
  Qty capacity;
  bool entered_intrabar = false;
};

Qty bar_capacity(const Scenario& sc, const Bar& bar) {
  const Qty raw = detail::unwrap(mul<Qty>(bar.volume, sc.costs.capacity_fraction, Rounding::Floor));
  return detail::unwrap(round_to_step(raw, sc.contract.lot_size, Rounding::Floor));
}

// Taker execution price: base adjusted by half-spread + slippage against the player, on the tick grid.
Price taker_price(const Scenario& sc, Price base, OrderSide side) {
  const Rate cost = Rate::from_raw(sc.costs.half_spread.raw + sc.costs.slippage.raw);
  const Price adj = detail::unwrap(mul<Price>(base, cost, side == OrderSide::Buy ? Rounding::Ceil : Rounding::Floor));
  const Price raw = Price::from_raw(side == OrderSide::Buy ? base.raw + adj.raw : base.raw - adj.raw);
  const Rounding mode = side == OrderSide::Buy ? Rounding::Ceil : Rounding::Floor;
  const Price px = detail::unwrap(round_to_step(raw, sc.contract.tick_size, mode));
  return std::max(px, sc.contract.tick_size);
}

void cancel(RunState& st, Order& o, Reason reason) {
  o.status = OrderStatus::Cancelled;
  detail::emit_order_update(st, o, reason);
}

// Quantity of `o` that can execute now: reduce-only clipped to the position, all clipped to capacity.
Qty executable_qty(const BarCtx& c, const Order& o, bool* capacity_limited) {
  Qty q = o.remaining();
  if (o.reduce_only) {
    const Position& p = c.st.position;
    const bool closes = !p.flat() && p.is_long() != (o.side == OrderSide::Buy);
    q = closes ? Qty::from_raw(std::min(q.raw, abs_qty(p.qty).raw)) : Qty{};
  }
  *capacity_limited = q > c.capacity;
  return std::min(q, c.capacity);
}

struct Exec {
  Price price;
  bool maker = false;
  std::uint8_t flags = kFlagNone;
};

// Executes as much of `o` as possible. Returns true if anything filled.
bool execute(BarCtx& c, Order& o, const Exec& ex) {
  bool limited = false;
  const Qty q = executable_qty(c, o, &limited);
  if (q.raw <= 0) {
    if (o.reduce_only && c.st.position.flat()) cancel(c.st, o, Reason::PositionClosed);
    return false;
  }
  if (!detail::margin_allows_fill(c.sc, c.st, o, q, ex.price)) {
    cancel(c.st, o, Reason::InsufficientMargin);
    return false;
  }
  const std::uint8_t flags = ex.flags | (limited ? kFlagCapacityFromBarVolume : kFlagNone);
  const Position before = c.st.position;
  detail::apply_fill(c.sc, c.st, {o.side, q, ex.price, o.price, ex.maker, flags, o.id});
  o.filled = Qty::from_raw(o.filled.raw + q.raw);
  c.capacity = Qty::from_raw(c.capacity.raw - q.raw);
  o.status = o.remaining().raw == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
  detail::emit_order_update(c.st, o, Reason::None);
  const bool increased = abs_qty(c.st.position.qty) > abs_qty(before.qty) ||
                         (before.qty.raw != 0 && (before.qty.raw > 0) != (c.st.position.qty.raw > 0));
  if (increased && (ex.flags & kFlagGapFill) == 0) c.entered_intrabar = true;
  return true;
}

// Market orders are IOC: any remainder after the open is cancelled.
void execute_market(BarCtx& c, Order& o) {
  execute(c, o, {taker_price(c.sc, c.bar.open, o.side), false, kFlagNone});
  if (o.active()) cancel(c.st, o, c.capacity.raw == 0 ? Reason::InsufficientCapacity : Reason::PositionClosed);
}

bool liquidation_level(const BarCtx& c, Price* out) { return detail::liquidation_price(c.sc, c.st.position, out); }

// ---- 1. open -------------------------------------------------------------------------------

void open_gap_liquidation(BarCtx& c) {
  Price liq;
  if (!liquidation_level(c, &liq)) return;
  const bool hit = c.st.position.is_long() ? c.bar.open <= liq : c.bar.open >= liq;
  if (hit) detail::liquidate(c.sc, c.st, c.bar.open, kFlagGapFill);
}

void open_gap_orders(BarCtx& c) {
  const Price open = c.bar.open;
  for (Order& o : c.st.orders) {
    if (!o.active() || o.type == OrderType::Market) continue;
    const bool buy = o.side == OrderSide::Buy;
    if (o.type == OrderType::StopMarket) {
      if (buy ? open >= o.price : open <= o.price) execute(c, o, {taker_price(c.sc, open, o.side), false, kFlagGapFill});
    } else if (buy ? open <= o.price : open >= o.price) {
      execute(c, o, {open, true, kFlagGapFill});
    }
  }
}

void open_phase(BarCtx& c) {
  open_gap_liquidation(c);
  open_gap_orders(c);
  for (Order& o : c.st.orders) {
    if (o.active() && o.type == OrderType::Market) execute_market(c, o);
  }
}

// ---- 2/3. intrabar -----------------------------------------------------------------------

bool favorable_exit_reachable(const BarCtx& c) {
  const Position& p = c.st.position;
  const Price tick = c.sc.contract.tick_size;
  for (const Order& o : c.st.orders) {
    if (!o.active() || o.type == OrderType::Market || o.type == OrderType::StopMarket) continue;
    if (p.is_long() && o.side == OrderSide::Sell && c.bar.high.raw >= o.price.raw + tick.raw) return true;
    if (!p.is_long() && o.side == OrderSide::Buy && c.bar.low.raw <= o.price.raw - tick.raw) return true;
  }
  return false;
}

// Closest adverse stop that the bar reached (first hit moving from the open toward the extreme).
Order* nearest_adverse_stop(BarCtx& c) {
  const bool is_long = c.st.position.is_long();
  Order* best = nullptr;
  for (Order& o : c.st.orders) {
    if (!o.active() || o.type != OrderType::StopMarket) continue;
    if (is_long != (o.side == OrderSide::Sell)) continue;
    const bool reached = is_long ? c.bar.low <= o.price : c.bar.high >= o.price;
    if (!reached) continue;
    if (best == nullptr || (is_long ? o.price > best->price : o.price < best->price)) best = &o;
  }
  return best;
}

// Returns true if an adverse event (stop or liquidation) happened.
bool adverse_step(BarCtx& c, bool post_entry) {
  const bool is_long = c.st.position.is_long();
  Price liq;
  const bool has_liq = liquidation_level(c, &liq);
  const bool liq_hit = has_liq && (is_long ? c.bar.low <= liq : c.bar.high >= liq);
  Order* stop = nearest_adverse_stop(c);
  const std::uint8_t amb = (post_entry || favorable_exit_reachable(c)) ? kFlagAmbiguousIntrabar : kFlagNone;
  // Liquidation wins ties and any stop sitting beyond the liquidation price.
  const bool stop_first = stop != nullptr && (!liq_hit || (is_long ? stop->price > liq : stop->price < liq));
  if (stop_first) return execute(c, *stop, {taker_price(c.sc, stop->price, stop->side), false, amb});
  if (liq_hit) detail::liquidate(c.sc, c.st, liq, amb);
  return liq_hit;
}

void exit_sweep(BarCtx& c, bool post_entry) {
  for (std::size_t guard = 0; guard <= c.st.orders.size() && !c.st.position.flat(); ++guard) {
    if (!adverse_step(c, post_entry)) return;
  }
}

void favorable_exits(BarCtx& c) {
  const Price tick = c.sc.contract.tick_size;
  for (Order& o : c.st.orders) {
    if (c.st.position.flat()) return;
    if (!o.active() || (o.type != OrderType::Limit && o.type != OrderType::TakeProfit)) continue;
    const bool closes = c.st.position.is_long() == (o.side == OrderSide::Sell);
    if (!closes) continue;
    const bool reached = o.side == OrderSide::Sell ? c.bar.high.raw >= o.price.raw + tick.raw
                                                   : c.bar.low.raw <= o.price.raw - tick.raw;
    if (reached) execute(c, o, {o.price, true, kFlagNone});
  }
}

bool entry_reached(const BarCtx& c, const Order& o) {
  const Price tick = c.sc.contract.tick_size;
  const bool buy = o.side == OrderSide::Buy;
  if (o.type == OrderType::StopMarket) return buy ? c.bar.high >= o.price : c.bar.low <= o.price;
  return buy ? c.bar.low.raw <= o.price.raw - tick.raw : c.bar.high.raw >= o.price.raw + tick.raw;
}

void entries(BarCtx& c) {
  for (Order& o : c.st.orders) {
    if (!o.active() || o.reduce_only || o.type == OrderType::Market || !entry_reached(c, o)) continue;
    const bool stop = o.type == OrderType::StopMarket;
    const Exec ex = stop ? Exec{taker_price(c.sc, o.price, o.side), false, kFlagAmbiguousIntrabar}
                         : Exec{o.price, true, kFlagAmbiguousIntrabar};
    execute(c, o, ex);
  }
}

void intrabar_phase(BarCtx& c) {
  exit_sweep(c, false);
  if (!c.entered_intrabar) favorable_exits(c);
  entries(c);
  if (c.entered_intrabar) exit_sweep(c, true);
}

// ---- 4. close ----------------------------------------------------------------------------

void close_liquidation(BarCtx& c) {
  Price liq;
  if (!liquidation_level(c, &liq)) return;
  const bool hit = c.st.position.is_long() ? c.bar.close <= liq : c.bar.close >= liq;
  if (hit || c.st.position.margin.raw <= 0) detail::liquidate(c.sc, c.st, c.bar.close, kFlagNone);
}

void update_metrics(BarCtx& c) {
  Metrics& m = c.st.metrics;
  const AccountView v = account_view(c.sc, c.st);
  m.peak_equity = std::max(m.peak_equity, v.equity);
  m.max_drawdown = std::max(m.max_drawdown, Money::from_raw(m.peak_equity.raw - v.equity.raw));
  if (v.equity.raw > 0) {
    const auto lev = static_cast<std::int64_t>(div_round(static_cast<i128>(v.notional.raw) * 10000, v.equity.raw, Rounding::Ceil));
    m.peak_leverage_bp = std::max(m.peak_leverage_bp, lev);
  }
  if (v.has_liquidation) {
    const std::int64_t dist = v.mark.raw > v.liquidation_price.raw ? v.mark.raw - v.liquidation_price.raw
                                                                   : v.liquidation_price.raw - v.mark.raw;
    const auto bp = static_cast<std::int64_t>(div_round(static_cast<i128>(dist) * 10000, v.mark.raw, Rounding::Floor));
    if (m.min_liq_distance_bp < 0 || bp < m.min_liq_distance_bp) m.min_liq_distance_bp = bp;
  }
}

bool can_afford_min_order(const Scenario& sc, const RunState& st) {
  const std::int32_t cap = std::min(sc.mission.leverage_cap, sc.contract.max_leverage);
  const Money n = detail::notional(sc, sc.contract.min_qty, sc.bars[st.step].close, Rounding::Ceil);
  const Money need = Money::from_raw(detail::initial_margin(n, cap).raw + detail::fee_for(n, sc.contract.taker_fee).raw);
  return account_view(sc, st).available >= need;
}

bool has_entry_orders(const RunState& st) {
  return std::any_of(st.orders.begin(), st.orders.end(), [](const Order& o) { return o.active() && !o.reduce_only; });
}

void emit_status(RunState& st, EventType type) {
  Event ev;
  ev.type = type;
  ev.amount = st.wallet;
  detail::push_event(st, ev);
}

void finish_or_bankrupt(BarCtx& c) {
  RunState& st = c.st;
  if (st.position.flat() && !has_entry_orders(st) && !can_afford_min_order(c.sc, st)) {
    st.status = RunStatus::Bankrupt;
    emit_status(st, EventType::Bankrupt);
    return;
  }
  if (st.step + 1 < c.sc.bars.size()) return;
  for (Order& o : st.orders) {
    if (!o.active()) continue;
    o.status = OrderStatus::Expired;
    detail::emit_order_update(st, o, Reason::Expired);
  }
  st.status = RunStatus::Finished;
  emit_status(st, EventType::RunFinished);
}

void close_phase(BarCtx& c, std::uint32_t index) {
  if (index % c.sc.contract.funding_interval == 0) detail::apply_funding(c.sc, c.st, index);
  close_liquidation(c);
  update_metrics(c);
  finish_or_bankrupt(c);
}

}  // namespace

std::uint32_t last_decision_step(const Scenario& sc) { return static_cast<std::uint32_t>(sc.bars.size() - 2); }

RunState start_run(const Scenario& sc, std::uint64_t seed) {
  RunState st;
  st.seed = seed;
  st.step = sc.mission.warmup_bars;
  st.wallet = sc.mission.initial_balance;
  st.metrics.peak_equity = st.wallet;
  st.rng = Pcg32(seed, Stream::Slippage);
  return st;
}

void advance(const Scenario& sc, RunState& st) {
  if (st.status != RunStatus::Active) return;
  const std::uint32_t index = st.step + 1;
  st.step = index;  // events of this step are stamped with the bar being processed
  BarCtx c{sc, st, sc.bars[index], bar_capacity(sc, sc.bars[index])};
  open_phase(c);
  intrabar_phase(c);
  close_phase(c, index);
}

}  // namespace tradelab
