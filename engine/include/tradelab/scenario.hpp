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
};

struct ContractSpec {
  Qty multiplier;          // underlying units per contract (linear perp)
  Price tick_size;
  Qty lot_size;
  Qty min_qty;
  std::int32_t max_leverage = 1;
  Rate maker_fee;          // fraction of notional
  Rate taker_fee;
  Rate maintenance_margin;  // single tier (execution-policy v1)
  std::uint32_t funding_interval = 8;  // in steps
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
  std::uint32_t settlement_bars = 1;  // TWAP window in bars; 1 = expiry close (flagged approximate)
  // Short option margin per contract unit (fractions of the underlying price F):
  // IM = max(im_base - OTM/F, im_floor) * F + ask ; MM = mm_base * F + mark
  Rate im_base;
  Rate im_floor;
  Rate mm_base;
  bool allow_short = false;
};

struct Scenario {
  std::string id;
  std::string alias;  // masked market name shown to the player
  ContractSpec contract;
  CostModel costs;
  MissionRules mission;
  std::vector<Bar> bars;
  // funding_rate[i]: rate charged at the close of bar i when i is a funding boundary.
  // Published (visible) from the close of bar i-1.
  std::vector<Rate> funding_rate;
  OptionSpec options;  // empty series => perpetual-only scenario
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
};

ScenarioError validate(const Scenario& s);

constexpr Rate rate_one() { return Rate::from_raw(Rate::kScale); }

}  // namespace tradelab
