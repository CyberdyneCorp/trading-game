#include "options_exec.hpp"

#include <algorithm>

#include "accounting.hpp"
#include "order_rules.hpp"
#include "spread_margin.hpp"

namespace tradelab::detail {
Money units_value(const Scenario& sc, Qty contracts, Price per_unit, Rounding mode) {
  const Qty u = unwrap(mul<Qty>(abs_qty(contracts), sc.options.multiplier, Rounding::TowardZero));
  return unwrap(mul<Money>(u, per_unit, mode));
}

// Short-option margin per position (spec: options "Writing options"), quotes at step.
ShortMargin short_margin(const Scenario& sc, const OptionSeries& s, Qty contracts, const OptionQuote& q, Price f) {
  const OptionSpec& o = sc.options;
  const Price otm = Price::from_raw(std::max<std::int64_t>(0, s.kind == OptionKind::Call ? s.strike.raw - f.raw
                                                                                          : f.raw - s.strike.raw));
  const Price im_a = unwrap(mul<Price>(f, o.im_base, Rounding::Ceil));
  const Price im_b = unwrap(mul<Price>(f, o.im_floor, Rounding::Ceil));
  const Price im_unit = Price::from_raw(std::max(im_a.raw - otm.raw, im_b.raw) + q.ask.raw);
  const Price mm_unit = Price::from_raw(unwrap(mul<Price>(f, o.mm_base, Rounding::Ceil)).raw + q.mid.raw);
  return {units_value(sc, contracts, im_unit, Rounding::Ceil), units_value(sc, contracts, mm_unit, Rounding::Ceil)};
}

Money option_fee(const Scenario& sc, Qty contracts, Price premium, Price underlying) {
  const Money by_notional = fee_for(units_value(sc, contracts, underlying, Rounding::Ceil), sc.options.fee_rate);
  const Money cap = unwrap(mul<Money>(units_value(sc, contracts, premium, Rounding::Ceil),
                                      Rate::from_raw(Rate::kScale / 8), Rounding::Ceil));  // 12.5% of premium
  return std::min(by_notional, cap);
}

namespace {

OptionPosition* find_position(RunState& st, std::uint32_t series) {
  for (OptionPosition& p : st.option_positions) {
    if (p.series == series) return &p;
  }
  return nullptr;
}

const OptionPosition* find_position(const RunState& st, std::uint32_t series) {
  for (const OptionPosition& p : st.option_positions) {
    if (p.series == series) return &p;
  }
  return nullptr;
}

void add(Money& m, Money d) { m = Money::from_raw(m.raw + d.raw); }

// Applies an option trade: premium cash, average premium, realized P&L on the closing part.
void apply_option_trade(const Scenario& sc, RunState& st, std::uint32_t series, OrderSide side, Qty q, Price premium,
                        Price underlying, std::uint32_t order_id) {
  OptionPosition* p = find_position(st, series);
  if (p == nullptr) {
    st.option_positions.push_back({series, Qty{}, Price{}});
    p = &st.option_positions.back();
  }
  const int dir = side_sign(side);
  const Money cash = units_value(sc, q, premium, Rounding::HalfEven);
  Money pnl{};
  const bool closing = p->qty.raw != 0 && (p->qty.raw > 0) != (dir > 0);
  const Qty close_q = closing ? Qty::from_raw(std::min(q.raw, abs_qty(p->qty).raw)) : Qty{};
  if (close_q.raw > 0) {
    const Price diff = Price::from_raw((p->qty.raw > 0 ? 1 : -1) * (premium.raw - p->avg_premium.raw));
    pnl = unwrap(mul<Money>(unwrap(mul<Qty>(close_q, sc.options.multiplier, Rounding::TowardZero)), diff, Rounding::Floor));
  }
  const Qty open_q = Qty::from_raw(q.raw - close_q.raw);
  const Qty old_abs = Qty::from_raw(abs_qty(p->qty).raw - close_q.raw);
  if (open_q.raw > 0) {
    const i128 w = static_cast<i128>(old_abs.raw) * p->avg_premium.raw + static_cast<i128>(open_q.raw) * premium.raw;
    p->avg_premium = Price::from_raw(static_cast<std::int64_t>(div_round(w, old_abs.raw + open_q.raw, Rounding::HalfEven)));
  }
  p->qty = Qty::from_raw(p->qty.raw + dir * q.raw);
  const Money fee = option_fee(sc, q, premium, underlying);
  const Money flow = Money::from_raw(dir > 0 ? -cash.raw : cash.raw);
  add(st.wallet, Money::from_raw(flow.raw - fee.raw));
  add(st.option_cash, flow);
  add(st.option_realized, pnl);
  add(st.fees_paid, fee);
  st.metrics.fills += 1;
  if (p->qty.raw == 0) std::erase_if(st.option_positions, [series](const OptionPosition& x) { return x.series == series; });
  Event ev;
  ev.type = EventType::Fill;
  ev.order_id = order_id;
  ev.side = side;
  ev.qty = q;
  ev.price = premium;
  ev.amount = fee;
  ev.pnl = pnl;
  ev.series = series;
  push_event(st, ev);
}

Qty opening_short_qty(const RunState& st, std::uint32_t series, OrderSide side, Qty q) {
  if (side != OrderSide::Sell) return Qty{};
  const OptionPosition* p = find_position(st, series);
  const std::int64_t held_long = p != nullptr && p->qty.raw > 0 ? p->qty.raw : 0;
  return Qty::from_raw(std::max<std::int64_t>(0, q.raw - held_long));
}

// Margin a sell adds under spread margin: the requirement of the holdings with the sale minus
// without it (working buys of this step count as longs at acceptance, design D5).
Money spread_sell_requirement(const Scenario& sc, const RunState& st, const OptionSeries& s, Qty qty, std::uint32_t step,
                              Price f, bool at_acceptance) {
  std::vector<Holding> h = holdings_of(st, at_acceptance);
  const Money before = total_initial(short_requirements(sc, h, step, f));
  add_holding(h, s.id, Qty::from_raw(-qty.raw));
  const Money after = total_initial(short_requirements(sc, h, step, f));
  return Money::from_raw(std::max<std::int64_t>(0, after.raw - before.raw));
}

// Cash or margin a new option order needs at quote `q` (published at `step`, forward f).
Money required_for(const Scenario& sc, const RunState& st, const OptionSeries& s, OrderSide side, Qty qty,
                   const OptionQuote& q, Price f, std::uint32_t step, bool at_acceptance) {
  if (side == OrderSide::Buy) {
    return Money::from_raw(units_value(sc, qty, q.ask, Rounding::Ceil).raw + option_fee(sc, qty, q.ask, f).raw);
  }
  if (sc.options.spread_margin) return spread_sell_requirement(sc, st, s, qty, step, f, at_acceptance);
  const Qty short_q = opening_short_qty(st, s.id, side, qty);
  return short_q.raw > 0 ? short_margin(sc, s, short_q, q, f).initial : Money{};
}

CommandResult reject(Reason r) { return CommandResult{false, r, 0, Reason::None}; }

Reason validate_option(const Scenario& sc, const RunState& st, const Command& c, const OptionSeries* s) {
  const OptionSpec& o = sc.options;
  if (s == nullptr || st.step < s->list_step || st.step >= s->expiry_step) return Reason::SeriesNotListed;
  if (in_settlement_window(sc, *s, st.step + 1)) return Reason::SeriesSettling;  // executes at the next open
  if (c.order_type != OrderType::Market && c.order_type != OrderType::Limit) return Reason::UnsupportedOrderType;
  if (c.qty.raw <= 0 || c.qty.raw % o.lot_size.raw != 0) return Reason::InvalidQuantity;
  if (c.order_type == OrderType::Limit && (c.price.raw <= 0 || c.price.raw % o.premium_tick.raw != 0)) {
    return Reason::InvalidTick;
  }
  if (!o.allow_short && opening_short_qty(st, s->id, c.side, c.qty).raw > 0) return Reason::ShortOptionsNotAllowed;
  return tif_refusal(sc, st, c, true);
}

void cancel_order(RunState& st, Order& o, Reason r) {
  o.status = r == Reason::Expired ? OrderStatus::Expired : OrderStatus::Cancelled;
  emit_order_update(st, o, r);
}

Money available_now(const Scenario& sc, const RunState& st, std::uint32_t exclude) {
  const OptionBook b = option_book(sc, st, st.step);
  return Money::from_raw(st.wallet.raw - st.position.margin.raw - order_margin_excluding(sc, st, exclude).raw -
                         b.initial_margin.raw);
}

void execute_option_order(const Scenario& sc, RunState& st, Order& o, std::uint32_t bar) {
  const OptionSeries* s = find_series(sc, o.series);
  if (in_settlement_window(sc, *s, bar)) {
    cancel_order(st, o, Reason::SeriesSettling);
    return;
  }
  const Price f = sc.bars[bar].open;
  // IV published at the previous close; time to expiry from this bar.
  OptionQuote q = quote(sc, *s, bar - 1, f);
  q = q.listed ? q : OptionQuote{};
  const bool buy = o.side == OrderSide::Buy;
  const Price px = buy ? q.ask : q.bid;
  const bool marketable = q.listed && px.raw > 0 && (o.type == OrderType::Market || (buy ? px <= o.price : px >= o.price));
  if (!marketable) {
    if (o.type == OrderType::Market) cancel_order(st, o, q.listed ? Reason::NoBid : Reason::SeriesNotListed);
    return;
  }
  if (required_for(sc, st, *s, o.side, o.remaining(), q, f, bar - 1, false) > available_now(sc, st, o.id)) {
    cancel_order(st, o, Reason::InsufficientMargin);
    return;
  }
  const Qty q_fill = o.remaining();
  apply_option_trade(sc, st, s->id, o.side, q_fill, px, f, o.id);
  o.filled = o.qty;
  o.status = OrderStatus::Filled;
  emit_order_update(st, o, Reason::None);
  resolve_link(st, o, q_fill);
}

void settle_series(const Scenario& sc, RunState& st, const OptionSeries& s, std::uint32_t bar) {
  bool approx = false;
  const Price spot = settlement_price(sc, bar, &approx);
  const Price value = intrinsic(s, spot);
  for (Order& o : st.orders) {
    if (o.active() && o.series == s.id) cancel_order(st, o, Reason::Expired);
  }
  OptionPosition* p = find_position(st, s.id);
  if (p == nullptr) return;
  const Money cash = units_value(sc, p->qty, value, Rounding::HalfEven);
  const Money flow = Money::from_raw(p->qty.raw > 0 ? cash.raw : -cash.raw);
  const Price diff = Price::from_raw((p->qty.raw > 0 ? 1 : -1) * (value.raw - p->avg_premium.raw));
  const Money pnl = unwrap(mul<Money>(unwrap(mul<Qty>(abs_qty(p->qty), sc.options.multiplier, Rounding::TowardZero)), diff,
                                      Rounding::Floor));
  const Money paid = Money::from_raw(std::max(flow.raw, -st.wallet.raw));  // never below zero
  add(st.insurance_absorbed, Money::from_raw(paid.raw - flow.raw));
  add(st.wallet, paid);
  add(st.option_cash, paid);
  add(st.option_realized, pnl);
  Event ev;
  ev.type = EventType::OptionSettlement;
  ev.qty = p->qty;
  ev.price = spot;
  ev.requested = value;
  ev.amount = paid;
  ev.pnl = pnl;
  ev.series = s.id;
  ev.flags = approx ? kFlagApproximate : kFlagNone;
  push_event(st, ev);
  std::erase_if(st.option_positions, [&s](const OptionPosition& x) { return x.series == s.id; });
}

// Account equity for option margin purposes excludes the isolated perp margin (spec).
std::int64_t option_account_equity(const RunState& st, const OptionBook& b) {
  return st.wallet.raw - st.position.margin.raw + b.long_value.raw - b.short_value.raw;
}

// Maintenance of each short at `bar`'s close: per leg, or paired under spread margin.
std::vector<ShortRequirement> short_maintenance(const Scenario& sc, const RunState& st, std::uint32_t bar) {
  return short_requirements(sc, holdings_of(st, false), bar, sc.bars[bar].close);
}

// Buys back the short with the largest maintenance margin at the ask (taker), then re-checks.
bool buy_back_largest_short(const Scenario& sc, RunState& st, std::uint32_t bar) {
  const Price f = sc.bars[bar].close;
  const OptionPosition* worst = nullptr;
  Money worst_mm{};
  for (const ShortRequirement& r : short_maintenance(sc, st, bar)) {
    if (worst == nullptr || r.maintenance > worst_mm) {
      worst = find_position(st, r.series);
      worst_mm = r.maintenance;
    }
  }
  if (worst == nullptr) return false;
  const OptionPosition target = *worst;
  const OptionQuote q = quote(sc, *find_series(sc, target.series), bar, f);
  apply_option_trade(sc, st, target.series, OrderSide::Buy, abs_qty(target.qty), q.ask, f, 0);
  if (st.wallet.raw < 0) {  // the simulated insurance fund covers any shortfall
    add(st.insurance_absorbed, Money::from_raw(-st.wallet.raw));
    add(st.option_cash, Money::from_raw(-st.wallet.raw));
    st.wallet = Money{};
  }
  return true;
}

void enforce_option_margin(const Scenario& sc, RunState& st, std::uint32_t bar) {
  OptionBook b = option_book(sc, st, bar);
  if (b.maintenance_margin.raw == 0 || option_account_equity(st, b) >= b.maintenance_margin.raw) return;
  Event ev;
  ev.type = EventType::MarginCall;
  ev.amount = b.maintenance_margin;
  ev.reason = Reason::MarginCall;
  push_event(st, ev);
  while (b.maintenance_margin.raw > 0 && option_account_equity(st, b) < b.maintenance_margin.raw) {
    if (!buy_back_largest_short(sc, st, bar)) return;
    b = option_book(sc, st, bar);
  }
}

}  // namespace

OptionBook option_book(const Scenario& sc, const RunState& st, std::uint32_t step) {
  OptionBook b;
  const Price f = sc.bars[step].close;
  for (const OptionPosition& p : st.option_positions) {
    const OptionSeries* s = find_series(sc, p.series);
    const OptionQuote q = quote(sc, *s, step, f);
    if (p.qty.raw > 0) add(b.long_value, units_value(sc, p.qty, q.bid, Rounding::Floor));
    else add(b.short_value, units_value(sc, p.qty, q.ask, Rounding::Ceil));
  }
  for (const ShortRequirement& r : short_requirements(sc, holdings_of(st, false), step, f)) {
    add(b.initial_margin, r.initial);
    add(b.maintenance_margin, r.maintenance);
  }
  return b;
}

Money option_order_requirement(const Scenario& sc, const RunState& st, const OptionSeries& s, OrderSide side, Qty qty) {
  const Price f = sc.bars[st.step].close;
  return required_for(sc, st, s, side, qty, quote(sc, s, st.step, f), f, st.step, true);
}

void fill_option_at_quote(const Scenario& sc, RunState& st, const OptionSeries& s, OrderSide side, Qty qty) {
  const Price f = sc.bars[st.step].close;
  const OptionQuote q = quote(sc, s, st.step, f);
  apply_option_trade(sc, st, s.id, side, qty, side == OrderSide::Buy ? q.ask : q.bid, f, 0);
}

void fill_option_at(const Scenario& sc, RunState& st, const OptionSeries& s, OrderSide side, Qty qty, Price premium) {
  apply_option_trade(sc, st, s.id, side, qty, premium, sc.bars[st.step].close, 0);
}

CommandResult place_option_order(const Scenario& sc, RunState& st, const Command& c, Money available) {
  const OptionSeries* s = find_series(sc, c.series);
  if (const Reason r = validate_option(sc, st, c, s); r != Reason::None) return reject(r);
  const Price f = sc.bars[st.step].close;
  const OptionQuote q = quote(sc, *s, st.step, f);
  if (required_for(sc, st, *s, c.side, c.qty, q, f, st.step, true) > available) return reject(Reason::InsufficientMargin);
  Order o;
  o.id = st.next_order_id++;
  o.seq = c.seq;
  o.type = c.order_type;
  o.side = c.side;
  o.qty = c.qty;
  o.price = c.order_type == OrderType::Limit ? c.price : Price{};
  o.placed_step = st.step;
  o.series = s->id;
  set_tif(o, c);
  st.orders.push_back(o);
  emit_order_update(st, o, Reason::None);
  return CommandResult{true, Reason::None, o.id, Reason::None};
}

void option_open_phase(const Scenario& sc, RunState& st, std::uint32_t bar) {
  for (std::size_t i = 0; i < st.orders.size(); ++i) {
    Order& o = st.orders[i];
    if (o.active() && o.series != 0) execute_option_order(sc, st, o, bar);
  }
}

void option_close_phase(const Scenario& sc, RunState& st, std::uint32_t bar) {
  for (const OptionSeries& s : sc.options.series) {
    if (s.expiry_step == bar) settle_series(sc, st, s, bar);
  }
  enforce_option_margin(sc, st, bar);
}

}  // namespace tradelab::detail
