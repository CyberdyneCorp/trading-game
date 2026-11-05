#include "accounting.hpp"

#include <algorithm>

namespace tradelab::detail {

Qty abs_qty(Qty q) { return Qty::from_raw(q.raw < 0 ? -q.raw : q.raw); }

Qty units(const Scenario& sc, Qty contracts) {
  return unwrap(mul<Qty>(contracts, sc.contract.multiplier, Rounding::TowardZero));
}

Money notional(const Scenario& sc, Qty contracts, Price px, Rounding mode) {
  return unwrap(mul<Money>(units(sc, abs_qty(contracts)), px, mode));
}

Money fee_for(Money n, Rate rate) { return unwrap(mul<Money>(n, rate, against_player(true))); }

Money initial_margin(Money n, std::int32_t leverage) {
  return Money::from_raw(static_cast<std::int64_t>(div_round(n.raw, leverage, Rounding::Ceil)));
}

Money unrealized_pnl(const Scenario& sc, const Position& p, Price mark) {
  if (p.flat()) return Money{};
  const Price diff = Price::from_raw(mark.raw - p.entry.raw);
  // Signed units * price difference; rounding down never overstates a gain.
  return unwrap(mul<Money>(units(sc, p.qty), diff, Rounding::Floor));
}

MarginTier margin_tier(const ContractSpec& c, Money n) {
  MarginTier t{Money{}, c.maintenance_margin, c.max_leverage};
  for (const MarginTier& x : c.margin_tiers) {
    if (n < x.notional_floor) break;
    t = x;
  }
  return t;
}

Money notional_after(const Scenario& sc, const Position& p, OrderSide side, Qty inc, Price px) {
  const Money add = notional(sc, inc, px, Rounding::Ceil);
  const bool same_side = !p.flat() && p.is_long() == (side == OrderSide::Buy);
  return same_side ? Money::from_raw(notional(sc, p.qty, p.entry, Rounding::Ceil).raw + add.raw) : add;
}

bool liquidation_price(const Scenario& sc, const Position& p, Price* out) {
  if (p.flat()) return false;
  const Qty u = units(sc, abs_qty(p.qty));
  const Rate mmr = margin_tier(sc.contract, notional(sc, p.qty, p.entry, Rounding::Ceil)).maintenance_margin;
  const Rate one = rate_one();
  if (p.is_long()) {
    // L = (u*E - M) / (u*(1 - mmr)); rounded up so liquidation is never later than the formula.
    const Money num = Money::from_raw(unwrap(mul<Money>(u, p.entry, Rounding::Ceil)).raw - p.margin.raw);
    if (num.raw <= 0) return false;
    const Qty den = unwrap(mul<Qty>(u, Rate::from_raw(one.raw - mmr.raw), Rounding::Floor));
    *out = unwrap(div<Price>(num, den, Rounding::Ceil));
    return true;
  }
  // L = (u*E + M) / (u*(1 + mmr)); rounded down for shorts.
  const Money num = Money::from_raw(unwrap(mul<Money>(u, p.entry, Rounding::Floor)).raw + p.margin.raw);
  const Qty den = unwrap(mul<Qty>(u, Rate::from_raw(one.raw + mmr.raw), Rounding::Ceil));
  *out = unwrap(div<Price>(num, den, Rounding::Floor));
  return true;
}

Price trail_trigger(const Scenario& sc, OrderSide side, Price close, Price distance, Rate rate) {
  const bool sell = side == OrderSide::Sell;
  const Price dist = rate.raw > 0 ? unwrap(mul<Price>(close, rate, Rounding::Ceil)) : distance;
  const Price raw = Price::from_raw(sell ? close.raw - dist.raw : close.raw + dist.raw);
  const Price px = unwrap(round_to_step(raw, sc.contract.tick_size, sell ? Rounding::Floor : Rounding::Ceil));
  return std::max(px, sc.contract.tick_size);
}

void push_event(RunState& st, Event ev) {
  ev.seq = st.next_event_seq++;
  ev.step = st.step;
  st.events.push_back(ev);
}

namespace {

void add_wallet(RunState& st, Money delta) { st.wallet = Money::from_raw(st.wallet.raw + delta.raw); }

// Increases the position in the direction of `side` by q at px.
void increase(const Scenario& sc, RunState& st, OrderSide side, Qty q, Price px) {
  Position& p = st.position;
  const Qty old_abs = abs_qty(p.qty);
  const Qty new_abs = Qty::from_raw(old_abs.raw + q.raw);
  // Weighted average entry (half-even): (old*E + q*px) / (old + q).
  const i128 weighted = static_cast<i128>(old_abs.raw) * p.entry.raw + static_cast<i128>(q.raw) * px.raw;
  p.entry = Price::from_raw(static_cast<std::int64_t>(div_round(weighted, new_abs.raw, Rounding::HalfEven)));
  p.qty = Qty::from_raw(side_sign(side) * new_abs.raw);
  const Money add_margin = initial_margin(notional(sc, q, px, Rounding::Ceil), st.leverage);
  p.margin = Money::from_raw(p.margin.raw + add_margin.raw);
}

// Reduces the position by q (<= |position|) at px; returns realized P&L.
Money reduce(const Scenario& sc, RunState& st, Qty q, Price px) {
  Position& p = st.position;
  const Qty old_abs = abs_qty(p.qty);
  const int dir = p.is_long() ? 1 : -1;
  const Price diff = Price::from_raw(dir * (px.raw - p.entry.raw));
  const Money pnl = unwrap(mul<Money>(units(sc, q), diff, Rounding::Floor));
  const bool closes_all = q.raw == old_abs.raw;
  const Money release =
      closes_all ? p.margin
                 : Money::from_raw(static_cast<std::int64_t>(
                       div_round(static_cast<i128>(p.margin.raw) * q.raw, old_abs.raw, Rounding::Floor)));
  p.margin = Money::from_raw(p.margin.raw - release.raw);
  p.qty = Qty::from_raw(dir * (old_abs.raw - q.raw));
  if (closes_all) p = Position{};
  add_wallet(st, pnl);
  st.realized_pnl = Money::from_raw(st.realized_pnl.raw + pnl.raw);
  return pnl;
}

}  // namespace

void apply_fill(const Scenario& sc, RunState& st, const FillSpec& f) {
  Position& p = st.position;
  Money pnl{};
  Qty rest = f.qty;
  bool closed = false;
  const bool opposite_dir = !p.flat() && (p.is_long() != (f.side == OrderSide::Buy));
  if (opposite_dir) {
    const Qty closing = Qty::from_raw(std::min(rest.raw, abs_qty(p.qty).raw));
    pnl = reduce(sc, st, closing, f.price);
    rest = Qty::from_raw(rest.raw - closing.raw);
    closed = p.flat();
  }
  if (rest.raw > 0) increase(sc, st, f.side, rest, f.price);

  const Money n = notional(sc, f.qty, f.price, Rounding::HalfEven);
  const Money fee = fee_for(n, f.maker ? sc.contract.maker_fee : sc.contract.taker_fee);
  add_wallet(st, Money::from_raw(-fee.raw));
  st.fees_paid = Money::from_raw(st.fees_paid.raw + fee.raw);
  st.metrics.turnover = Money::from_raw(st.metrics.turnover.raw + n.raw);
  st.metrics.fills += 1;
  if ((f.flags & kFlagAmbiguousIntrabar) != 0) st.metrics.ambiguous_fills += 1;

  Event ev;
  ev.type = EventType::Fill;
  ev.order_id = f.order_id;
  ev.side = f.side;
  ev.qty = f.qty;
  ev.price = f.price;
  ev.requested = f.requested;
  ev.amount = fee;
  ev.pnl = pnl;
  ev.flags = f.flags;
  push_event(st, ev);
  // After the fill event, and never the order being filled: it ends `filled`, not `cancelled`.
  if (closed) cancel_orphan_reduce_only(st, Reason::PositionClosed, f.order_id);
}

void cancel_orphan_reduce_only(RunState& st, Reason reason, std::uint32_t keep_order) {
  for (Order& o : st.orders) {
    if (!o.active() || !o.reduce_only || (keep_order != 0 && o.id == keep_order)) continue;
    o.status = OrderStatus::Cancelled;
    emit_order_update(st, o, reason);
  }
}

void liquidate(const Scenario& sc, RunState& st, Price px, std::uint8_t flags) {
  Position& p = st.position;
  const Money lost = p.margin;
  const Money loss_at_price = unrealized_pnl(sc, p, px);  // negative
  const Money shortfall = Money::from_raw(std::max<std::int64_t>(0, -loss_at_price.raw - lost.raw));
  Event ev;
  ev.type = EventType::Liquidation;
  ev.side = p.is_long() ? OrderSide::Sell : OrderSide::Buy;
  ev.qty = abs_qty(p.qty);
  ev.price = px;
  ev.amount = lost;
  ev.pnl = Money::from_raw(-lost.raw);
  ev.flags = flags;
  add_wallet(st, Money::from_raw(-lost.raw));
  st.realized_pnl = Money::from_raw(st.realized_pnl.raw - lost.raw);
  st.insurance_absorbed = Money::from_raw(st.insurance_absorbed.raw + shortfall.raw);
  st.metrics.liquidations += 1;
  p = Position{};
  push_event(st, ev);
  cancel_orphan_reduce_only(st, Reason::Liquidation);
}

void apply_funding(const Scenario& sc, RunState& st, std::uint32_t bar) {
  Position& p = st.position;
  if (p.flat()) return;
  const Rate rate = sc.funding_rate[bar];
  const Money n = notional(sc, p.qty, sc.bars[bar].close, Rounding::HalfEven);
  // Positive rate: longs pay. Payer rounds up, receiver rounds down.
  const bool pays = (rate.raw > 0) == p.is_long();
  const Money magnitude = unwrap(mul<Money>(n, Rate::from_raw(rate.raw < 0 ? -rate.raw : rate.raw),
                                            pays ? Rounding::Ceil : Rounding::Floor));
  const Money delta = Money::from_raw(pays ? -magnitude.raw : magnitude.raw);
  if (delta.raw == 0) return;
  add_wallet(st, delta);
  p.margin = Money::from_raw(p.margin.raw + delta.raw);
  st.funding_net = Money::from_raw(st.funding_net.raw + delta.raw);
  Event ev;
  ev.type = EventType::Funding;
  ev.qty = abs_qty(p.qty);
  ev.price = sc.bars[bar].close;
  ev.amount = delta;
  push_event(st, ev);
}

}  // namespace tradelab::detail
