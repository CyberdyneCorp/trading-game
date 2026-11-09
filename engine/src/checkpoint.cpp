#include "tradelab/checkpoint.hpp"

#include <string>

#include "tradelab/engine.hpp"
#include "tradelab/serialize.hpp"

namespace tradelab {
namespace {

constexpr std::uint32_t kMagic = 0x4B434C54;  // "TLCK"
constexpr std::uint32_t kMaxItems = 10'000'000;

// One field visitor drives both directions, so save and load cannot drift apart.
struct Out {
  Writer& w;
  bool ok = true;
  void u8(std::uint8_t& v) { w.u8(v); }
  void u32(std::uint32_t& v) { w.u32(v); }
  void u64(std::uint64_t& v) { w.u64(v); }
  void i64(std::int64_t& v) { w.i64(v); }
  void i32(std::int32_t& v) { w.u32(static_cast<std::uint32_t>(v)); }
  void flag(bool& v) { w.boolean(v); }
  template <typename E>
  void en(E& e) {
    w.u8(static_cast<std::uint8_t>(e));
  }
  template <typename F>
  void fx(F& f) {
    w.i64(f.raw);
  }
  bool count(std::uint32_t& n) {
    w.u32(n);
    return true;
  }
};

struct In {
  Reader& r;
  bool ok = true;
  template <typename T>
  void take(const Result<T>& x, T& out) {
    if (!x.ok()) ok = false;
    else out = x.value();
  }
  void u8(std::uint8_t& v) { take(r.u8(), v); }
  void u32(std::uint32_t& v) { take(r.u32(), v); }
  void u64(std::uint64_t& v) { take(r.u64(), v); }
  void i64(std::int64_t& v) { take(r.i64(), v); }
  void i32(std::int32_t& v) {
    std::uint32_t u = 0;
    u32(u);
    v = static_cast<std::int32_t>(u);
  }
  void flag(bool& v) { take(r.boolean(), v); }
  template <typename E>
  void en(E& e) {
    std::uint8_t u = 0;
    u8(u);
    e = static_cast<E>(u);
  }
  template <typename F>
  void fx(F& f) {
    i64(f.raw);
  }
  bool count(std::uint32_t& n) {
    u32(n);
    if (n > kMaxItems) ok = false;
    return ok;
  }
};

template <typename Ar>
void visit(Ar& a, Order& o) {
  a.u32(o.id);
  a.u64(o.seq);
  a.en(o.type);
  a.en(o.side);
  a.fx(o.qty);
  a.fx(o.filled);
  a.fx(o.price);
  a.flag(o.reduce_only);
  a.u32(o.placed_step);
  a.en(o.status);
  a.fx(o.bracket_stop);
  a.fx(o.bracket_tp);
  a.u32(o.parent_id);
  a.u32(o.oco_group);
  a.fx(o.trail_distance);
  a.u32(o.series);
  a.fx(o.trail_rate);  // checkpoint format 4 (policy v16)
  a.en(o.tif);         // checkpoint format 6 (policy v18)
  a.u32(o.tif_bars);
  a.u32(o.expire_step);
  a.u32(o.link_group);
}

template <typename Ar>
void visit(Ar& a, Event& e) {
  a.u64(e.seq);
  a.u32(e.step);
  a.en(e.type);
  a.u32(e.order_id);
  a.en(e.status);
  a.en(e.reason);
  a.u8(e.flags);
  a.en(e.side);
  a.fx(e.qty);
  a.fx(e.price);
  a.fx(e.requested);
  a.fx(e.amount);
  a.fx(e.pnl);
  a.u32(e.series);
}

template <typename Ar>
void visit(Ar& a, OptionPosition& p) {
  a.u32(p.series);
  a.fx(p.qty);
  a.fx(p.avg_premium);
}

template <typename Ar>
void visit(Ar& a, CommandRecord& c) {
  a.u64(c.client_id);
  a.flag(c.result.accepted);
  a.en(c.result.reason);
  a.u32(c.result.order_id);
  a.en(c.result.warning);
}

template <typename Ar>
void visit(Ar& a, PlanRecord& p) {
  a.u32(p.step);
  a.en(p.side);
  a.flag(p.skipped);
  a.flag(p.option);
  a.flag(p.has_lose_scenario);
  a.fx(p.invalidation);
  a.fx(p.target);
  a.u32(p.confidence_pct);
  a.fx(p.planned_risk);
}

template <typename Ar>
void visit(Ar& a, CalibrationAnswer& x) {
  a.u32(x.step);
  a.fx(x.threshold);
  a.u32(x.horizon);
  a.u32(x.p_bp);
  a.flag(x.skipped);
}

template <typename Ar>
void visit(Ar& a, Metrics& m) {
  a.fx(m.peak_equity);
  a.fx(m.max_drawdown);
  a.i64(m.peak_leverage_bp);
  a.i64(m.min_liq_distance_bp);
  a.u32(m.liquidations);
  a.u32(m.fills);
  a.u32(m.ambiguous_fills);
  a.fx(m.turnover);
}

template <typename Ar>
void visit(Ar& a, AdviceReason& r) {
  a.en(r.code);
  std::uint8_t vote = static_cast<std::uint8_t>(r.vote);
  a.u8(vote);
  r.vote = static_cast<std::int8_t>(vote);
  a.i32(r.ref);
}

template <typename Ar>
void visit(Ar& a, LessonAnswer& x) {
  a.u32(x.step);
  a.u32(x.question);
  a.u32(x.choice);
  a.fx(x.value);
  a.flag(x.skipped);
}

template <typename Ar>
void visit(Ar& a, DecisionRecord& x) {
  a.u32(x.step);
  a.en(x.kind);
}

template <typename Ar, typename Seq>
void visit_seq(Ar& a, Seq& items) {
  auto n = static_cast<std::uint32_t>(items.size());
  if (!a.count(n)) return;
  items.resize(n);
  for (auto& x : items) visit(a, x);
}

template <typename Ar>
void visit(Ar& a, Advice& x) {
  a.u32(x.step);
  a.en(x.stance);
  a.u32(x.confidence_bp);
  a.fx(x.stop);
  a.fx(x.size);
  visit_seq(a, x.reasons);
}

template <typename Ar>
void visit(Ar& a, RunState& st) {
  a.u64(st.seed);
  a.u32(st.step);
  a.en(st.status);
  a.i32(st.leverage);
  a.fx(st.wallet);
  a.fx(st.position.qty);
  a.fx(st.position.entry);
  a.fx(st.position.margin);
  visit_seq(a, st.orders);
  a.u32(st.next_order_id);
  a.u64(st.next_event_seq);
  a.u64(st.last_command_seq);
  a.fx(st.realized_pnl);
  a.fx(st.fees_paid);
  a.fx(st.funding_net);
  a.fx(st.insurance_absorbed);
  visit_seq(a, st.option_positions);
  a.fx(st.option_cash);
  a.fx(st.option_realized);
  visit(a, st.metrics);
  visit_seq(a, st.plans);
  visit_seq(a, st.answers);
  visit_seq(a, st.events);
  visit_seq(a, st.command_log);
  std::uint64_t rs = st.rng.state();
  std::uint64_t ri = st.rng.inc();
  a.u64(rs);
  a.u64(ri);
  st.rng = Pcg32::from_state(rs, ri);
  a.u32(st.options.advice_allowance);
  a.u8(st.options.news_lead);
  a.flag(st.options.calendar);
  a.u32(st.advice_left);
  visit_seq(a, st.advice);
  a.fx(st.options.starting_balance);
  a.i32(st.options.leverage_cap);
  visit_seq(a, st.lesson_answers);  // format 5 (scoring v11)
  a.i64(st.last_link_step);         // format 6 (policy v18)
  visit_seq(a, st.decisions);
}


}  // namespace

std::vector<std::uint8_t> save_checkpoint(const RunState& st, std::string_view scenario_id) {
  Writer w;
  w.u32(kMagic);
  w.u32(kCheckpointFormat);
  w.u32(kExecutionPolicyVersion);
  w.str(scenario_id);
  RunState copy = st;  // the visitor takes non-const references
  Out out{w};
  visit(out, copy);
  return w.data();
}

Result<RunState> load_checkpoint(std::span<const std::uint8_t> data, std::string_view scenario_id) {
  Reader r(data);
  const auto magic = r.u32();
  const auto format = r.u32();
  if (!magic.ok() || !format.ok()) return ErrorCode::TruncatedInput;
  if (magic.value() != kMagic || format.value() != kCheckpointFormat) return ErrorCode::NonCanonicalEncoding;
  const auto policy = r.u32();
  if (!policy.ok()) return ErrorCode::TruncatedInput;
  if (policy.value() != kExecutionPolicyVersion) return ErrorCode::IncompatiblePolicy;
  const auto id = r.str();
  if (!id.ok()) return ErrorCode::TruncatedInput;
  if (id.value() != scenario_id) return ErrorCode::ScenarioMismatch;
  RunState st;
  In in{r};
  visit(in, st);
  if (!in.ok) return ErrorCode::TruncatedInput;
  if (!r.at_end()) return ErrorCode::NonCanonicalEncoding;
  return st;
}

}  // namespace tradelab
