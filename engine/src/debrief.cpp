// Report-only debrief facts (spec gameplay-loop "Debrief", mission-scoring "Outcome metrics are
// separate"): spread/slippage cost, option P&L attribution by Greek and the leverage that would have
// kept a stop in front of liquidation. Nothing here feeds the score or touches run state; it is
// recomputed from the event log and the scenario with integer arithmetic (option quotes come from
// the same deterministic pricing kernel the engine trades with).

#include <algorithm>
#include <vector>

#include "accounting.hpp"
#include "tradelab/indicators.hpp"
#include "tradelab/options.hpp"
#include "tradelab/scoring.hpp"

namespace tradelab {
namespace {

constexpr i128 kPriceScale = Price::kScale;
constexpr i128 kRateScale = Rate::kScale;
constexpr i128 kVolPoint = Vol::kScale / 100;  // one volatility point (0.01)

std::int64_t abs64(std::int64_t v) { return v < 0 ? -v : v; }
std::int64_t scale(i128 a, i128 b, i128 den) { return static_cast<std::int64_t>(div_round(a * b, den, Rounding::HalfEven)); }

const Order* find_order(const RunState& st, std::uint32_t id) {
  for (const Order& o : st.orders) {
    if (o.id == id) return &o;
  }
  return nullptr;
}

bool is_stop(OrderType t) { return t == OrderType::StopMarket || t == OrderType::TrailingStop; }

// ---- spread / slippage ----------------------------------------------------------------------

// Taker perpetual fills (market, stop, trailing stop) execute at a base price moved against the
// player by half-spread + slippage: the open for market orders and gapped stops, else the trigger.
// Limits and take-profits fill as makers at their price.
Money perp_fill_cost(const Scenario& sc, const RunState& st, const Event& e) {
  const Order* o = find_order(st, e.order_id);
  if (o == nullptr || (o->type != OrderType::Market && !is_stop(o->type))) return Money{};
  const bool at_open = o->type == OrderType::Market || (e.flags & kFlagGapFill) != 0;
  const Price base = at_open ? sc.bars[e.step].open : e.requested;
  return detail::notional(sc, e.qty, Price::from_raw(abs64(e.price.raw - base.raw)), Rounding::HalfEven);
}

// Option orders fill at the bid/ask of the quote at the bar's open (IV of the previous close); a
// margin-call buy-back (no order) at the ask of the close quote. The cost is the distance to the mid.
OptionQuote fill_quote(const Scenario& sc, const OptionSeries& s, const Event& e) {
  if (e.order_id == 0) return quote(sc, s, e.step, sc.bars[e.step].close);
  return quote(sc, s, e.step - 1, sc.bars[e.step].open);
}

Money option_value(const Scenario& sc, std::int64_t qty_raw, std::int64_t per_unit_raw) {
  const std::int64_t u = scale(qty_raw, sc.options.multiplier.raw, Qty::kScale);
  return Money::from_raw(scale(u, per_unit_raw, kPriceScale));
}

Money option_fill_cost(const Scenario& sc, const Event& e) {
  const OptionSeries* s = find_series(sc, e.series);
  if (s == nullptr) return Money{};
  const OptionQuote q = fill_quote(sc, *s, e);
  return option_value(sc, e.qty.raw, abs64(e.price.raw - q.mid.raw));
}

// ---- Greek attribution ----------------------------------------------------------------------

struct Holding {
  std::uint32_t series = 0;
  std::int64_t qty = 0;  // signed raw contracts
};

void apply_fill(std::vector<Holding>& h, const Event& e) {
  auto it = std::find_if(h.begin(), h.end(), [&e](const Holding& x) { return x.series == e.series; });
  if (it == h.end()) it = h.insert(h.end(), Holding{e.series, 0});
  it->qty += (e.side == OrderSide::Buy ? 1 : -1) * e.qty.raw;
  if (it->qty == 0) h.erase(it);
}

void apply_settlement(std::vector<Holding>& h, const Event& e) {
  std::erase_if(h, [&e](const Holding& x) { return x.series == e.series; });
}

// Full revaluation per bar and held series (no Taylor expansion, so a gap or a crash lands in delta and
// gamma with its real size and sign). Each bar is walked in three legs whose mids chain exactly:
//   gap:   quote step b-1, forward previous close -> open      (price only)
//   price: quote step b-1, forward open -> close (settlement) (price only)
//   time:  quote step b-1 -> b at that forward                 (time and IV; intrinsic at expiry)
// A price leg is the mid change: delta = start delta x dF, gamma = the rest (convexity and the smile).
// A time leg is vega = average vega x dIV, theta = the rest. The residual is then only the spread paid
// against the mid at fills and rounding.
struct Leg {
  const OptionSeries& series;
  std::int64_t units;  // signed contract units (Qty raw x multiplier)
};

std::int64_t money(std::int64_t units, std::int64_t px_raw) { return scale(units, px_raw, kPriceScale); }
void add_to(Money& m, std::int64_t v) { m = Money::from_raw(m.raw + v); }

void price_leg(const Scenario& sc, const Leg& l, std::uint32_t step, Price f0, Price f1, GreekAttribution& g) {
  const OptionQuote q0 = quote(sc, l.series, step, f0);
  const OptionQuote q1 = quote(sc, l.series, step, f1);
  if (!q0.listed || !q1.listed) return;
  const std::int64_t delta = scale(money(l.units, f1.raw - f0.raw), q0.delta.raw, kRateScale);
  add_to(g.delta, delta);
  add_to(g.gamma, money(l.units, q1.mid.raw - q0.mid.raw) - delta);
}

void time_leg(const Scenario& sc, const Leg& l, std::uint32_t b, Price f, GreekAttribution& g) {
  const OptionQuote q0 = quote(sc, l.series, b - 1, f);
  const bool expires = l.series.expiry_step == b;
  const OptionQuote q1 = expires ? OptionQuote{} : quote(sc, l.series, b, f);
  if (!q0.listed || (!expires && !q1.listed)) return;
  const std::int64_t end = expires ? intrinsic(l.series, f).raw : q1.mid.raw;
  const std::int64_t vega = expires ? 0 : money(l.units, scale(q0.vega.raw + q1.vega.raw, q1.iv.raw - q0.iv.raw, 2 * kVolPoint));
  add_to(g.vega, vega);
  if (l.units < 0) add_to(g.vega_written, vega);
  add_to(g.theta, money(l.units, end - q0.mid.raw) - vega);
}

Leg leg_of(const Scenario& sc, const Holding& x) {
  return {*find_series(sc, x.series), scale(x.qty, sc.options.multiplier.raw, Qty::kScale)};
}

void gap_interval(const Scenario& sc, const std::vector<Holding>& h, std::uint32_t b, GreekAttribution& g) {
  for (const Holding& x : h) price_leg(sc, leg_of(sc, x), b - 1, sc.bars[b - 1].close, sc.bars[b].open, g);
}

// Fills at the open change the holding before this interval; an expiry at b ends at the settlement price.
void session_interval(const Scenario& sc, const std::vector<Holding>& h, std::uint32_t b, GreekAttribution& g) {
  for (const Holding& x : h) {
    const Leg l = leg_of(sc, x);
    bool approx = false;
    const Price f1 = l.series.expiry_step == b ? settlement_price(sc, b, &approx) : sc.bars[b].close;
    price_leg(sc, l, b - 1, sc.bars[b].open, f1, g);
    time_leg(sc, l, b, f1, g);
  }
}

bool open_fill(const Event& e) { return e.type == EventType::Fill && e.series != 0 && e.order_id != 0; }
bool close_option_event(const Event& e) {
  return e.series != 0 && ((e.type == EventType::Fill && e.order_id == 0) || e.type == EventType::OptionSettlement);
}

void apply_close_events(std::vector<Holding>& h, const std::vector<const Event*>& bar_events) {
  for (const Event* e : bar_events) {
    if (!close_option_event(*e)) continue;
    if (e->type == EventType::Fill) apply_fill(h, *e);
    else apply_settlement(h, *e);
  }
}

// Option P&L before fees: realized plus open positions valued at the final mid.
Money option_total(const Scenario& sc, const RunState& st) {
  std::int64_t total = st.option_realized.raw;
  const Price f = sc.bars[st.step].close;
  for (const OptionPosition& p : st.option_positions) {
    const OptionQuote q = quote(sc, *find_series(sc, p.series), st.step, f);
    if (!q.listed) continue;
    total += option_value(sc, p.qty.raw, q.mid.raw - p.avg_premium.raw).raw;
  }
  return Money::from_raw(total);
}

std::vector<std::vector<const Event*>> option_events_by_step(const RunState& st) {
  std::vector<std::vector<const Event*>> by_step(st.step + 1);
  for (const Event& e : st.events) {
    if (e.series != 0 && e.step <= st.step) by_step[e.step].push_back(&e);
  }
  return by_step;
}

// ---- stop vs liquidation --------------------------------------------------------------------

// Highest leverage the position carried: the run leverage at entry, raised by any re-leverage while
// it was open (which re-margins it and moves its liquidation price). A change at the exit bar comes
// after that bar's fills, so it belongs to the next trade.
std::int32_t leverage_held(const RunState& st, const TradeRow& t) {
  std::int32_t at_entry = 1;  // a run starts at 1x
  std::int32_t peak = 0;
  for (const Event& e : st.events) {
    if (e.type != EventType::LeverageChanged) continue;
    const auto lev = static_cast<std::int32_t>(e.amount.raw);
    if (e.step < t.entry_step) at_entry = lev;
    else if (!t.exit_step || e.step < *t.exit_step) peak = std::max(peak, lev);
  }
  return std::max(at_entry, peak);
}

// First reduce-only stop that protected the trade (bracket legs are placed at the entry fill).
const Order* trade_stop(const RunState& st, const TradeRow& t) {
  const std::uint32_t last = t.exit_step.value_or(st.step);
  const OrderSide exit_side = t.is_long ? OrderSide::Sell : OrderSide::Buy;
  for (const Order& o : st.orders) {
    if (o.series == 0 && o.reduce_only && is_stop(o.type) && o.side == exit_side && o.placed_step >= t.entry_step &&
        o.placed_step <= last) {
      return &o;
    }
  }
  return nullptr;
}

bool liq_at(const Scenario& sc, const TradeRow& t, std::int32_t lev, Price* liq) {
  Position p;
  p.qty = Qty::from_raw(t.is_long ? t.qty.raw : -t.qty.raw);
  p.entry = t.entry_avg;
  p.margin = detail::initial_margin(detail::notional(sc, t.qty, t.entry_avg, Rounding::Ceil), lev);
  return detail::liquidation_price(sc, p, liq);
}

bool stop_in_front(const Scenario& sc, const TradeRow& t, std::int32_t lev) {
  Price liq;
  if (!liq_at(sc, t, lev, &liq)) return true;
  return t.is_long ? t.stop > liq : t.stop < liq;
}

// Unrealized P&L of an open option position at the final quote, valued like the account equity:
// a long at the bid, a short at the ask.
Money option_unrealized(const Scenario& sc, const RunState& st, std::uint32_t series) {
  for (const OptionPosition& p : st.option_positions) {
    if (p.series != series) continue;
    const OptionQuote q = quote(sc, *find_series(sc, series), st.step, sc.bars[st.step].close);
    if (!q.listed) return Money{};
    const Price px = p.qty.raw > 0 ? q.bid : q.ask;
    return option_value(sc, p.qty.raw, px.raw - p.avg_premium.raw);
  }
  return Money{};
}

}  // namespace

void mark_open_trade(const Scenario& sc, const RunState& st, TradeRow& t) {
  if (t.reason != ExitReason::Open) return;
  const Money u = t.series == 0 ? detail::unrealized_pnl(sc, st.position, mark_bar(sc, st.step).close) : option_unrealized(sc, st, t.series);
  t.pnl = Money::from_raw(t.pnl.raw + u.raw);
  t.net = Money::from_raw(t.net.raw + u.raw);
}

Money spread_slippage_cost(const Scenario& sc, const RunState& st) {
  std::int64_t total = 0;
  for (const Event& e : st.events) {
    if (e.type != EventType::Fill) continue;
    total += (e.series == 0 ? perp_fill_cost(sc, st, e) : option_fill_cost(sc, e)).raw;
  }
  return Money::from_raw(total);
}

std::optional<GreekAttribution> greek_attribution(const Scenario& sc, const RunState& st) {
  const bool traded = std::any_of(st.events.begin(), st.events.end(),
                                  [](const Event& e) { return e.type == EventType::Fill && e.series != 0; });
  if (!traded) return std::nullopt;
  GreekAttribution g;
  std::vector<Holding> held;
  const auto by_step = option_events_by_step(st);
  for (std::uint32_t b = sc.mission.warmup_bars + 1; b <= st.step; ++b) {
    if (held.empty() && by_step[b].empty()) continue;
    gap_interval(sc, held, b, g);
    for (const Event* e : by_step[b]) {
      if (open_fill(*e)) apply_fill(held, *e);
    }
    session_interval(sc, held, b, g);
    apply_close_events(held, by_step[b]);
  }
  g.total = option_total(sc, st);
  g.residual = Money::from_raw(g.total.raw - g.delta.raw - g.gamma.raw - g.theta.raw - g.vega.raw);
  return g;
}

namespace {

void tally(SignalTally& t, int direction, std::int64_t move) {
  if (direction == 0 || move == 0) return;
  t.samples += 1;
  t.hits += (move > 0) == (direction > 0) ? 1 : 0;
}

int sign_of(std::int64_t x) {
  if (x == 0) return 0;
  return x > 0 ? 1 : -1;
}

// -1 / 0 / +1: v below lo, between, above hi.
int outside(Price v, Price lo, Price hi) {
  if (v.raw < lo.raw) return -1;
  return v.raw > hi.raw ? 1 : 0;
}

// Indicator reads are 0 while the indicator is still warming up (no value at bar i).
int rsi_side(const Series& rsi14, std::uint32_t i) {
  const std::optional<Price>& v = rsi14[i];
  if (!v.has_value()) return 0;
  return outside(*v, Price::from_raw(30 * Price::kScale), Price::from_raw(70 * Price::kScale));
}

int band_side(const Bollinger& b, std::uint32_t i, Price close) {
  const std::optional<Price>& lo = b.lower[i];
  const std::optional<Price>& hi = b.upper[i];
  if (!lo.has_value() || !hi.has_value()) return 0;
  return outside(close, *lo, *hi);
}

int value_sign(const Series& s, std::uint32_t i) {
  const std::optional<Price>& v = s[i];
  if (!v.has_value()) return 0;
  return sign_of(v->raw);
}

// Which side of the indicator value the reference price is on.
int side_of(const Series& s, std::uint32_t i, Price reference) {
  const std::optional<Price>& v = s[i];
  if (!v.has_value()) return 0;
  return sign_of(reference.raw - v->raw);
}

struct Reads {
  Series rsi14;
  Bollinger bands;
  Macd macd_;
  Series sma20;
};

void read_bar(const Scenario& sc, const Reads& r, std::uint32_t i, RegimeSignals& out) {
  const std::int64_t move = sc.bars[i + kSignalHorizon].close.raw - sc.bars[i + 1].open.raw;
  const Price close = sc.bars[i].close;
  tally(out.rsi_fade, -rsi_side(r.rsi14, i), move);
  tally(out.bollinger_fade, -band_side(r.bands, i, close), move);
  tally(out.macd_follow, value_sign(r.macd_.histogram, i), move);
  tally(out.sma_follow, side_of(r.sma20, i, close), move);
}

}  // namespace

std::vector<RegimeSignals> signals_by_regime(const Scenario& sc, const RunState& st, const std::vector<RegimeSpan>& spans) {
  std::vector<RegimeSignals> out;
  const std::uint32_t first = sc.mission.warmup_bars;
  const std::uint32_t last = st.step;
  if (last <= first) return out;
  const Reads r{rsi(sc, last, 14), bollinger(sc, last, 20, 2), macd(sc, last, 12, 26, 9), sma(sc, last, 20)};
  for (const RegimeSpan& s : spans) {
    const std::uint32_t from = std::max(s.start, first);
    const std::uint32_t to = std::min(s.start + s.bars, last + 1);  // exclusive
    if (from >= to) continue;
    RegimeSignals row;
    row.span = {from, to - from, s.label, s.direction};
    for (std::uint32_t i = from; i < to && i + kSignalHorizon <= last; ++i) read_bar(sc, r, i, row);
    out.push_back(row);
  }
  return out;
}

Money buy_and_hold_pnl(const Scenario& sc, const RunState& st) {
  if (st.step <= sc.mission.warmup_bars) return Money{};
  const Price entry = sc.bars[sc.mission.warmup_bars + 1].open;
  const Price exit = sc.bars[st.step].close;
  const Money budget = run_initial_balance(sc, st);
  const Qty raw = detail::unwrap(div<Qty>(budget, Money::from_raw(entry.raw), Rounding::Floor));
  const Qty qty = detail::unwrap(round_to_step(Qty::from_raw(raw.raw * 999 / 1000), sc.contract.lot_size, Rounding::Floor));
  const Money pnl = detail::unwrap(mul<Money>(qty, Price::from_raw(exit.raw - entry.raw), Rounding::Floor));
  const Money fees = Money::from_raw(detail::fee_for(detail::notional(sc, qty, entry, Rounding::Ceil), sc.contract.taker_fee).raw +
                                     detail::fee_for(detail::notional(sc, qty, exit, Rounding::Ceil), sc.contract.taker_fee).raw);
  return Money::from_raw(pnl.raw - fees.raw);
}

void annotate_stop(const Scenario& sc, const RunState& st, TradeRow& t) {
  if (t.series != 0) return;
  if (const Order* o = trade_stop(st, t)) t.stop = o->price;
  t.leverage = leverage_held(st, t);
  Price liq;
  if (liq_at(sc, t, t.leverage, &liq)) t.liq_price = liq;
  if (t.stop.raw == 0 || stop_in_front(sc, t, t.leverage)) return;
  t.stop_beyond_liq = true;
  for (std::int32_t lev = t.leverage - 1; lev >= 1; --lev) {
    if (stop_in_front(sc, t, lev)) {
      t.safe_leverage = lev;
      return;
    }
  }
}

}  // namespace tradelab
