#include "tradelab/c_api.h"

#include <algorithm>
#include <string>

#include "accounting.hpp"
#include "json_out.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"

using tradelab::detail::JsonOut;

struct tl_run {  // NOLINT(readability-identifier-naming): C ABI type
  tradelab::Scenario scenario;
  tradelab::RunState state;
  std::uint64_t next_seq = 1;
  std::string buffer;
};

namespace tradelab {
namespace {

const char* reason_name(Reason r) {
  static constexpr const char* kNames[] = {
      "none", "insufficient_margin", "leverage_above_cap", "invalid_tick", "invalid_quantity",
      "unsupported_order_type", "reduce_only_would_increase", "run_bankrupt", "run_finished",
      "insufficient_capacity", "liquidation", "position_closed", "user_cancelled", "unknown_order",
      "invalid_sequence", "wrong_decision_step", "numeric_overflow", "invalid_leverage", "position_open",
      "stop_beyond_liquidation", "short_not_allowed", "expired", "invalid_bracket", "oco_sibling_filled"};
  const auto i = static_cast<std::size_t>(r);
  return i < std::size(kNames) ? kNames[i] : "unknown";
}

const char* status_name(OrderStatus s) {
  static constexpr const char* kNames[] = {"accepted", "partially_filled", "filled", "cancelled",
                                           "rejected", "expired", "triggered"};
  return kNames[static_cast<std::size_t>(s)];
}

const char* type_name(OrderType t) {
  static constexpr const char* kNames[] = {"market", "limit", "stop_market", "take_profit", "trailing_stop"};
  return kNames[static_cast<std::size_t>(t)];
}

const char* event_name(EventType t) {
  static constexpr const char* kNames[] = {"order",    "fill",         "funding",         "liquidation",
                                           "bankrupt", "run_finished", "leverage_changed"};
  return kNames[static_cast<std::size_t>(t)];
}

const char* run_status_name(RunStatus s) {
  static constexpr const char* kNames[] = {"active", "finished", "bankrupt"};
  return kNames[static_cast<std::size_t>(s)];
}

const char* side_name(OrderSide s) { return s == OrderSide::Buy ? "buy" : "sell"; }

void write_contract(JsonOut& j, const Scenario& sc) {
  const ContractSpec& k = sc.contract;
  j.key("contract").begin_obj();
  j.field("tickSize", k.tick_size).field("lotSize", k.lot_size).field("minQty", k.min_qty);
  j.field("multiplier", k.multiplier).field("maxLeverage", k.max_leverage);
  j.field("leverageCap", std::min(sc.mission.leverage_cap, k.max_leverage));
  j.field("makerFee", k.maker_fee).field("takerFee", k.taker_fee).field("maintenanceMargin", k.maintenance_margin);
  j.field("fundingInterval", k.funding_interval).field("allowShort", sc.mission.allow_short);
  j.field("halfSpread", sc.costs.half_spread).field("slippage", sc.costs.slippage);
  j.field("capacityFraction", sc.costs.capacity_fraction);
  j.end_obj();
}

// Only bars 0..step: the future never crosses the ABI.
void write_bars(JsonOut& j, const Scenario& sc, std::uint32_t last) {
  j.key("bars").begin_arr();
  for (std::uint32_t i = 0; i <= last; ++i) {
    const Bar& b = sc.bars[i];
    j.begin_arr().dec(b.open).dec(b.high).dec(b.low).dec(b.close).dec(b.volume).boolean(b.synthetic).end_arr();
  }
  j.end_arr();
}

void write_account(JsonOut& j, const Scenario& sc, const RunState& st) {
  const AccountView v = account_view(sc, st);
  j.key("account").begin_obj();
  j.field("wallet", st.wallet).field("equity", v.equity).field("unrealized", v.unrealized);
  j.field("positionMargin", v.position_margin).field("orderMargin", v.order_margin);
  j.field("available", v.available).field("notional", v.notional).field("mark", v.mark);
  j.key("liquidationPrice");
  if (v.has_liquidation) j.dec(v.liquidation_price); else j.null();
  j.field("nextFundingRate", v.next_funding_rate).field("nextFundingStep", v.next_funding_step);
  j.end_obj();
  j.key("position").begin_obj();
  j.field("qty", st.position.qty).field("entry", st.position.entry).field("margin", st.position.margin);
  j.end_obj();
}

void write_orders(JsonOut& j, const RunState& st) {
  j.key("orders").begin_arr();
  for (const Order& o : st.orders) {
    if (!o.active()) continue;
    j.begin_obj().field("id", o.id).field("type", type_name(o.type)).field("side", side_name(o.side));
    j.field("qty", o.qty).field("filled", o.filled).field("price", o.price);
    j.field("reduceOnly", o.reduce_only).field("status", status_name(o.status)).field("placedStep", o.placed_step);
    j.field("parentId", o.parent_id).field("ocoGroup", o.oco_group).field("trailDistance", o.trail_distance);
    j.field("bracketStop", o.bracket_stop).field("bracketTp", o.bracket_tp);
    j.end_obj();
  }
  j.end_arr();
}

void write_event(JsonOut& j, const Event& e) {
  j.begin_obj().field("seq", e.seq).field("step", e.step).field("type", event_name(e.type));
  j.field("orderId", e.order_id).field("status", status_name(e.status)).field("reason", reason_name(e.reason));
  j.field("side", side_name(e.side)).field("qty", e.qty).field("price", e.price).field("requested", e.requested);
  j.field("amount", e.amount).field("pnl", e.pnl);
  j.field("ambiguous", (e.flags & kFlagAmbiguousIntrabar) != 0).field("gap", (e.flags & kFlagGapFill) != 0);
  j.field("capacityLimited", (e.flags & kFlagCapacityFromBarVolume) != 0);
  j.end_obj();
}

void write_totals(JsonOut& j, const RunState& st) {
  const Metrics& m = st.metrics;
  j.key("totals").begin_obj();
  j.field("realizedPnl", st.realized_pnl).field("feesPaid", st.fees_paid).field("fundingNet", st.funding_net);
  j.field("insuranceAbsorbed", st.insurance_absorbed).field("turnover", m.turnover);
  j.field("peakEquity", m.peak_equity).field("maxDrawdown", m.max_drawdown);
  j.field("peakLeverageBp", m.peak_leverage_bp).field("minLiqDistanceBp", m.min_liq_distance_bp);
  j.field("liquidations", m.liquidations).field("fills", m.fills).field("ambiguousFills", m.ambiguous_fills);
  j.end_obj();
}

const char* state_json(tl_run& r) {
  const Scenario& sc = r.scenario;
  const RunState& st = r.state;
  JsonOut j;
  j.begin_obj();
  j.field("mission", sc.id).field("alias", sc.alias).field("status", run_status_name(st.status));
  j.field("step", st.step).field("lastDecisionStep", last_decision_step(sc));
  j.field("warmupBars", sc.mission.warmup_bars).field("totalBars", static_cast<std::uint32_t>(sc.bars.size()));
  j.field("initialBalance", sc.mission.initial_balance).field("leverage", st.leverage);
  write_contract(j, sc);
  write_bars(j, sc, st.step);
  write_account(j, sc, st);
  write_orders(j, st);
  j.key("events").begin_arr();
  for (const Event& e : st.events) write_event(j, e);
  j.end_arr();
  write_totals(j, st);
  j.field("digest", to_hex(state_digest(st))).field("eventsDigest", to_hex(events_digest(st)));
  j.end_obj();
  r.buffer = j.text();
  return r.buffer.c_str();
}

const char* result_json(tl_run& r, const CommandResult& res) {
  JsonOut j;
  j.begin_obj().field("accepted", res.accepted).field("reason", reason_name(res.reason));
  j.field("orderId", res.order_id).key("warning");
  if (res.warning == Reason::None) j.null(); else j.str(reason_name(res.warning));
  j.end_obj();
  r.buffer = j.text();
  return r.buffer.c_str();
}

const char* error_json(tl_run& r, const char* code) {
  JsonOut j;
  j.begin_obj().field("accepted", false).field("reason", code).field("orderId", 0).key("warning").null().end_obj();
  r.buffer = j.text();
  return r.buffer.c_str();
}

// Empty or null means "not set" unless required. Returns false on a malformed value.
bool parse_optional_price(const char* text, Price* out, bool required) {
  if (text == nullptr || text[0] == '\0') return !required;
  const auto p = parse_decimal<Price>(text);
  if (!p.ok()) return false;
  *out = p.value();
  return true;
}

Command next_command(tl_run& r, CommandType type, std::uint32_t client_id) {
  Command c;
  c.type = type;
  c.seq = r.next_seq++;
  c.client_id = client_id;
  c.decision_step = r.state.step;
  return c;
}

// Buy-and-hold benchmark: 1x long from the first eligible open to the final close, taker fees both ways.
Money buy_and_hold(const Scenario& sc) {
  const Price entry = sc.bars[sc.mission.warmup_bars + 1].open;
  const Price exit = sc.bars.back().close;
  const Money budget = sc.mission.initial_balance;
  const Qty raw = detail::unwrap(div<Qty>(budget, Money::from_raw(entry.raw), Rounding::Floor));
  const Qty qty = detail::unwrap(round_to_step(Qty::from_raw(raw.raw * 999 / 1000), sc.contract.lot_size, Rounding::Floor));
  const Money pnl = detail::unwrap(mul<Money>(qty, Price::from_raw(exit.raw - entry.raw), Rounding::Floor));
  const Money fees = Money::from_raw(detail::fee_for(detail::notional(sc, qty, entry, Rounding::Ceil), sc.contract.taker_fee).raw +
                                     detail::fee_for(detail::notional(sc, qty, exit, Rounding::Ceil), sc.contract.taker_fee).raw);
  return Money::from_raw(pnl.raw - fees.raw);
}

}  // namespace
}  // namespace tradelab

using namespace tradelab;

extern "C" {

const char* tl_version(void) {
  static const std::string version = std::string("{\"engine\":\"0.1.0\",\"executionPolicy\":") +
                                      std::to_string(kExecutionPolicyVersion) + "}";
  return version.c_str();
}

const char* tl_missions(void) {
  static std::string buffer;
  JsonOut j;
  j.begin_arr();
  for (const MissionInfo& m : mission_catalog()) {
    j.begin_obj().field("id", m.id).field("totalBars", m.total_bars).field("warmupBars", m.warmup_bars);
    j.field("leverageCap", m.leverage_cap).field("allowShort", m.allow_short).end_obj();
  }
  j.end_arr();
  buffer = j.text();
  return buffer.c_str();
}

tl_run* tl_run_create(const char* mission_id, uint32_t seed) {
  auto* run = new tl_run();
  if (mission_id == nullptr || !build_mission(mission_id, seed, run->scenario)) {
    delete run;
    return nullptr;
  }
  run->state = start_run(run->scenario, seed);
  return run;
}

void tl_run_destroy(tl_run* run) { delete run; }

const char* tl_place_order(tl_run* run, uint32_t client_id, int32_t type, int32_t side, const char* qty,
                           const char* price, int32_t reduce_only) {
  return tl_place_order_ex(run, client_id, type, side, qty, price, reduce_only, nullptr, nullptr, nullptr);
}

const char* tl_place_order_ex(tl_run* run, uint32_t client_id, int32_t type, int32_t side, const char* qty,
                              const char* price, int32_t reduce_only, const char* attach_stop,
                              const char* attach_tp, const char* trail_distance) {
  if (type < 0 || type > 4 || side < 0 || side > 1) return error_json(*run, "unsupported_order_type");
  Command c = next_command(*run, CommandType::PlaceOrder, client_id);
  c.order_type = static_cast<OrderType>(type);
  c.side = static_cast<OrderSide>(side);
  const auto q = parse_decimal<Qty>(qty == nullptr ? "" : qty);
  if (!q.ok()) return error_json(*run, "invalid_quantity");
  c.qty = q.value();
  const bool needs_price = c.order_type != OrderType::Market && c.order_type != OrderType::TrailingStop;
  if (needs_price && !parse_optional_price(price, &c.price, true)) return error_json(*run, "invalid_tick");
  if (!parse_optional_price(attach_stop, &c.attach_stop, false) || !parse_optional_price(attach_tp, &c.attach_tp, false) ||
      !parse_optional_price(trail_distance, &c.trail_distance, false)) {
    return error_json(*run, "invalid_tick");
  }
  c.reduce_only = reduce_only != 0;
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_cancel_order(tl_run* run, uint32_t client_id, uint32_t order_id) {
  Command c = next_command(*run, CommandType::CancelOrder, client_id);
  c.order_id = order_id;
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_set_leverage(tl_run* run, uint32_t client_id, int32_t leverage) {
  Command c = next_command(*run, CommandType::SetLeverage, client_id);
  c.leverage = leverage;
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_close_position(tl_run* run, uint32_t client_id) {
  return result_json(*run, apply_command(run->scenario, run->state,
                                         next_command(*run, CommandType::ClosePosition, client_id)));
}

const char* tl_advance(tl_run* run, int32_t steps) {
  for (int32_t i = 0; i < steps && run->state.status == RunStatus::Active; ++i) advance(run->scenario, run->state);
  return state_json(*run);
}

const char* tl_state(tl_run* run) { return state_json(*run); }

const char* tl_debrief(tl_run* run) {
  if (run->state.status == RunStatus::Active) {
    run->buffer = "{\"error\":\"run_active\"}";
    return run->buffer.c_str();
  }
  const Scenario& sc = run->scenario;
  JsonOut j;
  j.begin_obj().field("mission", sc.id).field("alias", sc.alias);
  j.field("status", run_status_name(run->state.status));
  j.field("finalEquity", account_view(sc, run->state).equity);
  j.field("buyAndHoldPnl", buy_and_hold(sc)).field("flatPnl", Money{});
  j.field("syntheticMarket", true).field("seed", run->state.seed);
  j.field("startPrice", sc.bars[sc.mission.warmup_bars].close).field("endPrice", sc.bars.back().close);
  j.end_obj();
  run->buffer = j.text();
  return run->buffer.c_str();
}

}  // extern "C"
