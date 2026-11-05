// Canonical state and event-stream digests (spec: simulation-core "Checkpoints and digest").
// Field order here is part of the execution policy: append only, never reorder.

#include "tradelab/engine.hpp"
#include "tradelab/serialize.hpp"

namespace tradelab {
namespace {

void write_order(Writer& w, const Order& o) {
  w.u32(o.id);
  w.u64(o.seq);
  w.u8(static_cast<std::uint8_t>(o.type));
  w.u8(static_cast<std::uint8_t>(o.side));
  w.fixed(o.qty);
  w.fixed(o.filled);
  w.fixed(o.price);
  w.boolean(o.reduce_only);
  w.u32(o.placed_step);
  w.u8(static_cast<std::uint8_t>(o.status));
  w.fixed(o.bracket_stop);
  w.fixed(o.bracket_tp);
  w.u32(o.parent_id);
  w.u32(o.oco_group);
  w.fixed(o.trail_distance);
  w.u32(o.series);
  w.fixed(o.trail_rate);  // policy v16
}

void write_event(Writer& w, const Event& e) {
  w.u64(e.seq);
  w.u32(e.step);
  w.u8(static_cast<std::uint8_t>(e.type));
  w.u32(e.order_id);
  w.u8(static_cast<std::uint8_t>(e.status));
  w.u8(static_cast<std::uint8_t>(e.reason));
  w.u8(e.flags);
  w.u8(static_cast<std::uint8_t>(e.side));
  w.fixed(e.qty);
  w.fixed(e.price);
  w.fixed(e.requested);
  w.fixed(e.amount);
  w.fixed(e.pnl);
  w.u32(e.series);
}

void write_metrics(Writer& w, const Metrics& m) {
  w.fixed(m.peak_equity);
  w.fixed(m.max_drawdown);
  w.i64(m.peak_leverage_bp);
  w.i64(m.min_liq_distance_bp);
  w.u32(m.liquidations);
  w.u32(m.fills);
  w.u32(m.ambiguous_fills);
  w.fixed(m.turnover);
}

// Policy v11: start options and advice (a default run writes 0, 0, false, 0, 0); policy v12
// appends the campaign options (starting balance and leverage cap, 0 and 0 by default).
void write_advisor(Writer& w, const RunState& st) {
  w.u32(st.options.advice_allowance);
  w.u8(st.options.news_lead);
  w.boolean(st.options.calendar);
  w.u32(st.advice_left);
  w.u32(static_cast<std::uint32_t>(st.advice.size()));
  for (const Advice& a : st.advice) {
    w.u32(a.step);
    w.u8(static_cast<std::uint8_t>(a.stance));
    w.u32(a.confidence_bp);
    w.fixed(a.stop);
    w.fixed(a.size);
    w.u32(static_cast<std::uint32_t>(a.reasons.size()));
    for (const AdviceReason& r : a.reasons) {
      w.u8(static_cast<std::uint8_t>(r.code));
      w.u8(static_cast<std::uint8_t>(r.vote));
      w.u32(static_cast<std::uint32_t>(r.ref));
    }
  }
  w.fixed(st.options.starting_balance);
  w.u32(static_cast<std::uint32_t>(st.options.leverage_cap));
}

}  // namespace

Digest state_digest(const RunState& st) {
  Writer w;
  w.u32(kExecutionPolicyVersion);
  w.u64(st.seed);
  w.u32(st.step);
  w.u8(static_cast<std::uint8_t>(st.status));
  w.u32(static_cast<std::uint32_t>(st.leverage));
  w.fixed(st.wallet);
  w.fixed(st.position.qty);
  w.fixed(st.position.entry);
  w.fixed(st.position.margin);
  w.u32(static_cast<std::uint32_t>(st.orders.size()));
  for (const Order& o : st.orders) write_order(w, o);
  w.u32(st.next_order_id);
  w.u64(st.next_event_seq);
  w.u64(st.last_command_seq);
  w.fixed(st.realized_pnl);
  w.fixed(st.fees_paid);
  w.fixed(st.funding_net);
  w.fixed(st.insurance_absorbed);
  w.u32(static_cast<std::uint32_t>(st.option_positions.size()));
  for (const OptionPosition& p : st.option_positions) {
    w.u32(p.series);
    w.fixed(p.qty);
    w.fixed(p.avg_premium);
  }
  w.fixed(st.option_cash);
  w.fixed(st.option_realized);
  w.u32(static_cast<std::uint32_t>(st.plans.size()));
  for (const PlanRecord& p : st.plans) {
    w.u32(p.step);
    w.u8(static_cast<std::uint8_t>(p.side));
    w.boolean(p.skipped);
    w.boolean(p.option);
    w.boolean(p.has_lose_scenario);
    w.fixed(p.invalidation);
    w.fixed(p.target);
    w.u32(p.confidence_pct);
    w.fixed(p.planned_risk);
  }
  w.u32(static_cast<std::uint32_t>(st.answers.size()));
  for (const CalibrationAnswer& a : st.answers) {
    w.u32(a.step);
    w.fixed(a.threshold);
    w.u32(a.horizon);
    w.u32(a.p_bp);
    w.boolean(a.skipped);
  }
  write_metrics(w, st.metrics);
  w.u64(st.rng.state());
  w.u64(st.rng.inc());
  write_advisor(w, st);
  // Lesson answers (task 4.5) are appended only when present, so every run without them hashes as
  // before. Decision records are not hashed: advance derives them from the state at each step.
  if (!st.lesson_answers.empty()) {
    w.u32(static_cast<std::uint32_t>(st.lesson_answers.size()));
    for (const LessonAnswer& a : st.lesson_answers) {
      w.u32(a.step);
      w.u32(a.question);
      w.u32(a.choice);
      w.fixed(a.value);
      w.boolean(a.skipped);
    }
  }
  return w.digest();
}

Digest events_digest(const RunState& st) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(st.events.size()));
  for (const Event& e : st.events) write_event(w, e);
  return w.digest();
}

}  // namespace tradelab
