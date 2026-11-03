#include "tradelab/scoring.hpp"

#include <algorithm>

namespace tradelab {
namespace {

constexpr std::int64_t kBp = 10000;

std::int64_t abs64(std::int64_t v) { return v < 0 ? -v : v; }
std::int64_t sign_of(OrderSide s) { return s == OrderSide::Buy ? 1 : -1; }
bool perp_fill(const Event& e) { return e.type == EventType::Fill && e.series == 0; }

std::int64_t round_div(i128 num, i128 den) { return static_cast<std::int64_t>(div_round(num, den, Rounding::HalfEven)); }

// ---- trades ------------------------------------------------------------------------------

struct OpenTrade {
  TradeRow row;
  i128 entry_notional = 0;  // sum qty.raw * price.raw
  std::int64_t entry_qty = 0;
  i128 exit_notional = 0;
  std::int64_t exit_qty = 0;
};

OpenTrade open_trade(std::uint32_t n, const Event& e, std::int64_t qty_abs) {
  OpenTrade t;
  t.row.n = n;
  t.row.is_long = e.side == OrderSide::Buy;
  t.row.entry_step = e.step;
  t.row.qty = Qty::from_raw(qty_abs);
  t.row.entry_avg = e.price;
  t.entry_notional = static_cast<i128>(qty_abs) * e.price.raw;
  t.entry_qty = qty_abs;
  return t;
}

const PlanRecord* plan_for(const RunState& st, std::uint32_t entry_step, std::uint32_t after) {
  const PlanRecord* best = nullptr;
  for (const PlanRecord& p : st.plans) {
    if (!p.option && p.step <= entry_step && p.step >= after) best = &p;
  }
  return best;
}

ExitReason exit_reason(const TradeRow& t, const Event& fill) {
  if (fill.requested.raw == 0) return ExitReason::Manual;
  const bool favorable = t.is_long ? fill.requested > t.entry_avg : fill.requested < t.entry_avg;
  return favorable ? ExitReason::Target : ExitReason::Stop;
}

std::optional<bool> respected(const TradeRow& t, const PlanRecord* plan) {
  if (plan == nullptr || plan->invalidation.raw == 0 || t.exit_avg.raw == 0 || t.reason == ExitReason::Open) {
    return std::nullopt;
  }
  const std::int64_t tol = plan->invalidation.raw / 100;  // 1%
  return t.is_long ? t.exit_avg.raw >= plan->invalidation.raw - tol : t.exit_avg.raw <= plan->invalidation.raw + tol;
}

TradeRow finalize(const RunState& st, OpenTrade& o, std::uint32_t after) {
  TradeRow t = o.row;
  if (o.exit_qty > 0) t.exit_avg = Price::from_raw(round_div(o.exit_notional, o.exit_qty));
  t.net = Money::from_raw(t.pnl.raw - t.fees.raw);
  t.bars = t.exit_step.value_or(st.step) - t.entry_step;
  const PlanRecord* plan = plan_for(st, t.entry_step, after);
  if (plan != nullptr && plan->planned_risk.raw > 0) {
    t.planned_risk = plan->planned_risk;
    if (t.exit_step) t.r_bp = round_div(static_cast<i128>(t.net.raw) * kBp, plan->planned_risk.raw);
  }
  t.plan_respected = respected(t, plan);
  return t;
}

struct Walker {
  const RunState& st;
  std::vector<TradeRow> out;
  std::optional<OpenTrade> cur;
  std::int64_t pos = 0;
  std::uint32_t last_exit = 0;

  void close(std::uint32_t step, ExitReason reason) {
    if (!cur) return;
    OpenTrade& t = *cur;
    t.row.exit_step = step;
    t.row.reason = reason;
    out.push_back(finalize(st, t, last_exit));
    last_exit = step;
    cur.reset();
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
      cur->row.qty = Qty::from_raw(std::max(cur->row.qty.raw, abs64(next)));
      cur->row.entry_avg = Price::from_raw(round_div(cur->entry_notional, cur->entry_qty));
    } else {
      const std::int64_t closing = std::min(abs64(q), abs64(pos));
      cur->exit_notional += static_cast<i128>(closing) * e.price.raw;
      cur->exit_qty += closing;
      cur->row.pnl = Money::from_raw(cur->row.pnl.raw + e.pnl.raw);
    }
    cur->row.fees = Money::from_raw(cur->row.fees.raw + e.amount.raw);
    const bool flipped_or_flat = pos != 0 && (next == 0 || (next > 0) != (pos > 0));
    if (flipped_or_flat) {
      close(e.step, exit_reason(cur->row, e));
      if (next != 0) cur = open_trade(static_cast<std::uint32_t>(out.size() + 1), e, abs64(next));
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
  std::vector<const Event*> gap_stops;
  std::uint32_t ambiguous = 0;
  std::uint32_t capacity = 0;
  const PlanRecord* oversized = nullptr;
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

void count_fills(const RunState& st, Facts& f) {
  std::int64_t pos = 0;
  for (const Event& e : st.events) {
    if (e.type == EventType::Liquidation) {
      f.liquidations.push_back(&e);
      pos = 0;
    }
    if (!perp_fill(e)) continue;
    const std::int64_t q = sign_of(e.side) * e.qty.raw;
    note_fill(st, e, grows_exposure(pos, q), f);
    pos += q;
  }
}

Facts collect(const Scenario& sc, const RunState& st) {
  Facts f;
  count_fills(st, f);
  for (const CommandRecord& c : st.command_log) f.stop_warnings += c.result.warning == Reason::StopBeyondLiquidation ? 1 : 0;
  const std::int64_t limit = sc.mission.initial_balance.raw / 50;  // 2% of initial capital
  for (const PlanRecord& p : st.plans) {
    f.planned += p.skipped ? 0 : 1;
    f.planned_with_invalidation += !p.skipped && p.invalidation.raw > 0 ? 1 : 0;
    if (f.oversized == nullptr && p.planned_risk.raw > limit) f.oversized = &p;
  }
  return f;
}

// ---- dimensions ---------------------------------------------------------------------------

std::int32_t risk_score(const Scenario& sc, const RunState& st, const Facts& f) {
  if (f.entries == 0 && f.liquidations.empty()) return 100;
  std::int32_t s = 100;
  if (!f.liquidations.empty()) s -= 40;
  s -= static_cast<std::int32_t>(std::min<std::uint32_t>(40, f.unprotected * 20));
  if (f.oversized != nullptr) s -= 20;
  if (f.stop_warnings > 0) s -= 15;
  if (st.metrics.max_drawdown.raw * 5 > sc.mission.initial_balance.raw) s -= 15;  // > 20%
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
  // 100 * (1 - Brier) = (n * 1e10 - 100 * sum_sq) / (n * 1e8)
  r.calibration.score = static_cast<std::int32_t>(round_div(static_cast<i128>(n) * kBp * kBp * 100 - 100 * sum_sq,
                                                            static_cast<i128>(n) * kBp * kBp));
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

void risk_findings(const RunState& st, const Facts& f, ScoreReport& r) {
  if (st.status == RunStatus::Bankrupt) add(r, FindingCode::Bankrupt, FindingTone::Bad);
  for (const Event* l : f.liquidations) {
    r.findings.push_back(mk(FindingCode::Liquidated, FindingTone::Bad, l->step, 0, l->price, Price{}, l->amount));
  }
  if (f.stop_warnings > 0) add(r, FindingCode::StopBeyondLiq, FindingTone::Bad);
  if (f.unprotected > 0) r.findings.push_back(mk(FindingCode::NoStop, FindingTone::Warn, 0, f.unprotected));
  if (f.oversized != nullptr) {
    r.findings.push_back(mk(FindingCode::Oversized, FindingTone::Warn, f.oversized->step, 0, Price{}, Price{}, f.oversized->planned_risk));
  }
}

void outcome_findings(const Facts& f, ScoreReport& r) {
  const bool planned = f.planned > 0;
  if (f.entries == 0) add(r, FindingCode::Waited, FindingTone::Good);
  if (r.net_pnl.raw < 0 && planned && f.unprotected == 0 && f.liquidations.empty()) add(r, FindingCode::GoodProcessLoss, FindingTone::Good);
  if (r.net_pnl.raw > 0 && (!planned || f.unprotected > 0)) add(r, FindingCode::LuckyWin, FindingTone::Warn);
  if (f.entries > 0 && !planned) add(r, FindingCode::NoPlan, FindingTone::Warn);
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

bool has(const ScoreReport& r, FindingCode c) {
  return std::any_of(r.findings.begin(), r.findings.end(), [c](const Finding& f) { return f.code == c; });
}

LessonCode pick_lesson(const ScoreReport& r) {
  if (has(r, FindingCode::Liquidated) || has(r, FindingCode::StopBeyondLiq) || has(r, FindingCode::Bankrupt)) {
    return LessonCode::Futures2;
  }
  if (has(r, FindingCode::NoStop) || has(r, FindingCode::Oversized)) return LessonCode::Risk;
  if (has(r, FindingCode::FundingCost)) return LessonCode::Futures3;
  if (has(r, FindingCode::GapSlippage)) return LessonCode::Orders;
  if (has(r, FindingCode::NoPlan)) return LessonCode::Structure;
  return LessonCode::Evaluation;
}

}  // namespace

std::vector<TradeRow> build_trades(const Scenario& sc, const RunState& st) {
  (void)sc;
  Walker w{st, {}, std::nullopt};
  for (const Event& e : st.events) {
    if (e.type == EventType::Liquidation) w.on_liquidation(e);
    else if (perp_fill(e)) w.on_fill(e);
  }
  if (w.cur) w.out.push_back(finalize(st, *w.cur, w.last_exit));
  return w.out;
}

ScoreReport score_run(const Scenario& sc, const RunState& st) {
  ScoreReport r;
  const Facts f = collect(sc, st);
  r.entries = f.entries;
  r.net_pnl = Money::from_raw(account_view(sc, st).equity.raw - sc.mission.initial_balance.raw);
  r.risk.score = risk_score(sc, st, f);
  r.plan.score = plan_score(f);
  calibration(sc, st, r);
  combine(r);
  risk_findings(st, f, r);
  outcome_findings(f, r);
  execution_findings(st, f, r);
  r.lesson = pick_lesson(r);
  r.trades = build_trades(sc, st);
  return r;
}

}  // namespace tradelab
