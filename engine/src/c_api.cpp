#include "tradelab/c_api.h"

#include <algorithm>
#include <string>

#include "accounting.hpp"
#include "json_out.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/checkpoint.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/candle_patterns.hpp"
#include "tradelab/indicators.hpp"
#include "tradelab/options.hpp"
#include "tradelab/scoring.hpp"
#include "options_exec.hpp"

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
      "stop_beyond_liquidation", "short_not_allowed", "expired", "invalid_bracket", "oco_sibling_filled", "series_not_listed", "short_options_not_allowed", "no_bid", "margin_call", "margin_below_initial", "no_question_due", "invalid_plan"};
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
                                           "bankrupt", "run_finished", "leverage_changed", "option_settlement", "margin_call", "margin_adjusted"};
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
    j.field("bracketStop", o.bracket_stop).field("bracketTp", o.bracket_tp).field("series", o.series);
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
  j.field("capacityLimited", (e.flags & kFlagCapacityFromBarVolume) != 0).field("series", e.series);
  j.field("approximate", (e.flags & kFlagMarkFromLastPrice) != 0);
  j.end_obj();
}

void write_totals(JsonOut& j, const RunState& st) {
  const Metrics& m = st.metrics;
  j.key("totals").begin_obj();
  j.field("realizedPnl", st.realized_pnl).field("feesPaid", st.fees_paid).field("fundingNet", st.funding_net);
  j.field("insuranceAbsorbed", st.insurance_absorbed).field("turnover", m.turnover);
  j.field("optionCash", st.option_cash).field("optionRealized", st.option_realized);
  j.field("peakEquity", m.peak_equity).field("maxDrawdown", m.max_drawdown);
  j.field("peakLeverageBp", m.peak_leverage_bp).field("minLiqDistanceBp", m.min_liq_distance_bp);
  j.field("liquidations", m.liquidations).field("fills", m.fills).field("ambiguousFills", m.ambiguous_fills);
  j.end_obj();
}

void write_quote(JsonOut& j, const OptionSeries& s, const OptionQuote& q) {
  j.begin_obj().field("id", s.id).field("kind", s.kind == OptionKind::Call ? "call" : "put").field("strike", s.strike);
  j.field("listStep", s.list_step).field("expiryStep", s.expiry_step).field("listed", q.listed);
  j.field("bid", q.bid).field("ask", q.ask).field("mid", q.mid).field("iv", q.iv);
  j.field("delta", q.delta).field("gamma", q.gamma).field("theta", q.theta).field("vega", q.vega);
  j.field("ivFromRealized", q.iv_from_realized).end_obj();
}

// Chain at the current decision step only: listed, unexpired series quoted from the current close.
void write_options(JsonOut& j, const Scenario& sc, const RunState& st) {
  const OptionSpec& o = sc.options;
  const Price f = sc.bars[st.step].close;
  const detail::OptionBook b = detail::option_book(sc, st, st.step);
  j.key("options").begin_obj().field("enabled", !o.series.empty()).field("allowShort", o.allow_short);
  j.field("multiplier", o.multiplier).field("premiumTick", o.premium_tick).field("lotSize", o.lot_size);
  j.field("longValue", b.long_value).field("shortValue", b.short_value);
  j.field("initialMargin", b.initial_margin).field("maintenanceMargin", b.maintenance_margin);
  j.key("chain").begin_arr();
  for (const OptionSeries& s : o.series) {
    if (st.step >= s.list_step && st.step < s.expiry_step) write_quote(j, s, quote(sc, s, st.step, f));
  }
  j.end_arr().key("positions").begin_arr();
  for (const OptionPosition& p : st.option_positions) {
    j.begin_obj().field("series", p.series).field("qty", p.qty).field("avgPremium", p.avg_premium).end_obj();
  }
  j.end_arr().end_obj();
}

void write_series(JsonOut& j, std::string_view name, const Series& series) {
  j.key(name).begin_arr();
  for (const auto& v : series) {
    if (v) j.dec(*v);
    else j.null();
  }
  j.end_arr();
}

constexpr std::uint32_t kVwapSession = 24;  // one day of hourly bars

// Indicators over closed bars 0..step (null while warming up).
void write_indicators(JsonOut& j, const Scenario& sc, std::uint32_t step) {
  j.key("indicators").begin_obj().field("version", kIndicatorVersion);
  write_series(j, "sma20", sma(sc, step, 20));
  write_series(j, "ema50", ema(sc, step, 50));
  write_series(j, "rsi14", rsi(sc, step, 14));
  write_series(j, "atr14", atr(sc, step, 14));
  write_series(j, "vwap", vwap(sc, step, kVwapSession));
  const Bollinger bb = bollinger(sc, step, 20, 2);
  write_series(j, "bbUpper", bb.upper);
  write_series(j, "bbLower", bb.lower);
  const Macd m = macd(sc, step, 12, 26, 9);
  write_series(j, "macd", m.line);
  write_series(j, "macdSignal", m.signal);
  write_series(j, "macdHist", m.histogram);
  const Adx a = adx(sc, step, 14);
  write_series(j, "plusDi14", a.plus_di);
  write_series(j, "minusDi14", a.minus_di);
  write_series(j, "adx14", a.adx);
  j.field("patternVersion", kPatternVersion).key("patterns").begin_arr();
  for (const PatternHit& h : detect_patterns(sc, step)) {
    j.begin_obj().field("step", h.step).field("id", h.id).field("bars", std::uint32_t{h.bars}).field("bias", std::int32_t{h.bias});
    j.end_obj();
  }
  j.end_arr();
  j.end_obj();
}

const char* news_kind_name(NewsKind k) {
  static constexpr const char* kNames[] = {"rate_decision",  "regulation_rumor",       "exchange_incident", "network_outage",
                                           "liquidity_inflow", "institutional_adoption", "rumor_denied"};
  return kNames[static_cast<std::size_t>(k)];
}

// Only news published at or before the current step: the future never crosses the ABI.
void write_news(JsonOut& j, const Scenario& sc, std::uint32_t step) {
  j.key("news").begin_arr();
  for (const NewsItem& n : sc.news) {
    if (n.release_step > step) continue;
    j.begin_obj().field("step", n.release_step).field("kind", news_kind_name(n.kind)).field("tone", std::int32_t{n.tone});
    j.field("credibility", std::uint32_t{n.credibility}).field("expectation", std::int32_t{n.expectation});
    j.field("actual", std::int32_t{n.actual}).end_obj();
  }
  j.end_arr();
}

void write_calibration_state(JsonOut& j, const Scenario& sc, const RunState& st) {
  j.key("calibration").begin_obj().field("due", question_due(sc, st)).field("horizon", kQuestionHorizon);
  j.field("every", kQuestionEvery).field("threshold", sc.bars[st.step].close).field("answered", static_cast<std::uint32_t>(st.answers.size()));
  j.end_obj();
}

const char* finding_name(FindingCode c) {
  static constexpr const char* kNames[] = {"waited",          "liquidated", "stop_beyond_liq", "no_stop",   "gap_slippage",
                                           "funding_cost",    "ambiguous",  "capacity",        "good_process_loss",
                                           "lucky_win",       "no_plan",    "oversized",       "bankrupt"};
  return kNames[static_cast<std::size_t>(c)];
}

const char* tone_name(FindingTone t) {
  static constexpr const char* kNames[] = {"good", "warn", "bad", "info"};
  return kNames[static_cast<std::size_t>(t)];
}

const char* lesson_name(LessonCode l) {
  static constexpr const char* kNames[] = {"futures2", "risk", "futures3", "orders", "structure", "evaluation"};
  return kNames[static_cast<std::size_t>(l)];
}

const char* exit_name(ExitReason e) {
  static constexpr const char* kNames[] = {"stop", "target", "manual", "liquidation", "open"};
  return kNames[static_cast<std::size_t>(e)];
}

void write_dim(JsonOut& j, std::string_view name, const Dimension& d) {
  j.key(name).begin_obj().field("weight", d.weight).key("score");
  if (d.score) j.num(*d.score); else j.null();
  j.end_obj();
}

void write_trade(JsonOut& j, const TradeRow& t) {
  j.begin_obj().field("n", t.n).field("side", t.is_long ? "long" : "short").field("entryStep", t.entry_step).key("exitStep");
  if (t.exit_step) j.num(*t.exit_step); else j.null();
  j.field("qty", t.qty).field("entryAvg", t.entry_avg).field("exitAvg", t.exit_avg).field("pnl", t.pnl);
  j.field("fees", t.fees).field("net", t.net).field("bars", t.bars).field("reason", exit_name(t.reason));
  j.field("plannedRisk", t.planned_risk).key("rBp");
  if (t.r_bp) j.num(*t.r_bp); else j.null();
  j.key("planRespected");
  if (t.plan_respected) j.boolean(*t.plan_respected); else j.null();
  j.end_obj();
}

void write_score(JsonOut& j, const ScoreReport& r) {
  j.key("score").begin_obj().field("version", kScoringVersion).key("total");
  if (r.total) j.num(*r.total); else j.null();
  j.key("dims").begin_obj();
  write_dim(j, "risk", r.risk);
  write_dim(j, "plan", r.plan);
  write_dim(j, "understanding", r.understanding);
  write_dim(j, "calibration", r.calibration);
  j.end_obj().end_obj();
  j.key("findings").begin_arr();
  for (const Finding& f : r.findings) {
    j.begin_obj().field("code", finding_name(f.code)).field("tone", tone_name(f.tone)).field("step", f.step);
    j.field("count", f.count).field("price", f.price).field("requested", f.requested).field("amount", f.amount).end_obj();
  }
  j.end_arr().field("lesson", lesson_name(r.lesson)).field("entries", r.entries).field("netPnl", r.net_pnl);
  j.key("calibration").begin_obj().field("resolved", r.resolved_answers).key("brierBp");
  if (r.brier_bp) j.num(*r.brier_bp); else j.null();
  j.end_obj().key("trades").begin_arr();
  for (const TradeRow& t : r.trades) write_trade(j, t);
  j.end_arr();
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
  write_options(j, sc, st);
  write_indicators(j, sc, st.step);
  write_news(j, sc, st.step);
  write_calibration_state(j, sc, st);
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

std::string to_hex_bytes(const std::vector<std::uint8_t>& bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const std::uint8_t b : bytes) {
    out += kHex[b >> 4U];
    out += kHex[b & 0xFU];
  }
  return out;
}

bool from_hex(std::string_view hex, std::vector<std::uint8_t>* out) {
  if (hex.size() % 2 != 0) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  out->clear();
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out->push_back(static_cast<std::uint8_t>(hi * 16 + lo));
  }
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

const char* tl_place_option_order(tl_run* run, uint32_t client_id, uint32_t series, int32_t side, int32_t type,
                                  const char* qty, const char* premium) {
  if ((type != 0 && type != 1) || side < 0 || side > 1) return error_json(*run, "unsupported_order_type");
  Command c = next_command(*run, CommandType::PlaceOptionOrder, client_id);
  c.series = series;
  c.side = static_cast<OrderSide>(side);
  c.order_type = static_cast<OrderType>(type);
  const auto q = parse_decimal<Qty>(qty == nullptr ? "" : qty);
  if (!q.ok()) return error_json(*run, "invalid_quantity");
  c.qty = q.value();
  if (c.order_type == OrderType::Limit && !parse_optional_price(premium, &c.price, true)) {
    return error_json(*run, "invalid_tick");
  }
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_adjust_margin(tl_run* run, uint32_t client_id, const char* amount) {
  Command c = next_command(*run, CommandType::AdjustMargin, client_id);
  const auto m = parse_decimal<Money>(amount == nullptr ? "" : amount);
  if (!m.ok()) return error_json(*run, "invalid_quantity");
  c.margin_delta = m.value();
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_record_plan(tl_run* run, uint32_t client_id, int32_t side, int32_t skipped, int32_t option,
                           int32_t has_lose_scenario, const char* invalidation, const char* target,
                           int32_t confidence_pct, const char* planned_risk) {
  Command c = next_command(*run, CommandType::RecordPlan, client_id);
  c.plan.side = side == 1 ? OrderSide::Sell : OrderSide::Buy;
  c.plan.skipped = skipped != 0;
  c.plan.option = option != 0;
  c.plan.has_lose_scenario = has_lose_scenario != 0;
  c.plan.confidence_pct = static_cast<std::uint32_t>(std::max(0, confidence_pct));
  const auto risk = parse_decimal<Money>(planned_risk == nullptr || planned_risk[0] == '\0' ? "0" : planned_risk);
  if (!risk.ok() || !parse_optional_price(invalidation, &c.plan.invalidation, false) ||
      !parse_optional_price(target, &c.plan.target, false)) {
    return error_json(*run, "invalid_plan");
  }
  c.plan.planned_risk = risk.value();
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_answer_question(tl_run* run, uint32_t client_id, int32_t p_bp, int32_t skipped) {
  Command c = next_command(*run, CommandType::AnswerQuestion, client_id);
  c.p_bp = static_cast<std::uint32_t>(std::max(0, p_bp));
  c.skipped = skipped != 0;
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_checkpoint(tl_run* run) {
  JsonOut j;
  j.begin_obj().field("checkpoint", to_hex_bytes(save_checkpoint(run->state, run->scenario.id)));
  j.field("step", run->state.step).end_obj();
  run->buffer = j.text();
  return run->buffer.c_str();
}

tl_run* tl_run_restore(const char* mission_id, uint32_t seed, const char* checkpoint_hex) {
  std::vector<std::uint8_t> bytes;
  if (checkpoint_hex == nullptr || !from_hex(checkpoint_hex, &bytes)) return nullptr;
  tl_run* run = tl_run_create(mission_id, seed);
  if (run == nullptr) return nullptr;
  auto st = load_checkpoint(bytes, run->scenario.id);
  if (!st.ok()) {
    delete run;
    return nullptr;
  }
  run->state = st.value();
  run->next_seq = run->state.last_command_seq + 1;
  return run;
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
  write_score(j, score_run(sc, run->state));
  j.end_obj();
  run->buffer = j.text();
  return run->buffer.c_str();
}

}  // extern "C"
