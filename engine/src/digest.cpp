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
  write_metrics(w, st.metrics);
  w.u64(st.rng.state());
  w.u64(st.rng.inc());
  return w.digest();
}

Digest events_digest(const RunState& st) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(st.events.size()));
  for (const Event& e : st.events) write_event(w, e);
  return w.digest();
}

}  // namespace tradelab
