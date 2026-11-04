#pragma once

// Test helper: build small hand-written scenarios with zero costs unless overridden.

#include <initializer_list>
#include <string>

#include <doctest/doctest.h>

#include "tradelab/engine.hpp"

namespace doctest {
template <typename Tag, int D>
struct StringMaker<tradelab::Fixed<Tag, D>> {
  static String convert(const tradelab::Fixed<Tag, D>& v) { return tradelab::to_decimal_string(v).c_str(); }
};
}  // namespace doctest

namespace tradelab::test {

inline Price P(const char* s) { return parse_decimal<Price>(s).value(); }
inline Qty Q(const char* s) { return parse_decimal<Qty>(s).value(); }
inline Money M(const char* s) { return parse_decimal<Money>(s).value(); }
inline Rate R(const char* s) { return parse_decimal<Rate>(s).value(); }

struct BarSpec {
  const char* o;
  const char* h;
  const char* l;
  const char* c;
};

inline Scenario make_scenario(std::initializer_list<BarSpec> bars, std::uint32_t warmup = 0) {
  Scenario s;
  s.id = "test";
  s.alias = "T1";
  s.contract.multiplier = Q("1");
  s.contract.tick_size = P("0.01");
  s.contract.lot_size = Q("0.1");
  s.contract.min_qty = Q("0.1");
  s.contract.max_leverage = 20;
  s.contract.maker_fee = Rate{};
  s.contract.taker_fee = Rate{};
  s.contract.maintenance_margin = R("0.01");
  s.contract.funding_interval = 1000;  // effectively off unless a test sets it
  s.costs.half_spread = Rate{};
  s.costs.slippage = Rate{};
  s.costs.capacity_fraction = rate_one();
  s.mission.initial_balance = M("10000");
  s.mission.leverage_cap = 20;
  s.mission.warmup_bars = warmup;
  for (const BarSpec& b : bars) s.bars.push_back({P(b.o), P(b.h), P(b.l), P(b.c), Q("1000000"), false});
  s.funding_rate.assign(s.bars.size(), Rate{});
  return s;
}

// Adds an option chain: constant 50% implied vol, ids 1.. for each (kind, strike) at one expiry.
inline void add_options(Scenario& s, std::uint32_t expiry, std::initializer_list<const char*> strikes, bool allow_short = false) {
  OptionSpec& o = s.options;
  o.multiplier = Q("1");
  o.premium_tick = P("0.01");
  o.lot_size = Q("0.1");
  o.fee_rate = R("0.0003");
  o.spread = R("0.02");
  o.iv_base.assign(s.bars.size(), parse_decimal<Vol>("0.5").value());
  o.im_base = R("0.15");
  o.im_floor = R("0.10");
  o.mm_base = R("0.075");
  o.allow_short = allow_short;
  std::uint32_t id = 1;
  for (const char* k : strikes) {
    o.series.push_back({id++, OptionKind::Call, P(k), 0, expiry});
    o.series.push_back({id++, OptionKind::Put, P(k), 0, expiry});
  }
}

// Issues commands with automatic sequence numbers and client ids.
class Driver {
 public:
  explicit Driver(const Scenario& sc, std::uint64_t seed = 1) : sc_(sc), st_(start_run(sc, seed)) {}

  CommandResult order(OrderType t, OrderSide side, const char* qty, const char* price = nullptr,
                      bool reduce_only = false) {
    Command c = base(CommandType::PlaceOrder);
    c.order_type = t;
    c.side = side;
    c.qty = Q(qty);
    if (price != nullptr) c.price = P(price);
    c.reduce_only = reduce_only;
    return apply_command(sc_, st_, c);
  }
  CommandResult bracket(OrderType t, OrderSide side, const char* qty, const char* price, const char* stop,
                        const char* tp) {
    Command c = base(CommandType::PlaceOrder);
    c.order_type = t;
    c.side = side;
    c.qty = Q(qty);
    if (price != nullptr) c.price = P(price);
    if (stop != nullptr) c.attach_stop = P(stop);
    if (tp != nullptr) c.attach_tp = P(tp);
    return apply_command(sc_, st_, c);
  }
  CommandResult trailing(OrderSide side, const char* qty, const char* distance, bool reduce_only = true) {
    Command c = base(CommandType::PlaceOrder);
    c.order_type = OrderType::TrailingStop;
    c.side = side;
    c.qty = Q(qty);
    c.trail_distance = P(distance);
    c.reduce_only = reduce_only;
    return apply_command(sc_, st_, c);
  }
  CommandResult option(std::uint32_t series, OrderSide side, const char* qty, const char* limit = nullptr) {
    Command c = base(CommandType::PlaceOptionOrder);
    c.series = series;
    c.side = side;
    c.order_type = limit == nullptr ? OrderType::Market : OrderType::Limit;
    c.qty = Q(qty);
    if (limit != nullptr) c.price = P(limit);
    return apply_command(sc_, st_, c);
  }
  CommandResult margin(const char* delta) {
    Command c = base(CommandType::AdjustMargin);
    c.margin_delta = M(delta);
    return apply_command(sc_, st_, c);
  }
  CommandResult plan(OrderSide side, const char* invalidation, const char* risk, bool skipped = false, bool option = false) {
    Command c = base(CommandType::RecordPlan);
    c.plan.side = side;
    c.plan.skipped = skipped;
    c.plan.option = option;
    if (invalidation != nullptr) c.plan.invalidation = P(invalidation);
    if (risk != nullptr) c.plan.planned_risk = M(risk);
    c.plan.confidence_pct = 60;
    return apply_command(sc_, st_, c);
  }
  CommandResult answer(std::uint32_t p_bp, bool skipped = false) {
    Command c = base(CommandType::AnswerQuestion);
    c.p_bp = p_bp;
    c.skipped = skipped;
    return apply_command(sc_, st_, c);
  }
  CommandResult leverage(int lev) {
    Command c = base(CommandType::SetLeverage);
    c.leverage = lev;
    return apply_command(sc_, st_, c);
  }
  CommandResult cancel(std::uint32_t id) {
    Command c = base(CommandType::CancelOrder);
    c.order_id = id;
    return apply_command(sc_, st_, c);
  }
  CommandResult close() { return apply_command(sc_, st_, base(CommandType::ClosePosition)); }
  void step(int n = 1) {
    for (int i = 0; i < n; ++i) advance(sc_, st_);
  }

  RunState& state() { return st_; }
  const Scenario& scenario() const { return sc_; }
  AccountView view() const { return account_view(sc_, st_); }

 private:
  Command base(CommandType t) {
    Command c;
    c.type = t;
    c.seq = ++seq_;
    c.client_id = seq_;
    c.decision_step = st_.step;
    return c;
  }
  const Scenario& sc_;
  RunState st_;
  std::uint64_t seq_ = 0;
};

inline const Order* find_order(const RunState& st, std::uint32_t id) {
  for (const Order& o : st.orders) {
    if (o.id == id) return &o;
  }
  return nullptr;
}

inline std::vector<const Order*> children_of(const RunState& st, std::uint32_t parent) {
  std::vector<const Order*> out;
  for (const Order& o : st.orders) {
    if (o.parent_id == parent) out.push_back(&o);
  }
  return out;
}

template <typename D>
bool detail_liq(D& d, Price* out) {
  const AccountView v = d.view();
  *out = v.liquidation_price;
  return v.has_liquidation;
}

inline const Event* last_of(const RunState& st, EventType t) {
  for (auto it = st.events.rbegin(); it != st.events.rend(); ++it) {
    if (it->type == t) return &*it;
  }
  return nullptr;
}

}  // namespace tradelab::test
