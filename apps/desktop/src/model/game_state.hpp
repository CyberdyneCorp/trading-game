// Typed view of the engine's tl_state JSON for the trading terminal (engine IDL `GameState`, the same
// shape the web reads in apps/web/src/lib/engine/types.ts). Amounts are read as doubles for display
// and pre-trade estimates only; every value sent back to the engine is a decimal string built by the
// viewmodels (format.hpp), never one of these doubles round-tripped as state.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/json.hpp"

namespace tradelab::desktop {

struct MarginTier {
  double floor = 0;
  double mmr = 0;
  int max_leverage = 0;
};

struct Contract {
  double tick = 0.01;
  double lot = 0.1;
  double min_qty = 0.1;
  double multiplier = 1;
  int max_leverage = 1;
  int leverage_cap = 1;
  double taker_fee = 0;
  double maintenance_margin = 0;
  bool allow_short = true;
  double half_spread = 0;
  double slippage = 0;
  double capacity_fraction = 0;
  std::vector<MarginTier> tiers;
  std::string gap_policy;  // "skip" | "carryForward" | ""
  bool liquidity_shocks = false;
};

struct Objective {
  std::string code;
  int value_bp = 0;
};

struct MissionRules {
  int leverage_cap = 1;
  bool allow_short = true;
  bool options_buy = false;
  bool options_write = false;
  std::vector<Objective> objectives;
  int liq_warning_bp = 300;
  int drawdown_budget_bp = 0;
  bool confirm_stop_beyond_liq = false;
  bool spread_margin = false;  // defined-risk margin for paired shorts (add-academy-and-strategies D5)
};

struct GameBar {
  double open = 0, high = 0, low = 0, close = 0, volume = 0;
  bool synthetic = false;
  bool continuity = false;  // policy v17: the market did not trade
};

struct Account {
  double wallet = 0, equity = 0, unrealized = 0, position_margin = 0, available = 0, notional = 0, mark = 0;
  std::optional<double> liquidation;
  std::string liquidation_text;  // the engine's decimal, for the confirmation text
  double next_funding_rate = 0;
  int next_funding_step = 0;
};

struct Position {
  double qty = 0;  // signed
  std::string qty_text;
  double entry = 0;
  double margin = 0;
  std::string entry_text;  // the engine's decimal (what-if legs valued from the entry)
};

struct Order {
  int id = 0;
  std::string type;  // market | limit | stop_market | take_profit | trailing_stop
  std::string side;  // buy | sell
  double qty = 0;
  double filled = 0;
  std::string price_text;
  double price = 0;
  bool reduce_only = false;
  std::string status;
  int parent_id = 0;
  double trail_rate = 0;
  bool bracket = false;
  int series = 0;
};

struct EngineEvent {
  int seq = 0;
  int step = 0;
  std::string type;
  int order_id = 0;
  std::string order_type;  // "" when the event has none
  std::string status;
  std::string reason;
  std::string side;
  std::string qty, price, requested, amount, pnl;
  bool ambiguous = false, gap = false, capacity_limited = false, approximate = false;
  int series = 0;
};

struct Totals {
  double realized_pnl = 0, fees_paid = 0, funding_net = 0;
  int fills = 0;
};

struct OptionQuote {
  int id = 0;
  std::string kind;  // call | put
  double strike = 0;
  std::string strike_text;
  int expiry_step = 0;
  double bid = 0, ask = 0, iv = 0, delta = 0, gamma = 0, theta = 0, vega = 0;
  double mid = 0;  // the model value on the premium tick (the what-if at zero shift rounds to it)
  bool settling = false;
  bool listed = true;  // false: shown for its open positions only, no new orders
};

struct OptionPosition {
  int series = 0;
  double qty = 0;
  double avg_premium = 0;
  std::string qty_text;  // the engine's decimals (what-if legs from positions)
  std::string avg_premium_text;
};

struct Options {
  bool enabled = false;
  bool allow_short = false;
  double multiplier = 1;
  double premium_tick = 0.01;
  double lot = 0.1;
  double fee_rate = 0;
  int settlement_minutes = 30;
  double long_value = 0, short_value = 0, initial_margin = 0, maintenance_margin = 0;
  std::vector<OptionQuote> chain;
  std::vector<OptionPosition> positions;
};

struct PatternHit {
  int step = 0;
  std::string id;
  int bars = 1;
  int bias = 0;
};

// Indicator series, one entry per bar; nullopt while warming up (or at a skipped continuity bar).
using Series = std::vector<std::optional<double>>;

struct Indicators {
  Series sma20, ema50, rsi14, atr14, vwap, bb_upper, bb_lower, macd, macd_signal, macd_hist, plus_di14, minus_di14,
      adx14;
  std::vector<PatternHit> patterns;
};

struct News {
  int step = 0;
  std::string kind;
  int tone = 0;
  int credibility = 0;
  int expectation = 0;
  int actual = 0;
  bool early = false;
};

struct Upcoming {
  int step = 0;
  std::string kind;
  int expectation = 0;
};

struct Calibration {
  bool due = false;
  int horizon = 5;
  int every = 0;
  int answered = 0;
};

struct AdviceReason {
  std::string code;
  int vote = 0;
  int ref = 0;
};

struct Advice {
  int step = 0;
  std::string stance;  // short | wait | long
  int confidence_bp = 0;
  std::string stop;
  std::string size;
  std::vector<AdviceReason> reasons;
};

struct Advisor {
  int allowance = 0;
  int left = 0;
  int news_lead = 0;
  bool calendar = false;
  std::vector<Advice> advice;
};

struct Liquidity {
  double half_spread = 0, slippage = 0, capacity_fraction = 0;
};

struct LessonQuestion {
  std::string id;
  bool numeric = false;
  int choices = 0;
  int points = 0;
};

struct LessonInfo {
  std::string mission;
  bool reviewed = false;
  std::vector<LessonQuestion> questions;
};

struct GameState {
  std::string mission;
  std::string alias;
  std::string status;
  std::string market_mode;
  bool known_scenario = false;
  int step_seconds = 3600;
  int step = 0;
  int last_decision_step = 0;
  int warmup_bars = 0;
  int total_bars = 0;
  double initial_balance = 0;
  int leverage = 1;
  Contract contract;
  MissionRules rules;
  std::vector<GameBar> bars;
  Account account;
  Position position;
  std::vector<Order> orders;
  Options options;
  Indicators indicators;
  std::vector<News> news;
  std::vector<int> calendar;
  std::optional<Liquidity> liquidity;
  std::vector<Upcoming> upcoming;
  Calibration calibration;
  Advisor advisor;
  std::vector<EngineEvent> events;
  Totals totals;
  // The mission's lesson questions (tl_lesson, without the answer key); empty for a mission without any.
  LessonInfo lesson;

  [[nodiscard]] bool active() const { return status == "active"; }
  // The last traded price, the close of the current bar (the web's lastPrice). The engine reserves
  // margin, checks brackets, triggers stops and prices options against it; account.mark equals it
  // unless the market has a mark-price series (pack schema 2), where the mark serves only margin,
  // liquidation and funding. Order estimates and side checks use this, never the mark.
  [[nodiscard]] double last_price() const { return bars.empty() ? account.mark : bars.back().close; }
};

GameState game_state_from_json(const json::Value& state);
LessonInfo lesson_info_from_json(const json::Value& lesson);

}  // namespace tradelab::desktop
