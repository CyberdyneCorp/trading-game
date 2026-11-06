#include "model/game_state.hpp"

namespace tradelab::desktop {

namespace {

using json::Value;

int to_int(const Value& v, int fallback = 0) { return static_cast<int>(v.as_int(fallback)); }
double dbl(const Value& v) { return v.as_double(0.0); }

Series series_from(const Value& list) {
  Series out;
  out.reserve(list.size());
  for (const auto& v : list.items()) {
    if (v.is_null()) out.emplace_back(std::nullopt);
    else out.emplace_back(v.as_double());
  }
  return out;
}

Contract contract_from(const Value& c) {
  Contract k;
  k.tick = c["tickSize"].as_double(0.01);
  k.lot = c["lotSize"].as_double(0.1);
  k.min_qty = c["minQty"].as_double(0.1);
  k.multiplier = c["multiplier"].as_double(1);
  k.max_leverage = to_int(c["maxLeverage"], 1);
  k.leverage_cap = to_int(c["leverageCap"], 1);
  k.taker_fee = dbl(c["takerFee"]);
  k.maintenance_margin = dbl(c["maintenanceMargin"]);
  k.allow_short = c["allowShort"].as_bool(true);
  k.half_spread = dbl(c["halfSpread"]);
  k.slippage = dbl(c["slippage"]);
  k.capacity_fraction = dbl(c["capacityFraction"]);
  for (const auto& t : c["marginTiers"].items()) {
    k.tiers.push_back({dbl(t["notionalFloor"]), dbl(t["maintenanceMargin"]), to_int(t["maxLeverage"])});
  }
  k.gap_policy = c["gapPolicy"].as_string();
  k.liquidity_shocks = c["liquidityShocks"].as_bool(false);
  return k;
}

MissionRules rules_from(const Value& r) {
  MissionRules m;
  m.leverage_cap = to_int(r["leverageCap"], 1);
  m.allow_short = r["permissions"]["short"].as_bool(true);
  m.options_buy = r["permissions"]["optionsBuy"].as_bool(false);
  m.options_write = r["permissions"]["optionsWrite"].as_bool(false);
  for (const auto& o : r["objectives"].items()) m.objectives.push_back({o["code"].as_string(), to_int(o["valueBp"])});
  m.liq_warning_bp = to_int(r["liqWarningBp"], 300);
  m.drawdown_budget_bp = to_int(r["drawdownBudgetBp"]);
  m.confirm_stop_beyond_liq = r["confirmStopBeyondLiq"].as_bool(false);
  m.spread_margin = r["spreadMargin"].as_bool(false);
  return m;
}

std::vector<GameBar> bars_from(const Value& bars) {
  std::vector<GameBar> out;
  out.reserve(bars.size());
  for (const auto& b : bars.items()) {
    out.push_back({dbl(b[0]), dbl(b[1]), dbl(b[2]), dbl(b[3]), dbl(b[4]), b[5].as_bool(false), b[6].as_bool(false)});
  }
  return out;
}

Account account_from(const Value& a) {
  Account out;
  out.wallet = dbl(a["wallet"]);
  out.equity = dbl(a["equity"]);
  out.unrealized = dbl(a["unrealized"]);
  out.position_margin = dbl(a["positionMargin"]);
  out.available = dbl(a["available"]);
  out.notional = dbl(a["notional"]);
  out.mark = dbl(a["mark"]);
  if (!a["liquidationPrice"].is_null()) {
    out.liquidation = dbl(a["liquidationPrice"]);
    out.liquidation_text = a["liquidationPrice"].as_string();
  }
  out.next_funding_rate = dbl(a["nextFundingRate"]);
  out.next_funding_step = to_int(a["nextFundingStep"]);
  return out;
}

Order order_from(const Value& o) {
  Order out;
  out.id = to_int(o["id"]);
  out.type = o["type"].as_string();
  out.side = o["side"].as_string();
  out.qty = dbl(o["qty"]);
  out.price_text = o["price"].as_string();
  out.price = dbl(o["price"]);
  out.reduce_only = o["reduceOnly"].as_bool(false);
  out.status = o["status"].as_string();
  out.parent_id = to_int(o["parentId"]);
  out.trail_rate = dbl(o["trailRate"]);
  out.bracket = dbl(o["bracketStop"]) != 0 || dbl(o["bracketTp"]) != 0;
  out.series = to_int(o["series"]);
  return out;
}

EngineEvent event_from(const Value& e) {
  EngineEvent out;
  out.seq = to_int(e["seq"]);
  out.step = to_int(e["step"]);
  out.type = e["type"].as_string();
  out.order_id = to_int(e["orderId"]);
  out.order_type = e["orderType"].as_string();
  out.status = e["status"].as_string();
  out.reason = e["reason"].as_string();
  out.side = e["side"].as_string();
  out.qty = e["qty"].as_string();
  out.price = e["price"].as_string();
  out.requested = e["requested"].as_string();
  out.amount = e["amount"].as_string();
  out.pnl = e["pnl"].as_string();
  out.ambiguous = e["ambiguous"].as_bool(false);
  out.gap = e["gap"].as_bool(false);
  out.capacity_limited = e["capacityLimited"].as_bool(false);
  out.approximate = e["approximate"].as_bool(false);
  out.series = to_int(e["series"]);
  return out;
}

OptionQuote quote_from(const Value& q) {
  OptionQuote out;
  out.id = to_int(q["id"]);
  out.kind = q["kind"].as_string();
  out.strike = dbl(q["strike"]);
  out.strike_text = q["strike"].as_string();
  out.expiry_step = to_int(q["expiryStep"]);
  out.bid = dbl(q["bid"]);
  out.ask = dbl(q["ask"]);
  out.mid = dbl(q["mid"]);
  out.iv = dbl(q["iv"]);
  out.delta = dbl(q["delta"]);
  out.gamma = dbl(q["gamma"]);
  out.theta = dbl(q["theta"]);
  out.vega = dbl(q["vega"]);
  out.settling = q["settling"].as_bool(false);
  out.listed = q["listed"].as_bool(true);
  return out;
}

Options options_from(const Value& o) {
  Options out;
  out.enabled = o["enabled"].as_bool(false);
  out.allow_short = o["allowShort"].as_bool(false);
  out.multiplier = o["multiplier"].as_double(1);
  out.premium_tick = o["premiumTick"].as_double(0.01);
  out.lot = o["lotSize"].as_double(0.1);
  out.fee_rate = dbl(o["feeRate"]);
  out.settlement_minutes = to_int(o["settlementMinutes"], 30);
  out.long_value = dbl(o["longValue"]);
  out.short_value = dbl(o["shortValue"]);
  out.initial_margin = dbl(o["initialMargin"]);
  out.maintenance_margin = dbl(o["maintenanceMargin"]);
  for (const auto& q : o["chain"].items()) out.chain.push_back(quote_from(q));
  for (const auto& p : o["positions"].items()) {
    out.positions.push_back(
        {to_int(p["series"]), dbl(p["qty"]), dbl(p["avgPremium"]), p["qty"].as_string("0"), p["avgPremium"].as_string("0")});
  }
  return out;
}

Indicators indicators_from(const Value& i) {
  Indicators out;
  out.sma20 = series_from(i["sma20"]);
  out.ema50 = series_from(i["ema50"]);
  out.rsi14 = series_from(i["rsi14"]);
  out.atr14 = series_from(i["atr14"]);
  out.vwap = series_from(i["vwap"]);
  out.bb_upper = series_from(i["bbUpper"]);
  out.bb_lower = series_from(i["bbLower"]);
  out.macd = series_from(i["macd"]);
  out.macd_signal = series_from(i["macdSignal"]);
  out.macd_hist = series_from(i["macdHist"]);
  out.plus_di14 = series_from(i["plusDi14"]);
  out.minus_di14 = series_from(i["minusDi14"]);
  out.adx14 = series_from(i["adx14"]);
  for (const auto& p : i["patterns"].items()) {
    out.patterns.push_back({to_int(p["step"]), p["id"].as_string(), to_int(p["bars"], 1), to_int(p["bias"])});
  }
  return out;
}

void read_news(GameState& s, const Value& state) {
  for (const auto& n : state["news"].items()) {
    s.news.push_back({to_int(n["step"]), n["kind"].as_string(), to_int(n["tone"]), to_int(n["credibility"]),
                      to_int(n["expectation"]), to_int(n["actual"]), n["early"].as_bool(false)});
  }
  for (const auto& c : state["calendar"].items()) s.calendar.push_back(to_int(c));
  for (const auto& u : state["upcoming"].items()) {
    s.upcoming.push_back({to_int(u["step"]), u["kind"].as_string(), to_int(u["expectation"])});
  }
  const Value& liq = state["liquidity"];
  if (!liq.is_null() && liq.has("halfSpread")) {
    s.liquidity = Liquidity{dbl(liq["halfSpread"]), dbl(liq["slippage"]), dbl(liq["capacityFraction"])};
  }
}

Advisor advisor_from(const Value& a) {
  Advisor out;
  out.allowance = to_int(a["allowance"]);
  out.left = to_int(a["left"]);
  out.news_lead = to_int(a["newsLead"]);
  out.calendar = a["calendar"].as_bool(false);
  for (const auto& v : a["advice"].items()) {
    Advice adv{to_int(v["step"]), v["stance"].as_string(), to_int(v["confidenceBp"]), v["stop"].as_string("0"),
               v["size"].as_string("0"), {}};
    for (const auto& r : v["reasons"].items()) adv.reasons.push_back({r["code"].as_string(), to_int(r["vote"]), to_int(r["ref"])});
    out.advice.push_back(std::move(adv));
  }
  return out;
}

void read_header(GameState& s, const Value& state) {
  s.mission = state["mission"].as_string();
  s.alias = state["alias"].as_string();
  s.status = state["status"].as_string();
  s.market_mode = state["marketMode"].as_string("synthetic");
  s.known_scenario = state["knownScenario"].as_bool(false);
  s.step_seconds = to_int(state["stepSeconds"], 3600);
  s.step = to_int(state["step"]);
  s.last_decision_step = to_int(state["lastDecisionStep"]);
  s.warmup_bars = to_int(state["warmupBars"]);
  s.total_bars = to_int(state["totalBars"]);
  s.initial_balance = dbl(state["initialBalance"]);
  s.leverage = to_int(state["leverage"], 1);
}

}  // namespace

GameState game_state_from_json(const Value& state) {
  GameState s;
  read_header(s, state);
  s.contract = contract_from(state["contract"]);
  s.rules = rules_from(state["missionRules"]);
  s.bars = bars_from(state["bars"]);
  s.account = account_from(state["account"]);
  const Value& p = state["position"];
  s.position = {dbl(p["qty"]), p["qty"].as_string("0"), dbl(p["entry"]), dbl(p["margin"]), p["entry"].as_string("0")};
  for (const auto& o : state["orders"].items()) s.orders.push_back(order_from(o));
  s.options = options_from(state["options"]);
  s.indicators = indicators_from(state["indicators"]);
  read_news(s, state);
  const Value& c = state["calibration"];
  s.calibration = {c["due"].as_bool(false), to_int(c["horizon"], 5), to_int(c["every"]), to_int(c["answered"])};
  s.advisor = advisor_from(state["advisor"]);
  for (const auto& e : state["events"].items()) s.events.push_back(event_from(e));
  const Value& t = state["totals"];
  s.totals = {dbl(t["realizedPnl"]), dbl(t["feesPaid"]), dbl(t["fundingNet"]), to_int(t["fills"])};
  return s;
}

LessonInfo lesson_info_from_json(const Value& lesson) {
  LessonInfo out;
  out.mission = lesson["mission"].as_string();
  out.reviewed = lesson["reviewed"].as_bool(false);
  for (const auto& q : lesson["questions"].items()) {
    out.questions.push_back({q["id"].as_string(), q["kind"].as_string() == "numeric", to_int(q["choices"]),
                             to_int(q["points"])});
  }
  return out;
}

}  // namespace tradelab::desktop
