#include "tradelab/c_api.h"

#include <algorithm>
#include <string>

#include "accounting.hpp"
#include "c_api_run.hpp"
#include "json_out.hpp"
#include "tradelab/advisor.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/checkpoint.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/candle_patterns.hpp"
#include "tradelab/indicators.hpp"
#include "tradelab/options.hpp"
#include "tradelab/scoring.hpp"
#include "options_exec.hpp"

using tradelab::detail::JsonOut;


namespace tradelab {
namespace {

constexpr std::uint32_t kMissionStepSeconds = 3600;  // the built-in missions trade hourly bars

// Every maintenance-margin tier, the base one first (floor 0), so clients estimate liquidation with
// the same tier as the engine (policy v16).
void write_margin_tiers(JsonOut& j, const ContractSpec& k) {
  j.key("marginTiers").begin_arr();
  j.begin_obj().field("notionalFloor", Money{}).field("maintenanceMargin", k.maintenance_margin);
  j.field("maxLeverage", k.max_leverage).end_obj();
  for (const MarginTier& t : k.margin_tiers) {
    j.begin_obj().field("notionalFloor", t.notional_floor).field("maintenanceMargin", t.maintenance_margin);
    j.field("maxLeverage", t.max_leverage).end_obj();
  }
  j.end_arr();
}

void write_contract(JsonOut& j, const Scenario& sc, const RunState& st) {
  const ContractSpec& k = sc.contract;
  j.key("contract").begin_obj();
  j.field("tickSize", k.tick_size).field("lotSize", k.lot_size).field("minQty", k.min_qty);
  j.field("multiplier", k.multiplier).field("maxLeverage", k.max_leverage);
  j.field("leverageCap", run_leverage_cap(sc, st));
  j.field("makerFee", k.maker_fee).field("takerFee", k.taker_fee).field("maintenanceMargin", k.maintenance_margin);
  j.field("fundingInterval", k.funding_interval).field("allowShort", sc.mission.allow_short);
  j.field("halfSpread", sc.costs.half_spread).field("slippage", sc.costs.slippage);
  j.field("capacityFraction", sc.costs.capacity_fraction);
  // Policy v17: margin and liquidation read the last price when the scenario has no mark series;
  // indicators treat continuity bars by the gap policy.
  j.field("markFromLastPrice", mark_from_last_price(sc));
  j.field("gapPolicy", json_name(sc.mission.gap_policy));
  write_margin_tiers(j, k);
  j.end_obj();
}

// Only bars 0..step: the future never crosses the ABI.
void write_bars(JsonOut& j, const Scenario& sc, std::uint32_t last) {
  j.key("bars").begin_arr();
  for (std::uint32_t i = 0; i <= last; ++i) {
    const Bar& b = sc.bars[i];
    j.begin_arr().dec(b.open).dec(b.high).dec(b.low).dec(b.close).dec(b.volume).boolean(b.synthetic);
    j.boolean(b.continuity).end_arr();
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
    j.begin_obj().field("id", o.id).field("type", json_name(o.type)).field("side", json_name(o.side));
    j.field("qty", o.qty).field("filled", o.filled).field("price", o.price);
    j.field("reduceOnly", o.reduce_only).field("status", json_name(o.status)).field("placedStep", o.placed_step);
    j.field("parentId", o.parent_id).field("ocoGroup", o.oco_group).field("trailDistance", o.trail_distance);
    j.field("trailRate", o.trail_rate);
    j.field("bracketStop", o.bracket_stop).field("bracketTp", o.bracket_tp).field("series", o.series);
    j.end_obj();
  }
  j.end_arr();
}

const Order* order_by_id(const RunState& st, std::uint32_t id) {
  for (const Order& o : st.orders) {
    if (o.id == id) return &o;
  }
  return nullptr;
}

// orderType names the order an event refers to (null without one): a bracket leg placed and filled
// or cancelled within one advance never shows up in the live order list.
void write_event(JsonOut& j, const RunState& st, const Event& e) {
  j.begin_obj().field("seq", e.seq).field("step", e.step).field("type", json_name(e.type));
  j.field("orderId", e.order_id).key("orderType");
  const Order* o = e.order_id != 0 ? order_by_id(st, e.order_id) : nullptr;
  if (o != nullptr) j.str(json_name(o->type)); else j.null();
  j.field("status", json_name(e.status)).field("reason", json_name(e.reason));
  j.field("side", json_name(e.side)).field("qty", e.qty).field("price", e.price).field("requested", e.requested);
  j.field("amount", e.amount).field("pnl", e.pnl);
  j.field("ambiguous", (e.flags & kFlagAmbiguousIntrabar) != 0).field("gap", (e.flags & kFlagGapFill) != 0);
  j.field("capacityLimited", (e.flags & kFlagCapacityFromBarVolume) != 0).field("series", e.series);
  j.field("approximate", (e.flags & kFlagApproximate) != 0);
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

void write_quote(JsonOut& j, const OptionSeries& s, const OptionQuote& q, bool settling) {
  j.begin_obj().field("id", s.id).field("kind", json_name(s.kind)).field("strike", s.strike);
  j.field("listStep", s.list_step).field("expiryStep", s.expiry_step).field("listed", q.listed);
  j.field("bid", q.bid).field("ask", q.ask).field("mid", q.mid).field("iv", q.iv);
  j.field("delta", q.delta).field("gamma", q.gamma).field("theta", q.theta).field("vega", q.vega);
  j.field("ivFromRealized", q.iv_from_realized).field("settling", settling).end_obj();
}

// Chain at the current decision step only: listed, unexpired series quoted from the current close.
void write_options(JsonOut& j, const Scenario& sc, const RunState& st) {
  const OptionSpec& o = sc.options;
  const Price f = sc.bars[st.step].close;
  const detail::OptionBook b = detail::option_book(sc, st, st.step);
  j.key("options").begin_obj().field("enabled", !o.series.empty()).field("allowShort", o.allow_short);
  j.field("multiplier", o.multiplier).field("premiumTick", o.premium_tick).field("lotSize", o.lot_size);
  j.field("settlementMinutes", o.settlement_minutes);
  j.field("longValue", b.long_value).field("shortValue", b.short_value);
  j.field("initialMargin", b.initial_margin).field("maintenanceMargin", b.maintenance_margin);
  j.key("chain").begin_arr();
  for (const OptionSeries& s : o.series) {
    if (st.step < s.list_step || st.step >= s.expiry_step) continue;
    // An order placed now executes at the next open: inside the settlement window it is refused.
    write_quote(j, s, quote(sc, s, st.step, f), in_settlement_window(sc, s, st.step + 1));
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

// News published by the current step, plus the headlines a fast wire (news_lead) delivers early,
// flagged `early` until their release step. Bars after the step never cross the ABI.
void write_news(JsonOut& j, const Scenario& sc, const RunState& st) {
  const std::uint32_t visible_until = st.step + st.options.news_lead;
  j.key("news").begin_arr();
  for (const NewsItem& n : sc.news) {
    if (n.release_step > visible_until) continue;
    j.begin_obj().field("step", n.release_step).field("kind", json_name(n.kind)).field("tone", std::int32_t{n.tone});
    j.field("credibility", std::uint32_t{n.credibility}).field("expectation", std::int32_t{n.expectation});
    j.field("actual", std::int32_t{n.actual}).field("early", n.release_step > st.step).end_obj();
  }
  j.end_arr();
}

// Announced releases (G3) not visible yet: kind, release step and the consensus only, from the
// close of release_step - announce_lead. Tone, actual and credibility stay hidden until release.
void write_upcoming(JsonOut& j, const Scenario& sc, const RunState& st) {
  const std::uint32_t visible_until = st.step + st.options.news_lead;
  j.key("upcoming").begin_arr();
  for (const NewsItem& n : sc.news) {
    if (n.announce_lead == 0 || n.release_step <= visible_until || st.step + n.announce_lead < n.release_step) continue;
    j.begin_obj().field("step", n.release_step).field("kind", json_name(n.kind));
    j.field("expectation", std::int32_t{n.expectation}).end_obj();
  }
  j.end_arr();
}

// Economic calendar perk: the release step of each scheduled item (rate decisions) not visible yet;
// never its expectation, value or tone. Empty without the perk.
void write_calendar(JsonOut& j, const Scenario& sc, const RunState& st) {
  const std::uint32_t visible_until = st.step + st.options.news_lead;
  j.key("calendar").begin_arr();
  for (const NewsItem& n : sc.news) {
    if (st.options.calendar && n.kind == NewsKind::RateDecision && n.release_step > visible_until) j.num(n.release_step);
  }
  j.end_arr();
}

void write_calibration_state(JsonOut& j, const Scenario& sc, const RunState& st) {
  j.key("calibration").begin_obj().field("due", question_due(sc, st)).field("horizon", kQuestionHorizon);
  j.field("every", kQuestionEvery).field("threshold", sc.bars[st.step].close).field("answered", static_cast<std::uint32_t>(st.answers.size()));
  j.end_obj();
}

void write_advice(JsonOut& j, const Advice& a) {
  j.begin_obj().field("step", a.step).field("stance", json_name(a.stance)).field("confidenceBp", a.confidence_bp);
  j.field("stop", a.stop).field("size", a.size).key("reasons").begin_arr();
  for (const AdviceReason& r : a.reasons) {
    j.begin_obj().field("code", json_name(r.code)).field("vote", std::int32_t{r.vote}).field("ref", r.ref).end_obj();
  }
  j.end_arr().end_obj();
}

// Start options, remaining allowance and every piece of advice received so far.
void write_advisor(JsonOut& j, const RunState& st) {
  j.key("advisor").begin_obj().field("version", kAdvisorVersion).field("allowance", st.options.advice_allowance);
  j.field("left", st.advice_left).field("newsLead", std::uint32_t{st.options.news_lead}).field("calendar", st.options.calendar);
  j.key("advice").begin_arr();
  for (const Advice& a : st.advice) write_advice(j, a);
  j.end_arr().end_obj();
}

void write_advice_rows(JsonOut& j, const ScoreReport& r) {
  j.field("advisorPointsEarned", r.advisor_points_earned).key("advice").begin_arr();
  for (const AdviceRow& a : r.advice) {
    j.begin_obj().field("step", a.step).field("stance", json_name(a.stance)).field("confidenceBp", a.confidence_bp);
    j.field("outcome", json_name(a.outcome)).end_obj();
  }
  j.end_arr().key("adviceHitBp");
  if (r.advice_hit_bp) j.num(*r.advice_hit_bp); else j.null();
}

void write_dim(JsonOut& j, std::string_view name, const Dimension& d) {
  j.key(name).begin_obj().field("weight", d.weight).key("score");
  if (d.score) j.num(*d.score); else j.null();
  j.end_obj();
}

// The regime a trade was entered in (scoring v9; built-in missions only, after the run).
void write_entry_regime(JsonOut& j, const std::vector<RegimeSpan>& spans, std::uint32_t entry_step) {
  const RegimeSpan* span = span_at(spans, entry_step);
  j.key("regime");
  if (span != nullptr) j.str(regime_label_name(span->label)); else j.null();
}

void write_trade(JsonOut& j, const TradeRow& t, const std::vector<RegimeSpan>& spans) {
  j.begin_obj().field("n", t.n).field("series", t.series).field("side", t.is_long ? "long" : "short").field("entryStep", t.entry_step).key("exitStep");
  if (t.exit_step) j.num(*t.exit_step); else j.null();
  j.field("qty", t.qty).field("entryAvg", t.entry_avg).field("exitAvg", t.exit_avg).field("pnl", t.pnl);
  j.field("fees", t.fees).field("funding", t.funding).field("net", t.net).field("bars", t.bars).field("reason", json_name(t.reason));
  if (t.series != 0) {
    j.field("kind", t.is_call ? "call" : "put").field("strike", t.strike).field("expiryStep", t.expiry_step);
  } else {
    j.field("stop", t.stop).field("leverage", t.leverage).field("liqPrice", t.liq_price).field("stopBeyondLiq", t.stop_beyond_liq);
    j.key("safeLeverage");
    if (t.stop_beyond_liq) j.num(t.safe_leverage); else j.null();
  }
  j.field("plannedRisk", t.planned_risk).key("rBp");
  if (t.r_bp) j.num(*t.r_bp); else j.null();
  j.key("planRespected");
  if (t.plan_respected) j.boolean(*t.plan_respected); else j.null();
  write_entry_regime(j, spans, t.entry_step);
  j.end_obj();
}

void write_span(JsonOut& j, const RegimeSpan& s) {
  j.field("start", s.start).field("bars", s.bars).field("label", regime_label_name(s.label));
  j.field("direction", std::int32_t{s.direction});
}

void write_tally(JsonOut& j, std::string_view name, const SignalTally& t) {
  j.key(name).begin_obj().field("hits", t.hits).field("samples", t.samples).end_obj();
}

// Debrief only: the signals-by-regime table of this run (scoring v9).
void write_regime_signals(JsonOut& j, const std::vector<RegimeSignals>& rows) {
  j.key("regimes").begin_arr();
  for (const RegimeSignals& r : rows) {
    j.begin_obj();
    write_span(j, r.span);
    write_tally(j, "rsiFade", r.rsi_fade);
    write_tally(j, "bollingerFade", r.bollinger_fade);
    write_tally(j, "macdFollow", r.macd_follow);
    write_tally(j, "smaFollow", r.sma_follow);
    j.end_obj();
  }
  j.end_arr();
}

void write_score(JsonOut& j, const ScoreReport& r, const std::vector<RegimeSpan>& spans) {
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
    j.begin_obj().field("code", json_name(f.code)).field("tone", json_name(f.tone)).field("step", f.step);
    j.field("count", f.count).field("price", f.price).field("requested", f.requested).field("amount", f.amount).end_obj();
  }
  j.end_arr().field("lesson", json_name(r.lesson)).field("entries", r.entries).field("netPnl", r.net_pnl);
  j.key("calibration").begin_obj().field("resolved", r.resolved_answers).key("brierBp");
  if (r.brier_bp) j.num(*r.brier_bp); else j.null();
  j.end_obj().key("trades").begin_arr();
  for (const TradeRow& t : r.trades) write_trade(j, t, spans);
  j.end_arr();
  write_advice_rows(j, r);
  j.field("spreadSlippage", r.spread_cost).field("writtenOff", r.written_off).key("greeks");
  if (!r.greeks) {
    j.null();
    return;
  }
  const GreekAttribution& g = *r.greeks;
  j.begin_obj().field("delta", g.delta).field("gamma", g.gamma).field("theta", g.theta).field("vega", g.vega);
  j.field("residual", g.residual).field("total", g.total).end_obj();
}

const char* state_json(tl_run& r) {
  const Scenario& sc = r.scenario;
  const RunState& st = r.state;
  JsonOut j;
  j.begin_obj();
  j.field("mission", sc.id).field("alias", sc.alias).field("status", json_name(st.status));
  j.field("marketMode", r.from_pack ? r.pack.mode : std::string("synthetic")).field("knownScenario", r.from_pack);
  // Bar interval: the pack's declared one (an imported CSV can be 15m), else the missions' hourly bars.
  j.field("stepSeconds", r.from_pack ? r.pack.step_seconds : kMissionStepSeconds);
  j.field("step", st.step).field("lastDecisionStep", last_decision_step(sc));
  j.field("warmupBars", sc.mission.warmup_bars).field("totalBars", static_cast<std::uint32_t>(sc.bars.size()));
  j.field("initialBalance", run_initial_balance(sc, st)).field("leverage", st.leverage);
  write_contract(j, sc, st);
  write_bars(j, sc, st.step);
  write_account(j, sc, st);
  write_orders(j, st);
  write_options(j, sc, st);
  write_indicators(j, sc, st.step);
  write_news(j, sc, st);
  write_calendar(j, sc, st);
  write_upcoming(j, sc, st);
  write_calibration_state(j, sc, st);
  write_advisor(j, st);
  j.key("events").begin_arr();
  for (const Event& e : st.events) write_event(j, st, e);
  j.end_arr();
  write_totals(j, st);
  j.field("digest", to_hex(state_digest(st))).field("eventsDigest", to_hex(events_digest(st)));
  j.end_obj();
  r.buffer = j.text();
  return r.buffer.c_str();
}

const char* result_json(tl_run& r, const CommandResult& res) {
  JsonOut j;
  j.begin_obj().field("accepted", res.accepted).field("reason", json_name(res.reason));
  j.field("orderId", res.order_id).key("warning");
  if (res.warning == Reason::None) j.null(); else j.str(json_name(res.warning));
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

// Debrief only (spec scenario-packs "Reveal metadata"): what the masked market really was.
void write_pack_reveal(JsonOut& j, const PackInfo& p) {
  const PackReveal& r = p.reveal;
  j.field("knownScenario", true).field("marketMode", p.mode).field("packDigest", p.digest);
  j.key("reveal").begin_obj().field("asset", r.asset).field("pair", r.pair).field("venue", r.venue);
  j.field("periodFrom", r.period_from).field("periodTo", r.period_to).field("source", r.source);
  j.field("transformation", r.transformation).field("gaps", p.gaps).field("gapPolicy", p.gap_policy).end_obj();
}

}  // namespace

namespace detail {

bool apply_checkpoint(tl_run& run, const char* checkpoint_hex) {
  std::vector<std::uint8_t> bytes;
  if (checkpoint_hex == nullptr || !from_hex(checkpoint_hex, &bytes)) return false;
  auto st = load_checkpoint(bytes, run.scenario.id);
  if (!st.ok()) return false;
  run.state = st.value();
  run.next_seq = run.state.last_command_seq + 1;
  return true;
}

}  // namespace detail
}  // namespace tradelab

using namespace tradelab;

extern "C" {

const char* tl_mission_reveal(const char* mission_id, uint32_t seed) {
  static std::string buffer;
  MissionReveal r;
  if (mission_id == nullptr || !mission_reveal(mission_id, seed, r)) {
    buffer = "{\"error\":\"unknown_mission\"}";
    return buffer.c_str();
  }
  JsonOut j;
  j.begin_obj().field("mission", std::string(mission_id)).field("seed", seed).key("spans").begin_arr();
  for (const RegimeSpan& s : r.spans) {
    j.begin_obj();
    write_span(j, s);
    j.end_obj();
  }
  j.end_arr().key("events").begin_arr();
  for (const EventReveal& e : r.events) {
    j.begin_obj().field("step", e.release_step).field("kind", json_name(e.kind)).field("actual", std::int32_t{e.actual});
    j.field("surprise", std::int32_t{e.surprise}).field("intensityPct", e.intensity_pct).field("reaction", std::int32_t{e.reaction});
    j.field("expectation", std::int32_t{e.expectation}).field("credibility", std::uint32_t{e.credibility});
    j.field("announceLead", e.announce_lead).field("gapBp", e.gap_bp).field("moveBp", e.move_bp).field("impliedBp", e.implied_bp);
    j.end_obj();
  }
  j.end_arr().end_obj();
  buffer = j.text();
  return buffer.c_str();
}

const char* tl_version(void) {
  static const std::string version = std::string("{\"engine\":\"") + kEngineVersion + "\",\"executionPolicy\":" +
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
  return tl_run_create_ex(mission_id, seed, 0, 0, 0, nullptr, 0);
}

tl_run* tl_run_create_ex(const char* mission_id, uint32_t seed, uint32_t advice_allowance, uint32_t news_lead,
                         int32_t calendar, const char* starting_balance, int32_t leverage_cap) {
  if (news_lead > 255 || leverage_cap < 0) return nullptr;
  RunOptions options{advice_allowance, static_cast<std::uint8_t>(news_lead), calendar != 0, Money{}, leverage_cap};
  if (starting_balance != nullptr && *starting_balance != '\0') {
    const auto balance = parse_decimal<Money>(starting_balance);
    if (!balance.ok() || balance.value().raw <= 0) return nullptr;
    options.starting_balance = balance.value();
  }
  auto* run = new tl_run();
  if (mission_id == nullptr || !build_mission(mission_id, seed, run->scenario)) {
    delete run;
    return nullptr;
  }
  run->state = start_run(run->scenario, seed, options);
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

const char* tl_place_trailing_rate(tl_run* run, uint32_t client_id, int32_t side, const char* qty,
                                   int32_t reduce_only, const char* trail_rate) {
  if (side < 0 || side > 1) return error_json(*run, "unsupported_order_type");
  Command c = next_command(*run, CommandType::PlaceOrder, client_id);
  c.order_type = OrderType::TrailingStop;
  c.side = static_cast<OrderSide>(side);
  const auto q = parse_decimal<Qty>(qty == nullptr ? "" : qty);
  if (!q.ok()) return error_json(*run, "invalid_quantity");
  c.qty = q.value();
  const auto r = parse_decimal<Rate>(trail_rate == nullptr ? "" : trail_rate);
  if (!r.ok()) return error_json(*run, "invalid_tick");
  c.trail_rate = r.value();
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

const char* tl_request_advice(tl_run* run, uint32_t client_id) {
  return result_json(*run, apply_command(run->scenario, run->state, next_command(*run, CommandType::RequestAdvice, client_id)));
}

const char* tl_checkpoint(tl_run* run) {
  JsonOut j;
  j.begin_obj().field("checkpoint", to_hex_bytes(save_checkpoint(run->state, run->scenario.id)));
  j.field("step", run->state.step).end_obj();
  run->buffer = j.text();
  return run->buffer.c_str();
}

tl_run* tl_run_restore(const char* mission_id, uint32_t seed, const char* checkpoint_hex) {
  tl_run* run = tl_run_create(mission_id, seed);
  if (run == nullptr || detail::apply_checkpoint(*run, checkpoint_hex)) return run;
  delete run;
  return nullptr;
}

const char* tl_cancel_order(tl_run* run, uint32_t client_id, uint32_t order_id) {
  Command c = next_command(*run, CommandType::CancelOrder, client_id);
  c.order_id = order_id;
  return result_json(*run, apply_command(run->scenario, run->state, c));
}

const char* tl_move_order(tl_run* run, uint32_t client_id, uint32_t order_id, const char* price) {
  Command c = next_command(*run, CommandType::MoveOrder, client_id);
  c.order_id = order_id;
  if (!parse_optional_price(price, &c.price, true)) return error_json(*run, "invalid_tick");
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
  j.field("status", json_name(run->state.status));
  j.field("finalEquity", account_view(sc, run->state).equity);
  j.field("buyAndHoldPnl", buy_and_hold_pnl(sc, run->state)).field("flatPnl", Money{});
  j.field("syntheticMarket", !run->from_pack).field("seed", run->state.seed);
  MissionReveal reveal;
  if (run->from_pack) write_pack_reveal(j, run->pack);
  else if (mission_reveal(sc.id, run->state.seed, reveal)) write_regime_signals(j, signals_by_regime(sc, run->state, reveal.spans));
  j.field("startPrice", sc.bars[sc.mission.warmup_bars].close).field("endPrice", sc.bars[run->state.step].close);
  write_score(j, score_run(sc, run->state), reveal.spans);
  j.end_obj();
  run->buffer = j.text();
  return run->buffer.c_str();
}

}  // extern "C"
