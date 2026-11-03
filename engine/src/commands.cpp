// Command validation and application at a decision point (spec: simulation-core, order-execution).

#include <algorithm>

#include "accounting.hpp"
#include "options_exec.hpp"

namespace tradelab {
namespace {

using detail::abs_qty;

CommandResult reject(Reason r) { return CommandResult{false, r, 0, Reason::None}; }

// Reference price for reserving margin: limit/trigger, or last close plus a cost buffer for market.
Price reference_price(const Scenario& sc, const RunState& st, const Order& o) {
  if (o.type != OrderType::Market) return o.price;
  const Price close = sc.bars[st.step].close;
  const Rate buffer = Rate::from_raw(2 * (sc.costs.half_spread.raw + sc.costs.slippage.raw));
  const Price extra = detail::unwrap(mul<Price>(close, buffer, Rounding::Ceil));
  return Price::from_raw(close.raw + extra.raw);
}

// Quantity of `o` that would increase exposure given the current position.
Qty increasing_qty(const RunState& st, OrderSide side, Qty qty, bool reduce_only) {
  if (reduce_only) return Qty{};
  const Position& p = st.position;
  if (p.flat() || p.is_long() == (side == OrderSide::Buy)) return qty;
  return Qty::from_raw(std::max<std::int64_t>(0, qty.raw - abs_qty(p.qty).raw));
}

bool opens_short(const RunState& st, OrderSide side, Qty qty, bool reduce_only) {
  return side == OrderSide::Sell && increasing_qty(st, side, qty, reduce_only).raw > 0;
}

bool magnitude_ok(const Scenario& sc, Qty qty, Price px) {
  const auto u = mul<Qty>(qty, sc.contract.multiplier, Rounding::TowardZero);
  if (!u.ok()) return false;
  const auto n = mul<Money>(u.value(), px, Rounding::Ceil);
  return n.ok() && n.value() <= detail::kMaxNotional;
}

Reason validate_shape(const Scenario& sc, const Command& c) {
  const ContractSpec& k = sc.contract;
  if (c.order_type == OrderType::TakeProfit && !c.reduce_only) return Reason::UnsupportedOrderType;
  if (c.qty.raw <= 0 || c.qty.raw % k.lot_size.raw != 0) return Reason::InvalidQuantity;
  if (!c.reduce_only && c.qty < k.min_qty) return Reason::InvalidQuantity;
  if (c.order_type == OrderType::Market) return Reason::None;
  if (c.order_type == OrderType::TrailingStop) {
    const bool ok = c.trail_distance.raw > 0 && c.trail_distance.raw % k.tick_size.raw == 0;
    return ok ? Reason::None : Reason::InvalidTick;
  }
  if (c.price.raw <= 0 || c.price.raw % k.tick_size.raw != 0) return Reason::InvalidTick;
  return Reason::None;
}

Reason check_reduce_only(const RunState& st, const Command& c) {
  if (!c.reduce_only) return Reason::None;
  const Position& p = st.position;
  if (p.flat() || p.is_long() == (c.side == OrderSide::Buy)) return Reason::ReduceOnlyWouldIncrease;
  return Reason::None;
}

Money required_margin(const Scenario& sc, const RunState& st, const Order& o, Qty inc) {
  const Money n = detail::notional(sc, inc, reference_price(sc, st, o), Rounding::Ceil);
  const Money fee = detail::fee_for(n, sc.contract.taker_fee);
  return Money::from_raw(detail::initial_margin(n, st.leverage).raw + fee.raw);
}

Reason stop_warning(const Scenario& sc, const RunState& st, const Order& o) {
  Price liq;
  const bool stop = o.type == OrderType::StopMarket || o.type == OrderType::TrailingStop;
  if (!stop || !o.reduce_only || !detail::liquidation_price(sc, st.position, &liq)) {
    return Reason::None;
  }
  const bool beyond = st.position.is_long() ? o.price <= liq : o.price >= liq;
  return beyond ? Reason::StopBeyondLiquidation : Reason::None;
}

bool on_tick(Price p, Price tick) { return p.raw > 0 && p.raw % tick.raw == 0; }

// Bracket exits must sit on the right side of the entry reference price.
Reason validate_bracket(const Scenario& sc, const RunState& st, const Command& c, const Order& o) {
  const bool has_stop = c.attach_stop.raw != 0;
  const bool has_tp = c.attach_tp.raw != 0;
  if (!has_stop && !has_tp) return Reason::None;
  if (o.reduce_only || o.type == OrderType::TrailingStop) return Reason::InvalidBracket;
  const Price tick = sc.contract.tick_size;
  if ((has_stop && !on_tick(c.attach_stop, tick)) || (has_tp && !on_tick(c.attach_tp, tick))) return Reason::InvalidTick;
  const Price ref = o.type == OrderType::Market ? sc.bars[st.step].close : o.price;
  const bool buy = o.side == OrderSide::Buy;
  const bool stop_ok = !has_stop || (buy ? c.attach_stop < ref : c.attach_stop > ref);
  const bool tp_ok = !has_tp || (buy ? c.attach_tp > ref : c.attach_tp < ref);
  return stop_ok && tp_ok ? Reason::None : Reason::InvalidBracket;
}

// Initial trailing trigger from the decision-step close.
Price initial_trail(const Scenario& sc, const RunState& st, OrderSide side, Price dist) {
  const Price close = sc.bars[st.step].close;
  const bool sell = side == OrderSide::Sell;
  const Price raw = Price::from_raw(sell ? close.raw - dist.raw : close.raw + dist.raw);
  const Price px = detail::unwrap(round_to_step(raw, sc.contract.tick_size, sell ? Rounding::Floor : Rounding::Ceil));
  return std::max(px, sc.contract.tick_size);
}

Order make_order(const Scenario& sc, const RunState& st, const Command& c) {
  Order o;
  o.id = st.next_order_id;
  o.seq = c.seq;
  o.type = c.order_type;
  o.side = c.side;
  o.qty = c.qty;
  o.price = c.order_type == OrderType::Market ? Price{} : c.price;
  o.reduce_only = c.reduce_only || c.order_type == OrderType::TakeProfit;
  o.placed_step = st.step;
  o.bracket_stop = c.attach_stop;
  o.bracket_tp = c.attach_tp;
  if (c.order_type == OrderType::TrailingStop) {
    o.trail_distance = c.trail_distance;
    o.price = initial_trail(sc, st, c.side, c.trail_distance);
  }
  return o;
}

CommandResult place_order(const Scenario& sc, RunState& st, const Command& c) {
  if (const Reason r = validate_shape(sc, c); r != Reason::None) return reject(r);
  if (const Reason r = check_reduce_only(st, c); r != Reason::None) return reject(r);
  const Order o = make_order(sc, st, c);
  if (const Reason r = validate_bracket(sc, st, c, o); r != Reason::None) return reject(r);
  if (!magnitude_ok(sc, c.qty, reference_price(sc, st, o))) return reject(Reason::NumericOverflow);
  if (!sc.mission.allow_short && opens_short(st, c.side, c.qty, c.reduce_only)) {
    return reject(Reason::ShortNotAllowed);
  }
  const Qty inc = increasing_qty(st, c.side, c.qty, o.reduce_only);
  if (inc.raw > 0) {
    const AccountView view = account_view(sc, st);
    if (required_margin(sc, st, o, inc) > view.available) return reject(Reason::InsufficientMargin);
  }
  st.orders.push_back(o);
  st.next_order_id += 1;
  detail::emit_order_update(st, o, Reason::None);
  return CommandResult{true, Reason::None, o.id, stop_warning(sc, st, o)};
}

CommandResult cancel_order(RunState& st, const Command& c) {
  for (Order& o : st.orders) {
    if (o.id != c.order_id) continue;
    if (!o.active()) return reject(Reason::UnknownOrder);
    o.status = OrderStatus::Cancelled;
    detail::emit_order_update(st, o, Reason::UserCancelled);
    return CommandResult{true, Reason::None, o.id, Reason::None};
  }
  return reject(Reason::UnknownOrder);
}

CommandResult set_leverage(const Scenario& sc, RunState& st, const Command& c) {
  const std::int32_t cap = std::min(sc.mission.leverage_cap, sc.contract.max_leverage);
  if (c.leverage < 1) return reject(Reason::InvalidLeverage);
  if (c.leverage > cap) return reject(Reason::LeverageAboveCap);
  Position& p = st.position;
  if (!p.flat()) {
    const Money n = detail::notional(sc, p.qty, p.entry, Rounding::Ceil);
    const Money new_margin = detail::initial_margin(n, c.leverage);
    const Money extra = Money::from_raw(new_margin.raw - p.margin.raw);
    if (extra > account_view(sc, st).available) return reject(Reason::InsufficientMargin);
    p.margin = new_margin;
  }
  st.leverage = c.leverage;
  Event ev;
  ev.type = EventType::LeverageChanged;
  ev.amount = Money::from_raw(c.leverage);
  detail::push_event(st, ev);
  return CommandResult{true, Reason::None, 0, Reason::None};
}

CommandResult close_position(const Scenario& sc, RunState& st, const Command& c) {
  if (st.position.flat()) return reject(Reason::PositionClosed);
  Command m = c;
  m.order_type = OrderType::Market;
  m.side = st.position.is_long() ? OrderSide::Sell : OrderSide::Buy;
  m.qty = abs_qty(st.position.qty);
  m.reduce_only = true;
  return place_order(sc, st, m);
}

// Spec perpetual-futures "Isolated margin and leverage": add at any decision point; remove only
// while the position stays at or above its initial margin.
CommandResult adjust_margin(const Scenario& sc, RunState& st, const Command& c) {
  Position& p = st.position;
  if (p.flat()) return reject(Reason::PositionClosed);
  if (c.margin_delta.raw == 0) return reject(Reason::InvalidQuantity);
  if (c.margin_delta.raw > 0 && c.margin_delta > account_view(sc, st).available) return reject(Reason::InsufficientMargin);
  if (c.margin_delta.raw < 0) {
    const Money im = detail::initial_margin(detail::notional(sc, p.qty, p.entry, Rounding::Ceil), st.leverage);
    if (p.margin.raw + c.margin_delta.raw < im.raw) return reject(Reason::MarginBelowInitial);
  }
  p.margin = Money::from_raw(p.margin.raw + c.margin_delta.raw);
  Event ev;
  ev.type = EventType::MarginAdjusted;
  ev.amount = c.margin_delta;
  ev.qty = detail::abs_qty(p.qty);
  detail::push_event(st, ev);
  return CommandResult{true, Reason::None, 0, Reason::None};
}

CommandResult record_plan(RunState& st, const Command& c) {
  const PlanRecord& p = c.plan;
  if (p.confidence_pct > 100 || p.invalidation.raw < 0 || p.target.raw < 0 || p.planned_risk.raw < 0) {
    return reject(Reason::InvalidPlan);
  }
  PlanRecord rec = p;
  rec.step = st.step;
  st.plans.push_back(rec);
  return CommandResult{true, Reason::None, 0, Reason::None};
}

CommandResult answer_question(const Scenario& sc, RunState& st, const Command& c) {
  if (!question_due(sc, st)) return reject(Reason::NoQuestionDue);
  if (!c.skipped && c.p_bp > 10000) return reject(Reason::InvalidPlan);
  st.answers.push_back({st.step, sc.bars[st.step].close, kQuestionHorizon, c.skipped ? 0 : c.p_bp, c.skipped});
  return CommandResult{true, Reason::None, 0, Reason::None};
}

CommandResult dispatch(const Scenario& sc, RunState& st, const Command& c) {
  switch (c.type) {
    case CommandType::PlaceOrder: return place_order(sc, st, c);
    case CommandType::CancelOrder: return cancel_order(st, c);
    case CommandType::SetLeverage: return set_leverage(sc, st, c);
    case CommandType::ClosePosition: return close_position(sc, st, c);
    case CommandType::PlaceOptionOrder: return detail::place_option_order(sc, st, c, account_view(sc, st).available);
    case CommandType::AdjustMargin: return adjust_margin(sc, st, c);
    case CommandType::RecordPlan: return record_plan(st, c);
    case CommandType::AnswerQuestion: return answer_question(sc, st, c);
  }
  return reject(Reason::UnsupportedOrderType);
}

const CommandRecord* find_record(const RunState& st, std::uint64_t client_id) {
  for (const CommandRecord& r : st.command_log) {
    if (r.client_id == client_id) return &r;
  }
  return nullptr;
}

Reason precheck(const RunState& st, const Command& c) {
  if (st.status == RunStatus::Bankrupt) return Reason::RunBankrupt;
  if (st.status == RunStatus::Finished) return Reason::RunFinished;
  if (c.seq <= st.last_command_seq) return Reason::InvalidSequence;
  if (c.decision_step != st.step) return Reason::WrongDecisionStep;
  return Reason::None;
}

}  // namespace

CommandResult apply_command(const Scenario& sc, RunState& st, const Command& cmd) {
  if (const CommandRecord* prev = find_record(st, cmd.client_id); prev != nullptr) return prev->result;
  if (const Reason r = precheck(st, cmd); r != Reason::None) return reject(r);
  st.last_command_seq = cmd.seq;
  const CommandResult result = dispatch(sc, st, cmd);
  st.command_log.push_back(CommandRecord{cmd.client_id, result});
  return result;
}

bool question_due(const Scenario& sc, const RunState& st) {
  if (st.status != RunStatus::Active || st.step < sc.mission.warmup_bars) return false;
  if ((st.step - sc.mission.warmup_bars) % kQuestionEvery != 0) return false;
  if (st.step + kQuestionHorizon >= sc.bars.size()) return false;
  for (const CalibrationAnswer& a : st.answers) {
    if (a.step == st.step) return false;
  }
  return true;
}

namespace detail {

Money order_margin_excluding(const Scenario& sc, const RunState& st, std::uint32_t exclude_id) {
  Money total{};
  for (const Order& o : st.orders) {
    if (!o.active() || o.id == exclude_id || o.series != 0) continue;
    const Qty inc = increasing_qty(st, o.side, o.remaining(), o.reduce_only);
    if (inc.raw > 0) total = Money::from_raw(total.raw + required_margin(sc, st, o, inc).raw);
  }
  return total;
}

bool margin_allows_fill(const Scenario& sc, const RunState& st, const Order& o, Qty q, Price px) {
  const Qty inc = increasing_qty(st, o.side, q, o.reduce_only);
  if (inc.raw == 0) return true;
  const Money n = notional(sc, inc, px, Rounding::Ceil);
  const Money need = Money::from_raw(initial_margin(n, st.leverage).raw + fee_for(n, sc.contract.taker_fee).raw);
  const Money avail = Money::from_raw(st.wallet.raw - st.position.margin.raw -
                                      order_margin_excluding(sc, st, o.id).raw);
  return need <= avail;
}

void emit_order_update(RunState& st, const Order& o, Reason reason) {
  Event ev;
  ev.type = EventType::OrderUpdate;
  ev.order_id = o.id;
  ev.status = o.status;
  ev.reason = reason;
  ev.side = o.side;
  ev.qty = o.remaining();
  ev.requested = o.price;
  push_event(st, ev);
}

}  // namespace detail

AccountView account_view(const Scenario& sc, const RunState& st) {
  AccountView v;
  const Position& p = st.position;
  v.mark = sc.bars[st.step].close;
  v.unrealized = detail::unrealized_pnl(sc, p, v.mark);
  v.equity = Money::from_raw(st.wallet.raw + v.unrealized.raw);
  v.position_margin = p.margin;
  v.order_margin = detail::order_margin_excluding(sc, st, 0);
  const detail::OptionBook ob = detail::option_book(sc, st, st.step);
  v.option_long_value = ob.long_value;
  v.option_short_value = ob.short_value;
  v.option_initial_margin = ob.initial_margin;
  v.option_maintenance_margin = ob.maintenance_margin;
  v.equity = Money::from_raw(v.equity.raw + ob.long_value.raw - ob.short_value.raw);
  const std::int64_t free = st.wallet.raw - p.margin.raw - v.order_margin.raw - ob.initial_margin.raw;
  v.available = Money::from_raw(std::max<std::int64_t>(0, free));
  v.notional = detail::notional(sc, p.qty, v.mark, Rounding::HalfEven);
  v.has_liquidation = detail::liquidation_price(sc, p, &v.liquidation_price);
  const std::uint32_t interval = sc.contract.funding_interval;
  v.next_funding_step = (st.step / interval + 1) * interval;
  // Published from the close of bar i-1; only the immediately next bar's rate is visible.
  const std::uint32_t visible = std::min<std::uint32_t>(st.step + 1, static_cast<std::uint32_t>(sc.bars.size() - 1));
  v.next_funding_rate = sc.funding_rate[visible];
  return v;
}

}  // namespace tradelab
