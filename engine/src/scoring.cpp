#include "tradelab/scoring.hpp"

#include "tradelab/lessons.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace tradelab {
namespace {

constexpr std::int64_t kBp = 10000;

std::int64_t abs64(std::int64_t v) { return v < 0 ? -v : v; }
std::int64_t sign_of(OrderSide s) { return s == OrderSide::Buy ? 1 : -1; }
bool perp_fill(const Event& e) { return e.type == EventType::Fill && e.series == 0; }
bool option_fill(const Event& e) { return e.type == EventType::Fill && e.series != 0; }

std::int64_t round_div(i128 num, i128 den) { return static_cast<std::int64_t>(div_round(num, den, Rounding::HalfEven)); }

// ---- trades ------------------------------------------------------------------------------

struct OpenTrade {
  TradeRow row;
  i128 entry_notional = 0;  // sum qty.raw * price.raw
  std::int64_t entry_qty = 0;
  i128 exit_notional = 0;
  std::int64_t exit_qty = 0;
  std::vector<std::uint32_t> entry_orders;  // orders whose fills opened or added to the trade
};

void add_entry_order(OpenTrade& t, std::uint32_t order_id) {
  if (std::find(t.entry_orders.begin(), t.entry_orders.end(), order_id) == t.entry_orders.end()) t.entry_orders.push_back(order_id);
}

OpenTrade open_trade(std::uint32_t n, const Event& e, std::int64_t qty_abs) {
  OpenTrade t;
  t.row.n = n;
  t.row.series = e.series;
  t.row.is_long = e.side == OrderSide::Buy;
  t.row.entry_step = e.step;
  t.row.qty = Qty::from_raw(qty_abs);
  t.row.entry_avg = e.price;
  t.entry_notional = static_cast<i128>(qty_abs) * e.price.raw;
  t.entry_qty = qty_abs;
  add_entry_order(t, e.order_id);
  return t;
}

const Order* find_order(const RunState& st, std::uint32_t id) {
  const auto it = std::find_if(st.orders.begin(), st.orders.end(), [id](const Order& o) { return o.id == id; });
  return it != st.orders.end() ? &*it : nullptr;
}

// Orders a plan can belong to: placed by the player, not reduce-only and not a bracket child.
bool plannable(const Order& o) { return !o.reduce_only && o.parent_id == 0; }

// Plannable orders placed at the same decision step, on the same kind of instrument and side.
bool same_slot(const Order& a, const Order& b) {
  return plannable(a) && a.placed_step == b.placed_step && (a.series != 0) == (b.series != 0) &&
         a.side == b.side;
}

// Spec mission-scoring "Plan per entry": a plan is recorded for the next exposure-increasing order,
// in the same decision step. The state keeps plans and orders apart, so the plans and orders of one
// step, kind and side are paired from the last one back: a retry after a rejected order records a
// newer plan, which supersedes the rejected attempt's, and two orders sent in one step (a second
// option leg, an add) each keep their own plan instead of the later one's.
const PlanRecord* order_plan(const RunState& st, std::uint32_t order_id) {
  const Order* o = find_order(st, order_id);
  if (o == nullptr || !plannable(*o)) return nullptr;
  const auto later = static_cast<std::size_t>(
      std::count_if(st.orders.begin(), st.orders.end(), [o](const Order& x) { return x.id > o->id && same_slot(x, *o); }));
  std::vector<const PlanRecord*> plans;
  for (const PlanRecord& p : st.plans) {
    if (p.step == o->placed_step && p.option == (o->series != 0) && p.side == o->side) plans.push_back(&p);
  }
  return later < plans.size() ? plans[plans.size() - 1 - later] : nullptr;
}

struct EntryPlans {
  const PlanRecord* first = nullptr;  // first non-skipped plan: invalidation, stop and verdict
  Money risk;                         // planned risk of all non-skipped plans (an add adds its risk)
  Money largest_declared;             // largest risk declared for one entry, skipped plans included
};

EntryPlans entry_plans(const RunState& st, const OpenTrade& o) {
  EntryPlans r;
  for (const std::uint32_t id : o.entry_orders) {
    const PlanRecord* p = order_plan(st, id);
    if (p == nullptr) continue;
    r.largest_declared = std::max(r.largest_declared, p->planned_risk);
    if (p->skipped) continue;
    if (r.first == nullptr) r.first = p;
    r.risk = Money::from_raw(r.risk.raw + p->planned_risk.raw);
  }
  return r;
}

// The order type names the exit (scoring v10): a stop that exits in profit is still a stop, and a
// trailing stop is told apart from a fixed one. Limits and take-profits are targets.
ExitReason exit_reason(const RunState& st, const TradeRow& t, const Event& fill) {
  if (fill.series != 0 && fill.order_id == 0) return ExitReason::MarginCall;  // forced buy-back has no order
  if (fill.requested.raw == 0) return ExitReason::Manual;
  const Order* o = find_order(st, fill.order_id);
  if (o != nullptr && o->type == OrderType::TrailingStop) return ExitReason::Trailing;
  if (o != nullptr && o->type == OrderType::StopMarket) return ExitReason::Stop;
  if (o != nullptr) return ExitReason::Target;
  const bool favorable = t.is_long ? fill.requested > t.entry_avg : fill.requested < t.entry_avg;
  return favorable ? ExitReason::Target : ExitReason::Stop;
}

std::optional<bool> respected(const TradeRow& t, const PlanRecord* plan) {
  // The invalidation is an underlying price; it can't be compared with an option premium.
  if (plan == nullptr || t.series != 0 || plan->invalidation.raw == 0 || t.exit_avg.raw == 0 || t.reason == ExitReason::Open) {
    return std::nullopt;
  }
  const std::int64_t tol = plan->invalidation.raw / 100;  // 1%
  return t.is_long ? t.exit_avg.raw >= plan->invalidation.raw - tol : t.exit_avg.raw <= plan->invalidation.raw + tol;
}

// Scoring v12: a trade closed by its stop, triggered at or inside the plan's invalidation (1%
// tolerance), kept its plan even when a gap or slippage filled it further away: the market chose the
// fill price, not the player. `invalidation` is the plan's; t.stop is the stop order's trigger.
void credit_stop_exit(TradeRow& t, Price invalidation) {
  if (t.plan_respected != false || t.reason != ExitReason::Stop || invalidation.raw == 0 || t.stop.raw == 0) return;
  const std::int64_t tol = invalidation.raw / 100;
  t.plan_respected = t.is_long ? t.stop.raw >= invalidation.raw - tol : t.stop.raw <= invalidation.raw + tol;
}

TradeRow finalize(const RunState& st, const OpenTrade& o) {
  TradeRow t = o.row;
  if (o.exit_qty > 0) t.exit_avg = Price::from_raw(round_div(o.exit_notional, o.exit_qty));
  t.net = Money::from_raw(t.pnl.raw - t.fees.raw + t.funding.raw);
  t.bars = t.exit_step.value_or(st.step) - t.entry_step;
  const EntryPlans plans = entry_plans(st, o);
  t.declared_risk = plans.largest_declared;
  if (plans.risk.raw > 0) {
    t.planned_risk = plans.risk;
    if (t.exit_step) t.r_bp = round_div(static_cast<i128>(t.net.raw) * kBp, plans.risk.raw);
  }
  t.plan_respected = respected(t, plans.first);
  if (plans.first != nullptr && t.series == 0) t.stop = plans.first->invalidation;  // a stop order overrides it (annotate_stop)
  return t;
}

struct Walker {
  const RunState& st;
  std::vector<TradeRow> out;
  std::optional<OpenTrade> cur;
  std::int64_t pos = 0;

  void close(std::uint32_t step, ExitReason reason) {
    if (!cur) return;
    OpenTrade& t = *cur;
    t.row.exit_step = step;
    t.row.reason = reason;
    out.push_back(finalize(st, t));
    cur.reset();
  }

  // Expiry settles whatever is left of an option position at its settlement value.
  void on_settlement(const Event& e) {
    if (!cur) return;
    const std::int64_t q = abs64(e.qty.raw);
    if (q > 0) {
      cur->exit_notional += static_cast<i128>(q) * round_div(static_cast<i128>(abs64(e.amount.raw)) * Qty::kScale, q);
      cur->exit_qty += q;
    }
    cur->row.pnl = Money::from_raw(cur->row.pnl.raw + e.pnl.raw);
    pos = 0;
    close(e.step, ExitReason::Expiry);
  }

  // Funding is paid or received by the trade that holds the position at the funding step.
  void on_funding(const Event& e) {
    if (cur) cur->row.funding = Money::from_raw(cur->row.funding.raw + e.amount.raw);
  }

  void on_liquidation(const Event& e) {
    if (!cur) return;
    cur->row.pnl = Money::from_raw(cur->row.pnl.raw + e.pnl.raw);
    cur->exit_notional += static_cast<i128>(e.qty.raw) * e.price.raw;
    cur->exit_qty += e.qty.raw;
    pos = 0;
    close(e.step, ExitReason::Liquidation);
  }

  void on_fill(const Event& e) {
    const std::int64_t q = sign_of(e.side) * e.qty.raw;
    const std::int64_t next = pos + q;
    if (!cur) {
      cur = open_trade(static_cast<std::uint32_t>(out.size() + 1), e, abs64(q));
    } else if ((q > 0) == (pos > 0)) {
      cur->entry_notional += static_cast<i128>(abs64(q)) * e.price.raw;
      cur->entry_qty += abs64(q);
      add_entry_order(*cur, e.order_id);
      cur->row.qty = Qty::from_raw(std::max(cur->row.qty.raw, abs64(next)));
      cur->row.entry_avg = Price::from_raw(round_div(cur->entry_notional, cur->entry_qty));
    } else {
      const std::int64_t closing = std::min(abs64(q), abs64(pos));
      cur->exit_notional += static_cast<i128>(closing) * e.price.raw;
      cur->exit_qty += closing;
      cur->row.pnl = Money::from_raw(cur->row.pnl.raw + e.pnl.raw);
    }
    const bool flipped_or_flat = pos != 0 && (next == 0 || (next > 0) != (pos > 0));
    // A flip closes one trade and opens another with the same fill: its fee is split by quantity.
    const std::int64_t opening = flipped_or_flat ? abs64(next) : 0;
    const std::int64_t opening_fee = round_div(static_cast<i128>(e.amount.raw) * opening, abs64(q));
    cur->row.fees = Money::from_raw(cur->row.fees.raw + e.amount.raw - opening_fee);
    if (flipped_or_flat) {
      close(e.step, exit_reason(st, cur->row, e));
      if (next != 0) {
        cur = open_trade(static_cast<std::uint32_t>(out.size() + 1), e, abs64(next));
        cur->row.fees = Money::from_raw(opening_fee);
      }
    }
    pos = next;
  }
};

// ---- facts --------------------------------------------------------------------------------

bool is_stop(const Order& o) { return o.type == OrderType::StopMarket || o.type == OrderType::TrailingStop; }

// An entry is protected if a reduce-only stop on the other side was placed around it
// (bracket children are created at the fill step).
bool protected_entry(const RunState& st, const Event& fill) {
  return std::any_of(st.orders.begin(), st.orders.end(), [&fill](const Order& o) {
    return o.series == 0 && is_stop(o) && o.reduce_only && o.side != fill.side && o.placed_step + 1 >= fill.step &&
           o.placed_step <= fill.step + 2;
  });
}

struct Facts {
  std::uint32_t entries = 0;
  std::uint32_t unprotected = 0;
  std::uint32_t stop_warnings = 0;
  std::uint32_t planned = 0;
  std::uint32_t planned_with_invalidation = 0;
  std::vector<const Event*> liquidations;
  std::vector<const Event*> margin_calls;  // forced buy-backs of written options: the options' liquidation
  std::vector<const Event*> gap_stops;
  std::uint32_t ambiguous = 0;
  std::uint32_t capacity = 0;
  // First trade that actually happened with a planned risk above 2% of initial capital. Judged per
  // trade, from the plan matched to its entry: a plan whose order was rejected is not a trade.
  std::optional<std::pair<std::uint32_t, Money>> oversized;  // entry step, planned risk
  bool all_planned = true;  // every trade had a (non-skipped) plan
  bool wrote_options = false;  // an option was written uncovered (risk not limited by a premium or a long; v13)
  // v11: the first perpetual trade whose liquidation price was closer to its entry than twice its stop
  // distance at the highest leverage it carried (a gap of one more stop distance would liquidate it).
  std::optional<TradeRow> tight_stop;
};

bool grows_exposure(std::int64_t pos, std::int64_t q) { return pos == 0 || (q > 0) == (pos > 0) || abs64(q) > abs64(pos); }

void note_fill(const RunState& st, const Event& e, bool grows, Facts& f) {
  if (grows) {
    f.entries += 1;
    f.unprotected += protected_entry(st, e) ? 0 : 1;
  }
  const bool stop_exit = !grows && e.requested.raw != 0 && e.pnl.raw < 0;
  if (stop_exit && (e.flags & kFlagGapFill) != 0) f.gap_stops.push_back(&e);
  f.ambiguous += (e.flags & kFlagAmbiguousIntrabar) != 0 ? 1 : 0;
  f.capacity += (e.flags & kFlagCapacityFromBarVolume) != 0 ? 1 : 0;
}

// Open option and perpetual positions while the event log is walked (signed raw quantities).
struct OpenBook {
  std::vector<std::pair<std::uint32_t, std::int64_t>> options;  // series -> signed qty
  std::int64_t perp = 0;
};

const OptionSeries* series_of(const Scenario& sc, std::uint32_t id) {
  const auto it = std::find_if(sc.options.series.begin(), sc.options.series.end(), [id](const OptionSeries& s) { return s.id == id; });
  return it == sc.options.series.end() ? nullptr : &*it;
}

// Underlying units held long (+) or short (-) in options of one kind and expiry.
i128 option_units(const Scenario& sc, const OpenBook& b, OptionKind kind, std::uint32_t expiry, bool longs) {
  i128 units = 0;
  for (const auto& [id, q] : b.options) {
    const OptionSeries* s = series_of(sc, id);
    if (s == nullptr || s->kind != kind || s->expiry_step != expiry || (q > 0) != longs || q == 0) continue;
    units += static_cast<i128>(q < 0 ? -q : q) * sc.options.multiplier.raw;
  }
  return units;
}

// Scoring v13: a written option is uncovered when its kind and expiry hold more short units than long
// ones; a short call is also covered by long perpetual units (a covered call). Spreads, iron condors
// and covered calls are defined-risk structures, not naked writing.
bool has_uncovered_short(const Scenario& sc, const OpenBook& b) {
  const i128 perp_long = b.perp > 0 ? static_cast<i128>(b.perp) * sc.contract.multiplier.raw : 0;
  for (const auto& [id, q] : b.options) {
    const OptionSeries* s = series_of(sc, id);
    if (q >= 0 || s == nullptr) continue;
    const i128 cover = option_units(sc, b, s->kind, s->expiry_step, true) + (s->kind == OptionKind::Call ? perp_long : 0);
    if (option_units(sc, b, s->kind, s->expiry_step, false) > cover) return true;
  }
  return false;
}

void apply_fill(OpenBook& b, const Event& e) {
  const std::int64_t q = sign_of(e.side) * e.qty.raw;
  if (e.series == 0) {
    b.perp += q;
    return;
  }
  auto it = std::find_if(b.options.begin(), b.options.end(), [&e](const auto& p) { return p.first == e.series; });
  if (it == b.options.end()) it = b.options.insert(b.options.end(), {e.series, 0});
  it->second += q;
}

// Opening an option position is an entry too. A bought option is protected by construction (the
// premium is the maximum loss); a written one has no stop, but its risk is checked through margin
// calls, so it is not counted as an unprotected entry either. Cover is judged after all fills of a
// step (the legs of a combo fill at the same open), at every step with a fill: closing the long
// that covered a short leaves it naked.
std::int64_t held_option(const OpenBook& b, std::uint32_t series) {
  const auto it = std::find_if(b.options.begin(), b.options.end(), [series](const auto& p) { return p.first == series; });
  return it == b.options.end() ? 0 : it->second;
}

// Applies one event to the open book (and counts option entries); true when it was a fill.
bool apply_event(OpenBook& b, const Event& e, Facts& f) {
  if (e.type == EventType::Liquidation) b.perp = 0;
  if (e.type == EventType::OptionSettlement) {
    for (auto& [id, q] : b.options) q = id == e.series ? 0 : q;
  }
  if (e.type != EventType::Fill) return false;
  if (option_fill(e) && grows_exposure(held_option(b, e.series), sign_of(e.side) * e.qty.raw)) f.entries += 1;
  apply_fill(b, e);
  return true;
}

void count_option_entries(const Scenario& sc, const RunState& st, Facts& f) {
  OpenBook book;
  bool filled_this_step = false;
  for (std::size_t i = 0; i < st.events.size(); ++i) {
    filled_this_step = apply_event(book, st.events[i], f) || filled_this_step;
    const bool step_ends = i + 1 == st.events.size() || st.events[i + 1].step != st.events[i].step;
    if (step_ends && filled_this_step) {
      f.wrote_options = f.wrote_options || has_uncovered_short(sc, book);
      filled_this_step = false;
    }
  }
}

void count_fills(const Scenario& sc, const RunState& st, Facts& f) {
  count_option_entries(sc, st, f);
  std::int64_t pos = 0;
  for (const Event& e : st.events) {
    if (e.type == EventType::Liquidation) {
      f.liquidations.push_back(&e);
      pos = 0;
    }
    if (option_fill(e) && e.order_id == 0) f.margin_calls.push_back(&e);  // a forced buy-back has no order
    if (!perp_fill(e)) continue;
    const std::int64_t q = sign_of(e.side) * e.qty.raw;
    note_fill(st, e, grows_exposure(pos, q), f);
    pos += q;
  }
}

// The risk declared for each trade's entries (planned, or sent without a plan, which still carries
// the ticket's estimate), from the plan matched to each entry order (order_plan): the plan of an
// order that was rejected and never filled is not a trade.
// Leverage against the stop distance (scoring v11): the liquidation price, at the highest leverage the
// trade carried, lies closer to the entry than twice the stop's distance from it.
bool leverage_tight_to_stop(const TradeRow& t) {
  if (t.series != 0 || t.stop.raw <= 0 || t.liq_price.raw <= 0) return false;
  const i128 stop_dist = abs64(t.entry_avg.raw - t.stop.raw);
  const i128 liq_dist = abs64(t.entry_avg.raw - t.liq_price.raw);
  return liq_dist < 2 * stop_dist;
}

void trade_facts(const Scenario& sc, const RunState& st, Facts& f) {
  const std::int64_t limit = run_initial_balance(sc, st).raw / 50;  // 2% of the run's initial capital
  for (const TradeRow& t : build_trades(sc, st)) {
    if (!f.oversized && t.declared_risk.raw > limit) f.oversized = std::make_pair(t.entry_step, t.declared_risk);
    f.all_planned = f.all_planned && t.planned_risk.raw > 0;
    if (!f.tight_stop && leverage_tight_to_stop(t)) f.tight_stop = t;
  }
}

Facts collect(const Scenario& sc, const RunState& st) {
  Facts f;
  count_fills(sc, st, f);
  for (const CommandRecord& c : st.command_log) f.stop_warnings += c.result.warning == Reason::StopBeyondLiquidation ? 1 : 0;
  for (const PlanRecord& p : st.plans) {
    f.planned += p.skipped ? 0 : 1;
    f.planned_with_invalidation += !p.skipped && p.invalidation.raw > 0 ? 1 : 0;
  }
  trade_facts(sc, st, f);
  return f;
}

// ---- dimensions ---------------------------------------------------------------------------

// Max drawdown above the mission's drawdown budget (a share of the run's initial capital).
bool over_drawdown_budget(const Scenario& sc, const RunState& st) {
  return static_cast<i128>(st.metrics.max_drawdown.raw) * kBp > static_cast<i128>(run_initial_balance(sc, st).raw) * sc.mission.drawdown_budget_bp;
}

// The closest liquidation distance seen at a close fell below the mission's warning distance.
bool thin_liq_buffer(const Scenario& sc, const RunState& st) {
  const std::int64_t d = st.metrics.min_liq_distance_bp;
  return d >= 0 && d < static_cast<std::int64_t>(sc.mission.liq_warning_bp);
}

std::int32_t risk_score(const Scenario& sc, const RunState& st, const Facts& f) {
  if (f.entries == 0 && f.liquidations.empty()) return 100;
  std::int32_t s = 100;
  if (!f.liquidations.empty() || !f.margin_calls.empty()) s -= 40;  // scoring v3: a margin call counts like a liquidation
  s -= static_cast<std::int32_t>(std::min<std::uint32_t>(40, f.unprotected * 20));
  if (f.oversized) s -= 20;
  if (f.stop_warnings > 0) s -= 15;
  if (over_drawdown_budget(sc, st)) s -= 15;  // v11: the mission's budget (20% unless it declares one)
  if (f.tight_stop) s -= 10;                  // v11: leverage too high for the stop distance
  if (thin_liq_buffer(sc, st)) s -= 10;       // v11: liquidation came closer than the mission's warning distance
  return std::max(0, s);
}

std::optional<std::int32_t> plan_score(const Facts& f) {
  if (f.entries == 0) return std::nullopt;
  const i128 e = f.entries;
  const i128 p = f.planned;
  const i128 covered = std::min(p, e);
  if (p == 0) return 0;
  // 70 * min(p, e) / e + 30 * with_invalidation / p
  const i128 num = 70 * covered * p + 30 * static_cast<i128>(f.planned_with_invalidation) * e;
  return static_cast<std::int32_t>(round_div(num, e * p));
}

void calibration(const Scenario& sc, const RunState& st, ScoreReport& r) {
  i128 sum_sq = 0;
  std::uint32_t n = 0;
  for (const CalibrationAnswer& a : st.answers) {
    const std::uint32_t resolve = a.step + a.horizon;
    if (a.skipped || resolve > st.step || resolve >= sc.bars.size()) continue;
    const std::int64_t outcome = sc.bars[resolve].close > a.threshold ? kBp : 0;
    const std::int64_t d = static_cast<std::int64_t>(a.p_bp) - outcome;
    sum_sq += static_cast<i128>(d) * d;
    n += 1;
  }
  r.resolved_answers = n;
  if (n < 3) return;
  r.brier_bp = round_div(sum_sq, static_cast<i128>(n) * kBp);
  // Scoring v2: 100 * (1 - 2 * Brier), floored at 0. Always answering 50% (Brier 0.25, no
  // information) is neutral at 50 instead of a free 75; confident wrong answers cost more.
  // = (n * 1e10 - 200 * sum_sq) / (n * 1e8)
  const std::int64_t score = round_div(static_cast<i128>(n) * kBp * kBp * 100 - 200 * sum_sq, static_cast<i128>(n) * kBp * kBp);
  r.calibration.score = static_cast<std::int32_t>(std::max<std::int64_t>(0, score));
}

// Understanding (task 4.5, scoring v11): points of the right answers over the points of every question
// in the mission's rubric (skipped and unanswered questions earn 0). Applicable only with a rubric a
// human has reviewed and at least one answer or skip recorded: a run whose player never saw the
// questions keeps the dimension not applicable, as before.
void understanding(const Rubric* rubric, const RunState& st, ScoreReport& r) {
  if (rubric == nullptr) return;
  r.lessons_reviewed = rubric->reviewed;
  for (std::uint32_t i = 0; i < rubric->questions.size(); ++i) {
    const LessonQuestion& q = rubric->questions[i];
    LessonResult res;
    res.question = i;
    const auto it = std::find_if(st.lesson_answers.begin(), st.lesson_answers.end(), [i](const LessonAnswer& a) { return a.question == i; });
    if (it != st.lesson_answers.end()) {
      res.answered = !it->skipped;
      res.skipped = it->skipped;
      res.correct = lesson_correct(q, *it);
    }
    res.points = res.correct ? q.points : 0;
    r.lesson_points += res.points;
    r.lesson_points_total += q.points;
    r.lessons.push_back(res);
  }
  if (!rubric->reviewed || st.lesson_answers.empty() || r.lesson_points_total == 0) return;
  r.understanding.score = static_cast<std::int32_t>(round_div(static_cast<i128>(r.lesson_points) * 100, r.lesson_points_total));
}

void combine(ScoreReport& r) {
  std::int64_t num = 0;
  std::int64_t den = 0;
  for (const Dimension* d : {&r.risk, &r.plan, &r.understanding, &r.calibration}) {
    if (!d->score) continue;
    num += static_cast<std::int64_t>(d->weight) * *d->score;
    den += d->weight;
  }
  if (den > 0) r.total = static_cast<std::int32_t>(round_div(num, den));
}

// ---- findings -----------------------------------------------------------------------------

Finding mk(FindingCode c, FindingTone t, std::uint32_t step = 0, std::uint32_t count = 0, Price price = {},
           Price requested = {}, Money amount = {}) {
  Finding f;
  f.code = c;
  f.tone = t;
  f.step = step;
  f.count = count;
  f.price = price;
  f.requested = requested;
  f.amount = amount;
  return f;
}

void add(ScoreReport& r, FindingCode c, FindingTone t) { r.findings.push_back(mk(c, t)); }

// The liquidated trade's safe leverage (0 when its stop was in front, or there was no stop).
std::uint32_t safe_leverage_at(const ScoreReport& r, std::uint32_t step) {
  for (const TradeRow& t : r.trades) {
    if (t.reason == ExitReason::Liquidation && t.exit_step == step) return static_cast<std::uint32_t>(t.safe_leverage);
  }
  return 0;
}

// Stop beyond liquidation: warned when the stop was placed, or a bracket/planned stop found per trade.
// The finding names the first such trade: entry bar, stop, liquidation price, safe leverage.
void stop_beyond_finding(const Facts& f, ScoreReport& r) {
  const auto it = std::find_if(r.trades.begin(), r.trades.end(), [](const TradeRow& t) { return t.stop_beyond_liq; });
  if (it == r.trades.end()) {
    if (f.stop_warnings > 0) add(r, FindingCode::StopBeyondLiq, FindingTone::Bad);
    return;
  }
  r.findings.push_back(mk(FindingCode::StopBeyondLiq, FindingTone::Bad, it->entry_step, static_cast<std::uint32_t>(it->safe_leverage),
                          it->stop, it->liq_price));
}

void risk_findings(const RunState& st, const Facts& f, ScoreReport& r) {
  if (st.status == RunStatus::Bankrupt) add(r, FindingCode::Bankrupt, FindingTone::Bad);
  for (const Event* l : f.liquidations) {
    r.findings.push_back(mk(FindingCode::Liquidated, FindingTone::Bad, l->step, safe_leverage_at(r, l->step), l->price, Price{}, l->amount));
  }
  for (const Event* m : f.margin_calls) {
    r.findings.push_back(mk(FindingCode::MarginCalled, FindingTone::Bad, m->step, 0, m->price, Price{}, Money::from_raw(-m->pnl.raw)));
  }
  stop_beyond_finding(f, r);
  if (f.unprotected > 0) r.findings.push_back(mk(FindingCode::NoStop, FindingTone::Warn, 0, f.unprotected));
  if (f.oversized) {
    r.findings.push_back(mk(FindingCode::Oversized, FindingTone::Warn, f.oversized->first, 0, Price{}, Price{}, f.oversized->second));
  }
}

// Scoring v11 risk inputs. A stop already beyond liquidation is named by its own finding.
void budget_findings(const Scenario& sc, const RunState& st, const Facts& f, ScoreReport& r) {
  if (f.tight_stop && !f.tight_stop->stop_beyond_liq) {
    const TradeRow& t = *f.tight_stop;
    r.findings.push_back(mk(FindingCode::LeverageVsStop, FindingTone::Warn, t.entry_step, static_cast<std::uint32_t>(t.leverage), t.stop,
                            t.liq_price));
  }
  if (thin_liq_buffer(sc, st)) {
    r.findings.push_back(mk(FindingCode::LiqBuffer, FindingTone::Warn, 0, static_cast<std::uint32_t>(st.metrics.min_liq_distance_bp)));
  }
  if (over_drawdown_budget(sc, st)) {
    r.findings.push_back(
        mk(FindingCode::DrawdownBudget, FindingTone::Warn, 0, sc.mission.drawdown_budget_bp, Price{}, Price{}, st.metrics.max_drawdown));
  }
}

void outcome_findings(const Facts& f, ScoreReport& r) {
  const bool planned = f.planned > 0;
  if (f.entries == 0) add(r, FindingCode::Waited, FindingTone::Good);
  // Credit good process only when nothing in the process went wrong (scoring v3: also no oversized
  // entry, no option margin call, no stop beyond liquidation, and a plan for every trade; v5: no
  // written option either, whose risk is not limited by a stop or a premium).
  const bool clean = planned && f.all_planned && f.unprotected == 0 && f.liquidations.empty() && f.margin_calls.empty() &&
                     !f.oversized && f.stop_warnings == 0 && !f.wrote_options &&
                     std::none_of(r.trades.begin(), r.trades.end(), [](const TradeRow& t) { return t.stop_beyond_liq; });
  if (r.net_pnl.raw < 0 && clean) add(r, FindingCode::GoodProcessLoss, FindingTone::Good);
  if (r.net_pnl.raw > 0 && clean) add(r, FindingCode::GoodProcessWin, FindingTone::Good);
  // Scoring v8: a trade without its own plan also makes a profit unproven (before, a run where only some
  // trades had a plan got neither credit nor this warning, and "What happened" was empty).
  if (r.net_pnl.raw > 0 && (!f.all_planned || f.unprotected > 0)) add(r, FindingCode::LuckyWin, FindingTone::Warn);
  if (f.entries > 0 && !planned) add(r, FindingCode::NoPlan, FindingTone::Warn);
  // Says why a written option gets no good-process credit (a margin call already says more).
  if (f.wrote_options && f.margin_calls.empty()) add(r, FindingCode::WroteOptions, FindingTone::Warn);
}

void execution_findings(const RunState& st, const Facts& f, ScoreReport& r) {
  for (const Event* g : f.gap_stops) {
    r.findings.push_back(mk(FindingCode::GapSlippage, FindingTone::Info, g->step, 0, g->price, g->requested));
  }
  if (st.funding_net.raw < 0 && -st.funding_net.raw > st.fees_paid.raw) {
    r.findings.push_back(mk(FindingCode::FundingCost, FindingTone::Warn, 0, 0, Price{}, Price{}, Money::from_raw(-st.funding_net.raw)));
  }
  if (f.ambiguous > 0) r.findings.push_back(mk(FindingCode::Ambiguous, FindingTone::Info, 0, f.ambiguous));
  if (f.capacity > 0) r.findings.push_back(mk(FindingCode::Capacity, FindingTone::Info, 0, f.capacity));
}

// Theta or vega explaining at least half of an option loss points to the Options lesson (IV and Greeks).
// Only bought options lose to theta. A vega loss is a falling IV on bought options (iv_crush) or a
// rising IV on written ones (iv_rise), whichever side lost more of it.
void greek_findings(ScoreReport& r) {
  if (!r.greeks || r.greeks->total.raw >= 0) return;
  const GreekAttribution& g = *r.greeks;
  const std::int64_t loss = -g.total.raw;
  if (g.theta.raw < 0 && -2 * g.theta.raw >= loss) {
    r.findings.push_back(mk(FindingCode::ThetaDecay, FindingTone::Warn, 0, 0, Price{}, Price{}, Money::from_raw(-g.theta.raw)));
  }
  if (g.vega.raw < 0 && -2 * g.vega.raw >= loss) {
    const bool written = g.vega_written.raw < g.vega.raw - g.vega_written.raw;
    const FindingCode code = written ? FindingCode::IvRise : FindingCode::IvCrush;
    r.findings.push_back(mk(code, FindingTone::Warn, 0, 0, Price{}, Price{}, Money::from_raw(-g.vega.raw)));
  }
}

bool has(const ScoreReport& r, FindingCode c) {
  return std::any_of(r.findings.begin(), r.findings.end(), [c](const Finding& f) { return f.code == c; });
}

LessonCode pick_lesson(const ScoreReport& r) {
  if (has(r, FindingCode::Liquidated) || has(r, FindingCode::StopBeyondLiq)) return LessonCode::Futures2;
  // A margin call on written options (even one that ends in bankruptcy) is a sizing lesson.
  if (has(r, FindingCode::NoStop) || has(r, FindingCode::Oversized) || has(r, FindingCode::MarginCalled)) return LessonCode::Risk;
  if (has(r, FindingCode::Bankrupt)) return LessonCode::Futures2;
  if (has(r, FindingCode::LeverageVsStop) || has(r, FindingCode::LiqBuffer)) return LessonCode::Futures2;
  if (has(r, FindingCode::DrawdownBudget)) return LessonCode::Risk;
  if (has(r, FindingCode::ThetaDecay) || has(r, FindingCode::IvCrush) || has(r, FindingCode::IvRise)) return LessonCode::Options;
  if (has(r, FindingCode::FundingCost)) return LessonCode::Futures3;
  if (has(r, FindingCode::GapSlippage)) return LessonCode::Orders;
  if (has(r, FindingCode::NoPlan)) return LessonCode::Structure;
  return LessonCode::Evaluation;
}

// ---- advisor ------------------------------------------------------------------------------

// Step of an order's last status change (fill, cancel, expiry), or "still working".
std::uint32_t order_end_step(const RunState& st, const Order& o) {
  if (o.active()) return UINT32_MAX;
  std::uint32_t end = o.placed_step;
  for (const Event& e : st.events) {
    if (e.order_id == o.id && (e.type == EventType::OrderUpdate || e.type == EventType::Fill)) end = e.step;
  }
  return end;
}

// A reduce-only stop on the exit side was working at some bar while the trade was open.
bool stop_worked_during(const RunState& st, const TradeRow& t) {
  const OrderSide exit = t.is_long ? OrderSide::Sell : OrderSide::Buy;
  const std::uint32_t last = t.exit_step.value_or(st.step);
  return std::any_of(st.orders.begin(), st.orders.end(), [&](const Order& o) {
    return o.series == 0 && is_stop(o) && o.reduce_only && o.side == exit && o.placed_step <= last &&
           order_end_step(st, o) >= t.entry_step;
  });
}

AdviceOutcome outcome_of(const Scenario& sc, const Advice& a) {
  if (a.stance == AdviceStance::Wait) return AdviceOutcome::Wait;
  const std::size_t end = static_cast<std::size_t>(a.step) + kAdviceHorizon;
  if (end >= sc.bars.size()) return AdviceOutcome::Open;
  const std::int64_t move = sc.bars[end].close.raw - sc.bars[a.step + 1].open.raw;
  const bool right = a.stance == AdviceStance::Long ? move > 0 : move < 0;
  return right ? AdviceOutcome::Right : AdviceOutcome::Wrong;
}

void advice_summary(const Scenario& sc, const RunState& st, ScoreReport& r) {
  r.advice = advice_rows(sc, st);
  std::int64_t right = 0;
  std::int64_t judged = 0;
  for (const AdviceRow& a : r.advice) {
    right += a.outcome == AdviceOutcome::Right ? 1 : 0;
    judged += a.outcome == AdviceOutcome::Right || a.outcome == AdviceOutcome::Wrong ? 1 : 0;
  }
  if (judged > 0) r.advice_hit_bp = round_div(static_cast<i128>(right) * kBp, judged);
}

// Loss beyond the balance that the simulated fund covered when the wallet was floored at zero (a
// margin-call buy-back or a settlement larger than the balance): the trades' nets minus the result.
// A perpetual liquidation's rows already stop at the margin lost, so only option losses show here.
Money written_off(const RunState& st, const ScoreReport& r) {
  if (st.insurance_absorbed.raw <= 0) return Money{};
  std::int64_t nets = 0;
  for (const TradeRow& t : r.trades) nets += t.net.raw;
  return Money::from_raw(std::max<std::int64_t>(0, r.net_pnl.raw - nets));
}

}  // namespace

std::uint32_t advisor_points(const RunState& st, const std::vector<TradeRow>& trades) {
  std::uint32_t points = st.status == RunStatus::Finished ? 2 : 0;
  for (const TradeRow& t : trades) {
    if (t.series != 0 || !t.exit_step || t.planned_risk.raw <= 0 || !stop_worked_during(st, t)) continue;
    points += t.net.raw > 0 ? 2 : 1;
  }
  return std::min(points, kAdvisorPointsCap);
}

std::vector<AdviceRow> advice_rows(const Scenario& sc, const RunState& st) {
  std::vector<AdviceRow> rows;
  rows.reserve(st.advice.size());
  for (const Advice& a : st.advice) rows.push_back({a.step, a.stance, a.confidence_bp, outcome_of(sc, a)});
  return rows;
}

// Option rows carry their contract (expired series are no longer in the live chain).
void describe_series(const Scenario& sc, TradeRow& t) {
  if (t.series == 0) return;
  for (const OptionSeries& s : sc.options.series) {
    if (s.id != t.series) continue;
    t.is_call = s.kind == OptionKind::Call;
    t.strike = s.strike;
    t.expiry_step = s.expiry_step;
  }
}

namespace {
void route(const Event& e, Walker& perp, const auto& option_walker) {
  if (e.type == EventType::Liquidation) perp.on_liquidation(e);
  else if (e.type == EventType::Funding) perp.on_funding(e);
  else if (perp_fill(e)) perp.on_fill(e);
  else if (option_fill(e)) option_walker(e.series).on_fill(e);
  else if (e.type == EventType::OptionSettlement) option_walker(e.series).on_settlement(e);
}
}  // namespace

std::vector<TradeRow> build_trades(const Scenario& sc, const RunState& st) {
  Walker perp{st, {}, std::nullopt};
  std::vector<std::pair<std::uint32_t, Walker>> options;  // one walker per option series
  const auto option_walker = [&](std::uint32_t series) -> Walker& {
    for (auto& [id, w] : options) {
      if (id == series) return w;
    }
    return options.emplace_back(series, Walker{st, {}, std::nullopt}).second;
  };
  for (const Event& e : st.events) route(e, perp, option_walker);
  std::vector<TradeRow> rows;
  const auto take = [&](Walker& w) {
    if (w.cur) w.out.push_back(finalize(st, *w.cur));
    rows.insert(rows.end(), w.out.begin(), w.out.end());
  };
  take(perp);
  for (auto& [id, w] : options) take(w);
  std::stable_sort(rows.begin(), rows.end(), [](const TradeRow& a, const TradeRow& b) { return a.entry_step < b.entry_step; });
  for (std::size_t i = 0; i < rows.size(); ++i) {
    rows[i].n = static_cast<std::uint32_t>(i + 1);
    describe_series(sc, rows[i]);
    const Price invalidation = rows[i].stop;  // the plan's (finalize); annotate_stop puts the stop order's
    annotate_stop(sc, st, rows[i]);
    credit_stop_exit(rows[i], invalidation);
    mark_open_trade(sc, st, rows[i]);
  }
  return rows;
}

ScoreReport score_run(const Scenario& sc, const RunState& st) { return score_run(sc, st, rubric_for(sc.id)); }

ScoreReport score_run(const Scenario& sc, const RunState& st, const Rubric* rubric) {
  ScoreReport r;
  const Facts f = collect(sc, st);
  r.entries = f.entries;
  r.net_pnl = Money::from_raw(account_view(sc, st).equity.raw - run_initial_balance(sc, st).raw);
  r.risk.score = risk_score(sc, st, f);
  r.plan.score = plan_score(f);
  calibration(sc, st, r);
  understanding(rubric, st, r);
  combine(r);
  r.trades = build_trades(sc, st);
  r.spread_cost = spread_slippage_cost(sc, st);
  r.written_off = written_off(st, r);
  r.greeks = greek_attribution(sc, st);
  risk_findings(st, f, r);
  budget_findings(sc, st, f, r);
  outcome_findings(f, r);
  execution_findings(st, f, r);
  greek_findings(r);
  r.lesson = pick_lesson(r);
  r.advisor_points_earned = advisor_points(st, r.trades);
  advice_summary(sc, st, r);
  return r;
}

}  // namespace tradelab
