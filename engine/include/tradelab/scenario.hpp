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
};

enum class ScenarioError : std::uint8_t {
  Ok = 0,
  TooFewBars,
  InvalidBar,
  InvalidScale,
  InvalidContract,
  InvalidMission,
  FundingLengthMismatch,
};

ScenarioError validate(const Scenario& s);

constexpr Rate rate_one() { return Rate::from_raw(Rate::kScale); }

}  // namespace tradelab
