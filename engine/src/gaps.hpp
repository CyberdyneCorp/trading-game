#pragma once

// Continuity bars and the scenario gap policy (spec indicators "Gaps in data", policy v17). Internal.

#include <cstdint>
#include <vector>

#include "tradelab/scenario.hpp"

namespace tradelab::detail {

// True when indicators must leave continuity bars out (GapPolicy::Skip) and bars 0..last hold one.
// CarryForward needs no work: a continuity bar is already a flat bar at the previous close.
bool skips_gaps(const Scenario& sc, std::uint32_t last);

// Bars 0..last without the continuity bars (contract and mission copied, nothing else), and the
// original index of each kept bar.
struct TradingBars {
  Scenario sc;
  std::vector<std::uint32_t> index;
};
TradingBars trading_bars(const Scenario& sc, std::uint32_t last);

}  // namespace tradelab::detail
