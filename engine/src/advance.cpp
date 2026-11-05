// One simulation step (spec: simulation-core "Fixed step sequence", order-execution
// "Conservative OHLC intrabar policy", perpetual-futures liquidation/funding/bankruptcy).
//
// Bar b = step + 1 is processed in this order:
//   1. open: liquidation through a gap, gap-triggered stops/limits, market orders at the open
//   2. intrabar exits of the existing position, adverse levels first (stop vs liquidation by
//      distance from the open), then favorable take-profits
//   3. intrabar entries (resting limits/stops); a position entered intrabar is then checked
//      against the adverse extreme without crediting favorable exits (conservative)
//   4. close: funding at boundaries, liquidation at the close, trailing triggers ratchet from the
//      close (effective next bar), metrics, finish/bankruptcy
//
// Liquidation and funding read the mark bar (policy v17; the traded bar when the scenario has no mark
// series); fills always use the traded bar. A continuity bar (no trading) has zero capacity: phases 1-3
// do not run, so working orders wait for the next trading bar, and only the close phase runs.
//
// Within a phase, orders competing for the bar's capacity are served by price, then sequence (policy
// v16, `priority_pass`). Bracket entries spawn reduce-only stop/take-profit children for each filled
// quantity; exits in one OCO group reduce each other. Children are appended while matching
// (`RunState::orders` is a deque, so references stay valid).

#include <algorithm>
#include <vector>

#include "accounting.hpp"
#include "options_exec.hpp"

namespace tradelab {
namespace {

using detail::abs_qty;

struct BarCtx {
  const Scenario& sc;
  RunState& st;
  const Bar& bar;
  const Bar& mark;  // mark-price bar (the traded bar when the scenario has no mark series)
  CostModel costs;  // of this bar: the cost model with any liquidity shock applied
  Qty capacity;
  bool entered_intrabar = false;
};

Qty bar_capacity(const Scenario& sc, const Bar& bar, const CostModel& costs) {
  if (bar.continuity) return Qty{};  // nothing traded
  const Qty raw = detail::unwrap(mul<Qty>(bar.volume, costs.capacity_fraction, Rounding::Floor));
  return detail::unwrap(round_to_step(raw, sc.contract.lot_size, Rounding::Floor));
}

// Taker execution price: base adjusted by half-spread + slippage against the player, on the tick grid.
Price taker_price(const BarCtx& c, Price base, OrderSide side) {
  const Rate cost = Rate::from_raw(c.costs.half_spread.raw + c.costs.slippage.raw);
  const Price adj = detail::unwrap(mul<Price>(base, cost, side == OrderSide::Buy ? Rounding::Ceil : Rounding::Floor));
  const Price raw = Price::from_raw(side == OrderSide::Buy ? base.raw + adj.raw : base.raw - adj.raw);
  const Rounding mode = side == OrderSide::Buy ? Rounding::Ceil : Rounding::Floor;
  const Price px = detail::unwrap(round_to_step(raw, c.sc.contract.tick_size, mode));
  return std::max(px, c.sc.contract.tick_size);
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

bool is_stop(const Order& o) { return o.type == OrderType::StopMarket || o.type == OrderType::TrailingStop; }

// A stop that has started to fill was triggered: it is a market order now (policy v15), so its
// remainder executes at the next opens and no longer rests at the trigger.
bool triggered_stop(const Order& o) { return is_stop(o) && o.filled.raw > 0; }

// A resting stop still waiting for its trigger.
bool resting_stop(const Order& o) { return is_stop(o) && o.filled.raw == 0; }

// Adds q to the parent's active child of `type`, or creates it (reduce-only, OCO group = parent id).
void grow_child(RunState& st, const Order& parent, OrderType type, Price px, Qty q) {
  for (Order& ch : st.orders) {
    if (ch.parent_id == parent.id && ch.type == type && ch.active()) {
      ch.qty = Qty::from_raw(ch.qty.raw + q.raw);
      detail::emit_order_update(st, ch, Reason::None);
      return;
    }
  }
  Order ch;
  ch.id = st.next_order_id++;
  ch.seq = parent.seq;
  ch.type = type;
  ch.side = opposite(parent.side);
  ch.qty = q;
  ch.price = px;
  ch.reduce_only = true;
  ch.placed_step = st.step;
  ch.parent_id = parent.id;
  ch.oco_group = parent.id;
  st.orders.push_back(ch);
  detail::emit_order_update(st, ch, Reason::None);
}

void attach_bracket(RunState& st, const Order& parent, Qty q) {
  if (parent.bracket_stop.raw > 0) grow_child(st, parent, OrderType::StopMarket, parent.bracket_stop, q);
  if (parent.bracket_tp.raw > 0) grow_child(st, parent, OrderType::TakeProfit, parent.bracket_tp, q);
}

// A fill of q on one exit reduces every sibling in its OCO group by q.
void reduce_oco_siblings(RunState& st, const Order& filled, Qty q) {
  if (filled.oco_group == 0) return;
  for (Order& sib : st.orders) {
    if (!sib.active() || sib.id == filled.id || sib.oco_group != filled.oco_group) continue;
    sib.qty = Qty::from_raw(std::max(sib.filled.raw, sib.qty.raw - q.raw));
    if (sib.remaining().raw > 0) continue;
    sib.status = OrderStatus::Cancelled;
    detail::emit_order_update(st, sib, Reason::OcoSiblingFilled);
  }
}

// Quantity by which a fill grew exposure (same direction, or the new side after a flip).
Qty exposure_increase(const Position& before, const Position& after) {
  const Qty b = abs_qty(before.qty);
  const Qty a = abs_qty(after.qty);
  const bool flipped = before.qty.raw != 0 && after.qty.raw != 0 && (before.qty.raw > 0) != (after.qty.raw > 0);
  if (flipped) return a;
  return Qty::from_raw(std::max<std::int64_t>(0, a.raw - b.raw));
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
  if (const Reason refused = detail::fill_refusal(c.sc, c.st, o, q, ex.price); refused != Reason::None) {
    cancel(c.st, o, refused);
    return false;
  }
  const std::uint8_t flags = ex.flags | (limited ? kFlagCapacityFromBarVolume : kFlagNone);
  const Position before = c.st.position;
  detail::apply_fill(c.sc, c.st, {o.side, q, ex.price, o.price, ex.maker, flags, o.id});
  o.filled = Qty::from_raw(o.filled.raw + q.raw);
  c.capacity = Qty::from_raw(c.capacity.raw - q.raw);
  o.status = o.remaining().raw == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
  detail::emit_order_update(c.st, o, Reason::None);
  reduce_oco_siblings(c.st, o, q);
  // An exit larger than the position it just closed has nothing left to reduce (policy v14).
  if (o.reduce_only && o.active() && c.st.position.flat()) cancel(c.st, o, Reason::PositionClosed);
  const Qty inc = exposure_increase(before, c.st.position);
  if (inc.raw > 0) {
    if ((ex.flags & kFlagGapFill) == 0) c.entered_intrabar = true;
    attach_bracket(c.st, o, inc);
  }
  return true;
}

// Market orders are IOC: any remainder after the open is cancelled. A reduce-only market order
// (e.g. "close position") cut short by capacity keeps working at the next opens instead, so a
// close never silently leaves part of the position open.
void execute_market(BarCtx& c, Order& o) {
  execute(c, o, {taker_price(c, c.bar.open, o.side), false, kFlagNone});
  if (!o.active()) return;
  const bool out_of_capacity = c.capacity.raw == 0;
  if (out_of_capacity && o.reduce_only && !c.st.position.flat()) return;
  cancel(c.st, o, out_of_capacity ? Reason::InsufficientCapacity : Reason::PositionClosed);
}

// A triggered stop converts to a market order: a reduce-only remainder cut short by capacity keeps
// working at the next opens (open_phase, like a reduce-only market order); any other remainder is
// cancelled like a market order's (IOC).
bool execute_stop(BarCtx& c, Order& o, const Exec& ex) {
  const bool filled = execute(c, o, ex);
  if (filled && o.active() && !o.reduce_only) cancel(c.st, o, Reason::InsufficientCapacity);
  return filled;
}

// ---- capacity priority (policy v16) ------------------------------------------------------
// Phases follow the bar's own order (open, adverse exits, favorable exits, entries). Within a phase,
// orders competing for the bar's capacity are served by price, then sequence: the level the market
// reaches first, then the earlier command sequence (bracket legs carry their entry's), then the id.

// How far along the market's path to the order's level: with a direction (+1 up, -1 down) the signed
// distance from `from`, so a level already passed ranks first; without one, the distance either way.
std::int64_t reach_key(const Order& o, Price from, std::int64_t dir) {
  const std::int64_t d = o.price.raw - from.raw;
  if (dir != 0) return d * dir;
  return d < 0 ? -d : d;
}

template <typename Key>
bool served_before(const Order& a, const Order& b, const Key& key) {
  const std::int64_t ka = key(a);
  const std::int64_t kb = key(b);
  if (ka != kb) return ka < kb;
  return a.seq != b.seq ? a.seq < b.seq : a.id < b.id;
}

// Runs `act` once on each eligible order, best priority first. Eligibility is re-read after every
// execution, so legs attached by a fill join the same pass and orders the fill made moot drop out.
template <typename Eligible, typename Key, typename Act>
void priority_pass(BarCtx& c, const Eligible& eligible, const Key& key, const Act& act) {
  std::vector<std::uint32_t> served;
  for (;;) {
    Order* best = nullptr;
    for (Order& o : c.st.orders) {
      if (!eligible(o) || std::find(served.begin(), served.end(), o.id) != served.end()) continue;
      if (best == nullptr || served_before(o, *best, key)) best = &o;
    }
    if (best == nullptr) return;
    served.push_back(best->id);
    act(*best);
  }
}

bool liquidation_level(const BarCtx& c, Price* out) { return detail::liquidation_price(c.sc, c.st.position, out); }

// ---- 1. open -------------------------------------------------------------------------------

void open_gap_liquidation(BarCtx& c) {
  Price liq;
  if (!liquidation_level(c, &liq)) return;
  const bool hit = c.st.position.is_long() ? c.mark.open <= liq : c.mark.open >= liq;
  if (hit) detail::liquidate(c.sc, c.st, c.mark.open, kFlagGapFill);
}

// True if the open has gapped through a resting perpetual stop or limit.
bool gapped_through(const BarCtx& c, const Order& o) {
  if (!o.active() || o.series != 0 || o.type == OrderType::Market || triggered_stop(o)) return false;
  const bool buy = o.side == OrderSide::Buy;
  const Price open = c.bar.open;
  if (is_stop(o)) return buy ? open >= o.price : open <= o.price;
  return buy ? open <= o.price : open >= o.price;
}

void open_gap_orders(BarCtx& c, std::uint32_t index) {
  // The gap runs from the previous close to the open: levels along it are reached in that direction.
  const Price from = c.sc.bars[index - 1].close;
  const std::int64_t dir = c.bar.open > from ? 1 : (c.bar.open < from ? -1 : 0);
  const auto key = [&](const Order& o) { return reach_key(o, from, dir); };
  const auto eligible = [&](const Order& o) { return gapped_through(c, o); };
  priority_pass(c, eligible, key, [&](Order& o) {
    if (is_stop(o)) execute_stop(c, o, {taker_price(c, c.bar.open, o.side), false, kFlagGapFill});
    else execute(c, o, {c.bar.open, true, kFlagGapFill});
  });
}

bool executes_at_open(const Order& o) { return o.active() && o.series == 0 && (o.type == OrderType::Market || triggered_stop(o)); }

void open_phase(BarCtx& c, std::uint32_t index) {
  open_gap_liquidation(c);
  open_gap_orders(c, index);
  // Market orders (and triggered stops' remainders) have no price to rank by: sequence only.
  priority_pass(c, executes_at_open, [](const Order&) { return std::int64_t{0}; }, [&](Order& o) { execute_market(c, o); });
  // Bracket legs attached by fills at the open are already live there: one the open has gapped
  // through (e.g. a stop computed from the previous close) executes at the open, never at a
  // trigger price the bar did not trade after it. Orders checked in the first pass are unchanged.
  open_gap_orders(c, index);
}

// ---- 2/3. intrabar -----------------------------------------------------------------------

bool favorable_exit_reachable(const BarCtx& c) {
  const Position& p = c.st.position;
  const Price tick = c.sc.contract.tick_size;
  for (const Order& o : c.st.orders) {
    if (!o.active() || o.series != 0 || o.type == OrderType::Market || is_stop(o)) continue;
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
    if (!o.active() || o.series != 0 || !resting_stop(o)) continue;
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
  const bool liq_hit = has_liq && (is_long ? c.mark.low <= liq : c.mark.high >= liq);
  Order* stop = nearest_adverse_stop(c);
  const std::uint8_t amb = (post_entry || favorable_exit_reachable(c)) ? kFlagAmbiguousIntrabar : kFlagNone;
  // Liquidation wins ties and any stop sitting beyond the liquidation price.
  const bool stop_first = stop != nullptr && (!liq_hit || (is_long ? stop->price > liq : stop->price < liq));
  if (stop_first) return execute_stop(c, *stop, {taker_price(c, stop->price, stop->side), false, amb});
  if (liq_hit) detail::liquidate(c.sc, c.st, liq, amb);
  return liq_hit;
}

void exit_sweep(BarCtx& c, bool post_entry) {
  for (std::size_t guard = 0; guard <= c.st.orders.size() && !c.st.position.flat(); ++guard) {
    if (!adverse_step(c, post_entry)) return;
  }
}

bool favorable_exit_reached(const BarCtx& c, const Order& o) {
  if (c.st.position.flat() || !o.active() || o.series != 0) return false;
  if (o.type != OrderType::Limit && o.type != OrderType::TakeProfit) return false;
  if (c.st.position.is_long() != (o.side == OrderSide::Sell)) return false;
  const Price tick = c.sc.contract.tick_size;
  return o.side == OrderSide::Sell ? c.bar.high.raw >= o.price.raw + tick.raw : c.bar.low.raw <= o.price.raw - tick.raw;
}

// Intrabar levels are ranked by distance from the open: the nearer one trades first.
void favorable_exits(BarCtx& c) {
  const auto eligible = [&](const Order& o) { return favorable_exit_reached(c, o); };
  const auto key = [&](const Order& o) { return reach_key(o, c.bar.open, 0); };
  priority_pass(c, eligible, key, [&](Order& o) { execute(c, o, {o.price, true, kFlagNone}); });
}

bool entry_reached(const BarCtx& c, const Order& o) {
  const Price tick = c.sc.contract.tick_size;
  const bool buy = o.side == OrderSide::Buy;
  if (is_stop(o)) return buy ? c.bar.high >= o.price : c.bar.low <= o.price;
  return buy ? c.bar.low.raw <= o.price.raw - tick.raw : c.bar.high.raw >= o.price.raw + tick.raw;
}

void entries(BarCtx& c) {
  const auto eligible = [&](const Order& o) {
    return o.active() && o.series == 0 && !o.reduce_only && o.type != OrderType::Market && entry_reached(c, o);
  };
  const auto key = [&](const Order& o) { return reach_key(o, c.bar.open, 0); };
  priority_pass(c, eligible, key, [&](Order& o) {
    if (is_stop(o)) execute_stop(c, o, {taker_price(c, o.price, o.side), false, kFlagAmbiguousIntrabar});
    else execute(c, o, {o.price, true, kFlagAmbiguousIntrabar});
  });
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
  const bool hit = c.st.position.is_long() ? c.mark.close <= liq : c.mark.close >= liq;
  if (hit || c.st.position.margin.raw <= 0) detail::liquidate(c.sc, c.st, c.mark.close, kFlagNone);
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
  const std::int32_t cap = run_leverage_cap(sc, st);
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
  const bool exposed = !st.position.flat() || !st.option_positions.empty();
  if (!exposed && !has_entry_orders(st) && !can_afford_min_order(c.sc, st)) {
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

// Trailing triggers follow the best close (never loosen) by a fixed distance or, from policy v16, a
// rate of that close; computed at the close of bar t, so they only take effect from bar t+1 (spec
// order-execution "Trailing stop").
void update_trailing(BarCtx& c) {
  for (Order& o : c.st.orders) {
    if (!o.active() || o.type != OrderType::TrailingStop || triggered_stop(o)) continue;
    const Price cand = detail::trail_trigger(c.sc, o.side, c.bar.close, o.trail_distance, o.trail_rate);
    if (o.side == OrderSide::Sell ? cand > o.price : cand < o.price) o.price = cand;
  }
}

void close_phase(BarCtx& c, std::uint32_t index) {
  if (index % c.sc.contract.funding_interval == 0) detail::apply_funding(c.sc, c.st, index);
  close_liquidation(c);
  detail::option_close_phase(c.sc, c.st, index);
  update_trailing(c);
  update_metrics(c);
  finish_or_bankrupt(c);
}

}  // namespace

Money run_initial_balance(const Scenario& sc, const RunState& st) {
  return st.options.starting_balance.raw > 0 ? st.options.starting_balance : sc.mission.initial_balance;
}

std::int32_t run_leverage_cap(const Scenario& sc, const RunState& st) {
  const std::int32_t cap = std::min(sc.mission.leverage_cap, sc.contract.max_leverage);
  return st.options.leverage_cap > 0 ? std::min(cap, st.options.leverage_cap) : cap;
}

std::uint32_t last_decision_step(const Scenario& sc) { return static_cast<std::uint32_t>(sc.bars.size() - 2); }

RunState start_run(const Scenario& sc, std::uint64_t seed, const RunOptions& options) {
  RunState st;
  st.seed = seed;
  st.options = options;
  st.advice_left = options.advice_allowance;
  st.step = sc.mission.warmup_bars;
  st.wallet = run_initial_balance(sc, st);
  st.metrics.peak_equity = st.wallet;
  st.rng = Pcg32(seed, Stream::Slippage);
  return st;
}

void advance(const Scenario& sc, RunState& st) {
  if (st.status != RunStatus::Active) return;
  const std::uint32_t index = st.step + 1;
  st.step = index;  // events of this step are stamped with the bar being processed
  const Bar& bar = sc.bars[index];
  const CostModel costs = costs_at(sc, index);
  BarCtx c{sc, st, bar, mark_bar(sc, index), costs, bar_capacity(sc, bar, costs)};
  if (!bar.continuity) {
    open_phase(c, index);
    detail::option_open_phase(sc, st, index);
    intrabar_phase(c);
  }
  close_phase(c, index);
}

}  // namespace tradelab
