#pragma once

// Deterministic synthetic market generator (spec: market-generator, minimal v1).
// Regime segments with volatility clustering and jumps; volume tied to |return|;
// funding follows recent drift. Same spec + seed => identical bars on every target.

#include <cstdint>
#include <string>
#include <vector>

#include "tradelab/scenario.hpp"

namespace tradelab {

struct Segment {
  std::uint32_t bars = 0;
  double drift = 0.0;        // mean log-return per bar
  double vol = 0.01;         // base log-return stdev per bar
  double jump_prob = 0.0;    // probability of a jump per bar
  double jump_mean = 0.0;    // signed mean jump size (log)
  double volume_mult = 1.0;
  double funding_bias = 0.0;  // added to the funding rate in this segment
  double iv_mult = 1.0;       // implied-vol multiplier in this segment (event premium / crush)
};

struct GeneratorSpec {
  double start_price = 100.0;
  double base_volume = 5000.0;
  double funding_base = 0.0001;   // per funding boundary
  double funding_sensitivity = 0.5;
  double iv_premium = 0.10;      // implied over realized
  double bars_per_year = 8760.0;  // hourly bars
  std::vector<Segment> segments;
};

// Fills scenario.bars and scenario.funding_rate (contract.tick_size must be set).
void generate_market(const GeneratorSpec& spec, std::uint64_t seed, Scenario& out);

struct MissionInfo {
  std::string id;
  std::uint32_t total_bars = 0;
  std::uint32_t warmup_bars = 0;
  std::int32_t leverage_cap = 1;
  bool allow_short = true;
};

std::vector<MissionInfo> mission_catalog();
// Builds a validated built-in synthetic mission; returns false for an unknown id.
bool build_mission(const std::string& id, std::uint64_t seed, Scenario& out);

}  // namespace tradelab
