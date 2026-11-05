#pragma once

// Immutable, validated scenario: market data plus contract, cost and mission rules.

#include <cstdint>
#include <string>
#include <vector>

#include "tradelab/fixed.hpp"
#include "tradelab/result.hpp"

namespace tradelab {

struct Bar {
  Price open;
  Price high;
  Price low;
  Price close;
  Qty volume;
  bool synthetic = false;  // generated (event lab / branch) bar
  // Synthetic continuity bar (spec indicators "Gaps in data"): the market did not trade. It repeats
  // the previous close (open = high = low = close, zero volume), has zero executable capacity and is
  // fed to indicators by the scenario's gap policy (policy v17).
  bool continuity = false;
};

// How indicators treat continuity bars (policy v17): Skip leaves them out of every input (no value at
// a continuity bar; VWAP sessions still count real bars); CarryForward feeds them as flat bars at the
// previous close with zero volume.
enum class GapPolicy : std::uint8_t { Skip = 0, CarryForward = 1 };

// Sub-bar closes ending at the close of bar `bar` (oldest first; the last equals the bar's close),
// `minutes` apart. Used for TWAP settlement windows finer than a bar (spec options "Expiry and
// settlement", policy v17).
struct SubBars {
  std::uint32_t bar = 0;
  std::uint32_t minutes = 1;
  std::vector<Price> closes;
};

// Maintenance-margin tier (execution-policy v16): a position whose notional at its average entry is at
// least `notional_floor` keeps `maintenance_margin` and may be held at no more than `max_leverage`.
struct MarginTier {
  Money notional_floor;
  Rate maintenance_margin;
  std::int32_t max_leverage = 1;
};

struct ContractSpec {
  Qty multiplier;          // underlying units per contract (linear perp)
  Price tick_size;
  Qty lot_size;
  Qty min_qty;
  std::int32_t max_leverage = 1;
  Rate maker_fee;          // fraction of notional
  Rate taker_fee;
  Rate maintenance_margin;  // first tier: notional below the first floor (up to max_leverage)
  std::uint32_t funding_interval = 8;  // in steps
  // Higher tiers by notional (policy v16): floors strictly ascending, rates non-decreasing, leverage
  // non-increasing; empty = one tier for every notional.
  std::vector<MarginTier> margin_tiers;
};

struct CostModel {
  Rate half_spread;        // fraction of price
  Rate slippage;           // fraction of price, market/stop fills
  Rate capacity_fraction;  // share of bar volume the player can trade per step
};

struct MissionRules {
  Money initial_balance;
  std::int32_t leverage_cap = 1;
  std::uint32_t warmup_bars = 0;  // bars visible before the first decision point
  bool allow_short = true;
  GapPolicy gap_policy = GapPolicy::Skip;  // continuity bars in indicators (policy v17)
};

enum class OptionKind : std::uint8_t { Call = 0, Put = 1 };

// European, cash-settled option on the perpetual price (spec: options).
struct OptionSeries {
  std::uint32_t id = 0;  // >= 1; unique within the scenario
  OptionKind kind = OptionKind::Call;
  Price strike;
  std::uint32_t list_step = 0;    // tradable from this decision step
  std::uint32_t expiry_step = 0;  // settles at the close of this bar
};

struct OptionSpec {
  std::vector<OptionSeries> series;
  Qty multiplier;       // underlying units per contract
  Price premium_tick;
  Qty lot_size;
  Rate fee_rate;        // fraction of underlying notional, capped at 12.5% of the premium
  Rate spread;          // half-spread as a fraction of the model mid (min one tick)
  // Implied volatility: iv = base[t] * (1 + term_slope * (sqrt(T_ref / T) - 1)) + skew * ln(K/F) + smile * ln(K/F)^2
  std::vector<Vol> iv_base;  // per bar, annualized; empty => realized volatility of past closes
  Vol skew;
  Vol smile;
  Vol term_slope;
  std::uint32_t minutes_per_step = 60;
  // Settlement window in market time ending at the expiry close (policy v17): TWAP of sub-bar closes
  // when the scenario has them for the expiry bar, else the mean of the closes of the bars covering
  // the window (the expiry close when the window is shorter than a bar), flagged approximate. Orders
  // in the series that would execute inside the window are rejected with `series_settling`.
  std::uint32_t settlement_minutes = 30;
  // Short option margin per contract unit (fractions of the underlying price F):
  // IM = max(im_base - OTM/F, im_floor) * F + ask ; MM = mm_base * F + mark
  Rate im_base;
  Rate im_floor;
  Rate mm_base;
  bool allow_short = false;
};

enum class NewsKind : std::uint8_t {
  RateDecision = 0,
  RegulationRumor = 1,
  ExchangeIncident = 2,
  NetworkOutage = 3,
  LiquidityInflow = 4,
  InstitutionalAdoption = 5,
  RumorDenied = 6,
  LiquidityOutflow = 7,  // phase-2 flows (design D8): appended, existing values keep their numbers
  MacroData = 8,         // phase-2 scheduled releases: a fictional economic print
  NetworkUpgrade = 9,
};

// What the player can see about a news item once released (spec: market-generator "Events").
// The market's reaction is not part of the scenario data: it is baked into the generated bars.
struct NewsItem {
  std::uint32_t release_step = 0;  // visible from this decision step (published at its close)
  NewsKind kind = NewsKind::RateDecision;
  std::int8_t tone = 0;            // headline sentiment shown to the player: -1, 0, +1
  std::uint8_t credibility = 100;  // 0..100
  std::int8_t expectation = 0;     // what the market expected: -2..2 (shown)
  std::int8_t actual = 0;          // what was announced: -2..2 (shown); surprise = actual - expectation
  // G3 (design D8.1): announced this many bars ahead. From the close of release_step - announce_lead
  // the state lists it as upcoming with kind, release step and expectation only. Visibility only:
  // the bars never depend on it. 0 = unannounced.
  std::uint32_t announce_lead = 0;
};

// Event spread and capacity channels (spec market-generator "Events"; generator v5): from bar
// `start` for `bars` bars, the half-spread and slippage are scaled by spread_pct / 100 and the
// capacity fraction by capacity_pct / 100 (100 = the cost model as declared). Execution reads the
// costs of the bar it fills on (costs_at); the bars themselves never depend on it.
struct LiquidityShock {
  std::uint32_t start = 0;
  std::uint32_t bars = 0;
  std::uint32_t spread_pct = 100;    // 10..1000
  std::uint32_t capacity_pct = 100;  // 0..1000
};

struct Scenario {
  std::string id;
  std::string alias;  // masked market name shown to the player
  ContractSpec contract;
  CostModel costs;
  MissionRules mission;
  std::vector<Bar> bars;
  // Mark-price bars (policy v17), one per bar: unrealized P&L, margin, liquidation and funding use
  // them, fills use `bars`. Empty = the scenario is flagged markFromLastPrice and both use `bars`.
  std::vector<Bar> mark;
  std::vector<SubBars> sub_bars;  // finer closes around option expiries; may be empty
  // funding_rate[i]: rate charged at the close of bar i when i is a funding boundary.
  // Published (visible) from the close of bar i-1.
  std::vector<Rate> funding_rate;
  OptionSpec options;  // empty series => perpetual-only scenario
  std::vector<NewsItem> news;
  std::vector<LiquidityShock> liquidity;  // empty => the cost model on every bar
};

enum class ScenarioError : std::uint8_t {
  Ok = 0,
  TooFewBars,
  InvalidBar,
  InvalidScale,
  InvalidContract,
  InvalidMission,
  FundingLengthMismatch,
  InvalidOptions,
  InvalidNews,
  InvalidLiquidity,
  InvalidMark,
  InvalidContinuity,
  InvalidSubBars,
};

ScenarioError validate(const Scenario& s);

// The cost model in force on `bar`: the declared costs with every liquidity shock covering that bar
// applied (spread and slippage rounded up, capacity rounded down). Equal to s.costs without shocks.
CostModel costs_at(const Scenario& s, std::uint32_t bar);

constexpr Rate rate_one() { return Rate::from_raw(Rate::kScale); }

// True when the scenario has no mark series and marks positions at the last price.
inline bool mark_from_last_price(const Scenario& s) { return s.mark.empty(); }

// The bar positions are marked against at index i: the mark bar, or the traded bar without one.
inline const Bar& mark_bar(const Scenario& s, std::uint32_t i) { return s.mark.empty() ? s.bars[i] : s.mark[i]; }

}  // namespace tradelab
