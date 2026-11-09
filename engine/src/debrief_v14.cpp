// Scoring v14 report-only debrief facts (add-practice-and-advanced-strategies design D13): the bias
// findings with their evidence, risk_limit_exceeded and the position episodes. They read the action log
// (orders, plans, events) and the bars up to each step, never a later bar, and none of them feeds the
// score or a digest.

#include <algorithm>
#include <optional>
#include <vector>

#include "accounting.hpp"
#include "options_exec.hpp"
#include "tradelab/indicators.hpp"
#include "tradelab/scoring.hpp"
#include "tradelab/whatif.hpp"

namespace tradelab {
namespace {

constexpr std::int64_t kBp = 10'000;
constexpr std::uint32_t kRevengeBars = 3;
constexpr std::int64_t kRevengeRatioBp = 15'000;
constexpr std::uint32_t kFomoCloses = 4;
constexpr std::int64_t kFomoExtension = 200;  // hundredths of ATR
constexpr std::uint32_t kAtrPeriod = 14;
constexpr std::uint32_t kSmaPeriod = 20;

std::int64_t abs64(std::int64_t v) { return v < 0 ? -v : v; }
std::int64_t dir_of(OrderSide s) { return s == OrderSide::Buy ? 1 : -1; }
bool perp_fill(const Event& e) { return e.type == EventType::Fill && e.series == 0; }

std::int64_t ratio_bp(i128 num, i128 den) {
  if (den <= 0) return 0;
  return static_cast<std::int64_t>(div_round(num * kBp, den, Rounding::HalfEven));
}

const Order* order_by_id(const RunState& st, std::uint32_t id) {
  for (const Order& o : st.orders) {
    if (o.id == id) return &o;
  }
  return nullptr;
}

bool entry_order(const Order* o) { return o != nullptr && !o->reduce_only && o->parent_id == 0 && o->series == 0; }

// Option contracts held after the events up to `step`: a perpetual order placed then is a hedge of a
// structure (m18, m19), which the bias rules leave alone.
bool holds_options(const RunState& st, std::uint32_t step) {
  std::vector<std::pair<std::uint32_t, std::int64_t>> held;
  for (const Event& e : st.events) {
    if (e.step > step) break;
    const bool fill = e.type == EventType::Fill && e.series != 0;
    if (!fill && e.type != EventType::OptionSettlement) continue;
    auto it = std::find_if(held.begin(), held.end(), [&e](const auto& h) { return h.first == e.series; });
    if (it == held.end()) it = held.insert(held.end(), {e.series, 0});
    if (fill) it->second += (e.side == OrderSide::Buy ? 1 : -1) * e.qty.raw;
    else it->second = 0;
  }
  return std::any_of(held.begin(), held.end(), [](const auto& h) { return h.second != 0; });
}

// An exposure-increasing perpetual entry the bias rules judge: no reduce-only exit, no bracket child,
// and no option structure held when it was decided.
bool speculative_entry(const RunState& st, const Order* o) { return entry_order(o) && !holds_options(st, o->placed_step); }
bool is_stop(const Order& o) { return o.type == OrderType::StopMarket || o.type == OrderType::TrailingStop; }

// Last step an order could act on: the step of its last update or fill, or "still working".
std::uint32_t end_step(const RunState& st, const Order& o) {
  if (o.active()) return UINT32_MAX;
  std::uint32_t end = o.placed_step;
  for (const Event& e : st.events) {
    if (e.order_id == o.id && (e.type == EventType::OrderUpdate || e.type == EventType::Fill)) end = e.step;
  }
  return end;
}

// The protective stop of an entry: its bracket stop, else the nearest reduce-only perpetual stop on the
// exit side that was working at decision step d.
Price protective_stop(const RunState& st, const Order& entry, std::uint32_t d) {
  if (entry.bracket_stop.raw > 0) return entry.bracket_stop;
  Price best{};
  const OrderSide exit = entry.side == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy;
  for (const Order& o : st.orders) {
    if (o.series != 0 || !o.reduce_only || !is_stop(o) || o.side != exit || o.placed_step > d || end_step(st, o) < d) continue;
    const bool nearer = best.raw == 0 || (exit == OrderSide::Sell ? o.price > best : o.price < best);
    if (nearer) best = o.price;
  }
  return best;
}

// ---- A running perpetual position over the fills, as the engine books it ----

struct Book {
  std::int64_t qty = 0;  // signed raw Qty
  Price entry;           // average entry (half-even weighted, like accounting's increase)

  // Applies a fill; returns the quantity (raw, unsigned) that grew the position.
  std::int64_t apply(OrderSide side, Qty q, Price px) {
    const std::int64_t d = dir_of(side);
    std::int64_t rest = q.raw;
    if (qty != 0 && (qty > 0) != (d > 0)) {
      const std::int64_t closing = std::min(rest, abs64(qty));
      qty += d * closing;
      rest -= closing;
      if (qty == 0) entry = Price{};
    }
    if (rest == 0) return 0;
    const std::int64_t old_abs = abs64(qty);
    const i128 w = static_cast<i128>(old_abs) * entry.raw + static_cast<i128>(rest) * px.raw;
    entry = Price::from_raw(static_cast<std::int64_t>(div_round(w, old_abs + rest, Rounding::HalfEven)));
    qty = d * (old_abs + rest);
    return rest;
  }
};

Money risk_to(Price avg, Price stop, std::int64_t qty_abs) {
  if (stop.raw == 0 || qty_abs == 0) return Money{};
  const std::int64_t gap = abs64(avg.raw - stop.raw);
  return Money::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(gap) * qty_abs, Qty::kScale, Rounding::HalfEven)));
}

// ---- revenge_trade ----

Money size_of(const PlanRecord* plan, const Order& o) {
  if (plan != nullptr && !plan->skipped && plan->planned_risk.raw > 0) return plan->planned_risk;
  return Money::from_raw(o.qty.raw);  // quantity, compared with quantity
}

// The first exposure-increasing entry after `t` closed at a loss, decided within kRevengeBars of its exit.
std::optional<Finding> revenge_after(const RunState& st, const TradeRow& t, std::uint32_t exit) {
  for (const Order& o : st.orders) {
    if (o.placed_step < exit || o.placed_step > exit + kRevengeBars || o.filled.raw == 0 || !speculative_entry(st, &o)) continue;
    const PlanRecord* plan = plan_of_order(st, o.id);
    const bool planned = plan != nullptr && !plan->skipped;
    const bool by_risk = t.planned_risk.raw > 0 && planned;
    const i128 now = by_risk ? size_of(plan, o).raw : o.qty.raw;
    const i128 before = by_risk ? t.planned_risk.raw : t.qty.raw;
    const std::int64_t ratio = ratio_bp(now, before);
    if (planned && ratio < kRevengeRatioBp) continue;
    FindingEvidence ev;
    ev.trade = t.n;
    ev.exit_step = exit;
    ev.entry_step = o.placed_step;
    ev.ratio_bp = static_cast<std::int32_t>(std::min<std::int64_t>(ratio, INT32_MAX));
    ev.planned = planned;
    ev.loss = Money::from_raw(-t.net.raw);
    Finding f{FindingCode::RevengeTrade, FindingTone::Warn, o.placed_step, static_cast<std::uint32_t>(ev.ratio_bp), Price{}, Price{}, ev.loss, ev};
    return f;
  }
  return std::nullopt;
}

void revenge_findings(const RunState& st, const std::vector<TradeRow>& trades, std::vector<Finding>& out) {
  for (const TradeRow& t : trades) {
    if (t.series != 0 || !t.exit_step || t.net.raw >= 0) continue;
    if (const std::optional<Finding> f = revenge_after(st, t, *t.exit_step)) out.push_back(*f);
  }
}

// ---- fomo_entry ----

struct Reads {
  Series atr;
  Series sma;
};

std::uint32_t closes_in_direction(const Scenario& sc, std::uint32_t d, std::int64_t dir) {
  std::uint32_t k = 0;
  while (d > k && dir * (sc.bars[d - k].close.raw - sc.bars[d - k - 1].close.raw) > 0) ++k;
  return k;
}

std::optional<Finding> fomo_at(const Scenario& sc, const RunState& st, const Reads& r, const Order& o) {
  const std::uint32_t d = o.placed_step;
  const std::int64_t dir = dir_of(o.side);
  const std::uint32_t closes = closes_in_direction(sc, d, dir);
  const std::optional<Price> a = r.atr[d];
  const std::optional<Price> m = r.sma[d];
  if (closes < kFomoCloses || !a || !m || a->raw <= 0) return std::nullopt;
  const std::int64_t close = sc.bars[d].close.raw;
  const auto ext = static_cast<std::int64_t>(div_round(static_cast<i128>(dir) * (close - m->raw) * 100, a->raw, Rounding::HalfEven));
  if (ext <= kFomoExtension) return std::nullopt;
  const Price stop = protective_stop(st, o, d);
  if (stop.raw != 0 && dir * (close - stop.raw) <= 2 * a->raw) return std::nullopt;
  FindingEvidence ev;
  ev.entry_step = d;
  ev.extension_atr = static_cast<std::int32_t>(ext);
  ev.closes = closes;
  ev.stop = stop;
  return Finding{FindingCode::FomoEntry, FindingTone::Warn, d, closes, stop, Price{}, Money{}, ev};
}

void fomo_findings(const Scenario& sc, const RunState& st, std::vector<Finding>& out) {
  const std::uint32_t last = st.step;
  const Reads r{atr(sc, last, kAtrPeriod), sma(sc, last, kSmaPeriod)};
  Book book;
  std::vector<std::uint32_t> seen;
  for (const Event& e : st.events) {
    if (!perp_fill(e)) continue;
    const bool flat_or_same = book.qty == 0 || (book.qty > 0) == (e.side == OrderSide::Buy);
    const std::int64_t grew = book.apply(e.side, e.qty, e.price);
    const Order* o = order_by_id(st, e.order_id);
    if (grew == 0 || !flat_or_same || !speculative_entry(st, o) || std::find(seen.begin(), seen.end(), o->id) != seen.end()) continue;
    seen.push_back(o->id);
    if (const std::optional<Finding> f = fomo_at(sc, st, r, *o)) out.push_back(*f);
  }
}

// ---- averaging_down ----

struct OpenPosition {
  Book book;
  Money first_plan;  // the first entry's planned risk (0 = none)
};

std::optional<Finding> averaging_at(const Scenario& sc, const RunState& st, const OpenPosition& before, const Event& e, const Order& o) {
  const std::uint32_t d = o.placed_step;
  const std::int64_t dir = before.book.qty > 0 ? 1 : -1;
  const Price avg = before.book.entry;
  const std::int64_t q_before = abs64(before.book.qty);
  const i128 unreal = static_cast<i128>(dir) * (sc.bars[d].close.raw - avg.raw) * q_before / Qty::kScale;
  if (dir * (e.price.raw - avg.raw) >= 0 || unreal >= 0) return std::nullopt;
  Book after = before.book;
  after.apply(e.side, e.qty, e.price);
  const Price stop = protective_stop(st, o, d);
  FindingEvidence ev;
  ev.entry_step = d;
  ev.avg_before = avg;
  ev.avg_after = after.entry;
  ev.stop = stop;
  ev.loss = Money::from_raw(static_cast<std::int64_t>(-unreal));
  ev.risk_before = risk_to(avg, stop, q_before);
  ev.risk_after = risk_to(after.entry, stop, abs64(after.qty));
  ev.risk_plan = before.first_plan;
  const PlanRecord* plan = plan_of_order(st, o.id);
  ev.planned = plan != nullptr && !plan->skipped;
  const Money limit = before.first_plan.raw > 0 ? before.first_plan : ev.risk_before;
  if (stop.raw != 0 && ev.risk_after <= limit) return std::nullopt;
  return Finding{FindingCode::AveragingDown, FindingTone::Warn, d, 0, e.price, avg, ev.loss, ev};
}

Money first_plan_risk(const RunState& st, const Order* o) {
  const PlanRecord* p = o != nullptr ? plan_of_order(st, o->id) : nullptr;
  return p != nullptr && !p->skipped ? p->planned_risk : Money{};
}

void averaging_findings(const Scenario& sc, const RunState& st, std::vector<Finding>& out) {
  OpenPosition pos;
  std::vector<std::uint32_t> seen;
  for (const Event& e : st.events) {
    if (e.type == EventType::Liquidation) pos = OpenPosition{};
    if (!perp_fill(e)) continue;
    const Order* o = order_by_id(st, e.order_id);
    const bool adds = pos.book.qty != 0 && (pos.book.qty > 0) == (e.side == OrderSide::Buy) && speculative_entry(st, o);
    if (adds && std::find(seen.begin(), seen.end(), o->id) == seen.end()) {
      seen.push_back(o->id);
      if (const std::optional<Finding> f = averaging_at(sc, st, pos, e, *o)) out.push_back(*f);
    }
    const bool was_flat = pos.book.qty == 0;
    pos.book.apply(e.side, e.qty, e.price);
    if (pos.book.qty == 0) pos.first_plan = Money{};
    else if (was_flat) pos.first_plan = first_plan_risk(st, o);
  }
}

// ---- risk_limit_exceeded: the holdings at each close, rebuilt from the events ----

// The account as the engine books it, applied event by event (accounting.cpp, options_exec.cpp).
struct Shadow {
  RunState st;

  void perp_fill_event(const Event& e) {
    Book b{st.position.qty.raw, st.position.entry};
    b.apply(e.side, e.qty, e.price);
    st.position.qty = Qty::from_raw(b.qty);
    st.position.entry = b.entry;
    st.wallet = Money::from_raw(st.wallet.raw + e.pnl.raw - e.amount.raw);
  }
  void option_fill_event(const Scenario& sc, const Event& e) {
    auto it = std::find_if(st.option_positions.begin(), st.option_positions.end(), [&e](const OptionPosition& p) { return p.series == e.series; });
    if (it == st.option_positions.end()) it = st.option_positions.insert(st.option_positions.end(), {e.series, Qty{}, Price{}});
    it->qty = Qty::from_raw(it->qty.raw + dir_of(e.side) * e.qty.raw);
    const Money cash = detail::units_value(sc, e.qty, e.price, Rounding::HalfEven);
    st.wallet = Money::from_raw(st.wallet.raw + (e.side == OrderSide::Buy ? -cash.raw : cash.raw) - e.amount.raw);
    st.wallet = Money::from_raw(std::max<std::int64_t>(0, st.wallet.raw));  // a forced buy-back floors it
    if (it->qty.raw == 0) st.option_positions.erase(it);
  }
  void apply(const Scenario& sc, const Event& e) {
    if (perp_fill(e)) perp_fill_event(e);
    else if (e.type == EventType::Fill) option_fill_event(sc, e);
    else if (e.type == EventType::Funding) st.wallet = Money::from_raw(st.wallet.raw + e.amount.raw);
    else if (e.type == EventType::Liquidation) {
      st.wallet = Money::from_raw(st.wallet.raw - e.amount.raw);
      st.position = Position{};
    } else if (e.type == EventType::OptionSettlement) {
      st.wallet = Money::from_raw(st.wallet.raw + e.amount.raw);
      std::erase_if(st.option_positions, [&e](const OptionPosition& p) { return p.series == e.series; });
    }
  }
};

struct LimitTally {
  RiskLimitKind kind = RiskLimitKind::Delta;
  std::uint32_t limit_bp = 0;
  std::uint32_t closes = 0;
  std::uint32_t first = 0;
  std::int64_t worst = 0;
};

void tally(std::vector<LimitTally>& out, const RiskLimitCheck& c, std::uint32_t step) {
  auto it = std::find_if(out.begin(), out.end(), [&c](const LimitTally& t) { return t.kind == c.kind; });
  if (it == out.end()) it = out.insert(out.end(), LimitTally{c.kind, c.limit_bp, 0, step, c.value_bp});
  it->closes += 1;
  it->worst = std::max(it->worst, c.value_bp);
}

// The holdings' risk block at this close (the what-if with holdings) against each declared limit.
void measure(const Scenario& sc, const RunState& holdings, std::vector<LimitTally>& seen) {
  if (holdings.position.flat() && holdings.option_positions.empty()) return;
  WhatIfRequest req;
  req.holdings = true;
  const WhatIfResult r = run_option_whatif(sc, holdings, req);
  if (!r.risk) return;
  for (const RiskLimitCheck& c : r.risk->limits) {
    if (c.exceeded) tally(seen, c, holdings.step);
  }
}

void risk_limit_findings(const Scenario& sc, const RunState& st, std::vector<Finding>& out) {
  if (!sc.mission.risk_limits.any()) return;
  Shadow sh;
  sh.st.wallet = run_initial_balance(sc, st);
  sh.st.options = st.options;
  std::vector<LimitTally> seen;
  std::size_t next = 0;
  for (std::uint32_t s = sc.mission.warmup_bars; s <= st.step; ++s) {
    for (; next < st.events.size() && st.events[next].step <= s; ++next) sh.apply(sc, st.events[next]);
    sh.st.step = s;
    measure(sc, sh.st, seen);
  }
  for (const LimitTally& t : seen) {
    FindingEvidence ev;
    ev.limit = t.kind;
    ev.limit_bp = t.limit_bp;
    ev.worst_bp = t.worst;
    out.push_back({FindingCode::RiskLimitExceeded, FindingTone::Warn, t.first, t.closes, Price{}, Price{}, Money{}, ev});
  }
}

// ---- position episodes ----

struct OpenEpisode {
  Episode ep;
  std::int64_t max_qty = 0;
};

void close_episode(std::vector<Episode>& out, std::optional<OpenEpisode>& cur, std::uint32_t step) {
  if (!cur) return;
  cur->ep.exit_step = step;
  out.push_back(cur->ep);
  cur.reset();
}

void on_episode_fill(std::vector<Episode>& out, std::optional<OpenEpisode>& cur, Book& book, const Event& e) {
  const std::int64_t before = book.qty;
  const bool reduces = before != 0 && (before > 0) != (e.side == OrderSide::Buy);
  const std::int64_t closing = reduces ? std::min(e.qty.raw, abs64(before)) : 0;
  book.apply(e.side, e.qty, e.price);
  if (closing > 0 && cur) cur->ep.exits.push_back({e.step, Qty::from_raw(closing), e.price});
  if (reduces && abs64(before) == closing) close_episode(out, cur, e.step);
  const std::int64_t opened = e.qty.raw - closing;
  if (opened == 0) return;
  if (!cur) {
    cur = OpenEpisode{};
    cur->ep.is_long = e.side == OrderSide::Buy;
    cur->ep.entry_step = e.step;
  }
  cur->ep.entries.push_back({e.step, Qty::from_raw(opened), e.price});
  cur->max_qty = std::max(cur->max_qty, abs64(book.qty));
}

std::vector<Episode> walk_episodes(const RunState& st) {
  std::vector<Episode> out;
  std::optional<OpenEpisode> cur;
  Book book;
  for (const Event& e : st.events) {
    if (e.type == EventType::Liquidation && cur) {
      cur->ep.exits.push_back({e.step, e.qty, e.price});
      close_episode(out, cur, e.step);
      book = Book{};
    } else if (perp_fill(e)) {
      on_episode_fill(out, cur, book, e);
    }
  }
  if (cur) out.push_back(cur->ep);
  return out;
}

bool within(const Episode& ep, std::uint32_t step) { return step >= ep.entry_step && (!ep.exit_step || step < *ep.exit_step); }

bool movable_stop(const Order* o) { return o != nullptr && o->series == 0 && o->reduce_only && o->type == OrderType::StopMarket; }

// Away from the price, toward it, or to (or past) the average entry on the favourable side.
StopMoveKind move_kind(const Episode& ep, const TradeRow& t, std::int64_t before, Price after) {
  const bool raised = after.raw > before;
  if (raised != ep.is_long) return StopMoveKind::Away;
  const bool past_entry = ep.is_long ? after >= t.entry_avg : after <= t.entry_avg;
  return past_entry ? StopMoveKind::ToBreakeven : StopMoveKind::Toward;
}

// Reduce-only perpetual stop moves (accepted MoveOrder), each against the trigger before it.
void add_stop_moves(const RunState& st, const TradeRow& t, Episode& ep) {
  std::vector<std::pair<std::uint32_t, std::int64_t>> last;  // order id -> last trigger seen
  for (const Event& e : st.events) {
    if (e.type != EventType::OrderUpdate) continue;
    auto it = std::find_if(last.begin(), last.end(), [&e](const auto& p) { return p.first == e.order_id; });
    if (it == last.end()) {
      last.emplace_back(e.order_id, e.requested.raw);
      continue;
    }
    const bool counted = e.reason == Reason::OrderMoved && movable_stop(order_by_id(st, e.order_id)) && within(ep, e.step);
    if (counted && e.requested.raw != it->second) ep.stop_moves.push_back({e.step, move_kind(ep, t, it->second, e.requested), e.requested});
    it->second = e.requested.raw;
  }
}

bool used_trailing(const RunState& st, const Episode& ep) {
  const std::uint32_t last = ep.exit_step.value_or(st.step);
  return std::any_of(st.orders.begin(), st.orders.end(), [&](const Order& o) {
    return o.series == 0 && o.type == OrderType::TrailingStop && o.reduce_only && o.placed_step <= last && end_step(st, o) >= ep.entry_step;
  });
}

// Best and worst excursion of the bars held against the first entry, for the largest size held.
void add_excursions(const Scenario& sc, const RunState& st, Episode& ep, std::int64_t max_qty) {
  if (ep.planned_risk.raw <= 0 || ep.entries.empty()) return;
  const std::int64_t first = ep.entries.front().price.raw;
  std::int64_t best = 0;
  std::int64_t worst = 0;
  for (std::uint32_t b = ep.entry_step; b <= ep.exit_step.value_or(st.step) && b < sc.bars.size(); ++b) {
    const std::int64_t up = sc.bars[b].high.raw - first;
    const std::int64_t down = first - sc.bars[b].low.raw;
    best = std::max(best, ep.is_long ? up : down);
    worst = std::max(worst, ep.is_long ? down : up);
  }
  const auto in_r = [&](std::int64_t gap) {
    return ratio_bp(static_cast<i128>(gap) * max_qty / Qty::kScale, ep.planned_risk.raw);
  };
  ep.mfe_r_bp = in_r(best);
  ep.mae_r_bp = in_r(worst);
}

std::vector<std::int64_t> max_quantities(const RunState& st) {
  std::vector<std::int64_t> out;
  Book book;
  std::int64_t peak = 0;
  for (const Event& e : st.events) {
    if (e.type == EventType::Liquidation && book.qty != 0) {
      out.push_back(peak);
      book = Book{};
      peak = 0;
    }
    if (!perp_fill(e)) continue;
    const std::int64_t before = book.qty;
    book.apply(e.side, e.qty, e.price);
    const bool flipped = before != 0 && (book.qty == 0 || (book.qty > 0) != (before > 0));
    if (flipped) {
      out.push_back(std::max(peak, abs64(before)));
      peak = 0;
    }
    peak = std::max(peak, abs64(book.qty));
  }
  if (book.qty != 0) out.push_back(peak);
  return out;
}

}  // namespace

std::vector<Finding> v14_findings(const Scenario& sc, const RunState& st, const std::vector<TradeRow>& trades) {
  std::vector<Finding> out;
  revenge_findings(st, trades, out);
  fomo_findings(sc, st, out);
  averaging_findings(sc, st, out);
  risk_limit_findings(sc, st, out);
  std::stable_sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) { return a.step < b.step; });
  return out;
}

std::vector<Episode> position_episodes(const Scenario& sc, const RunState& st, const std::vector<TradeRow>& trades) {
  std::vector<Episode> eps = walk_episodes(st);
  const std::vector<std::int64_t> peaks = max_quantities(st);
  std::vector<const TradeRow*> perp;
  for (const TradeRow& t : trades) {
    if (t.series == 0) perp.push_back(&t);
  }
  for (std::size_t i = 0; i < eps.size(); ++i) {
    Episode& ep = eps[i];
    if (i < perp.size()) {
      const TradeRow& t = *perp[i];
      ep.n = t.n;
      ep.net = t.net;
      ep.planned_risk = t.planned_risk;
      ep.r_bp = t.r_bp;
      add_stop_moves(st, t, ep);
    }
    ep.trailing = used_trailing(st, ep);
    add_excursions(sc, st, ep, i < peaks.size() ? peaks[i] : 0);
  }
  return eps;
}

}  // namespace tradelab
