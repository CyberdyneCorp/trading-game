#include "gaps.hpp"

#include <algorithm>

namespace tradelab::detail {

bool skips_gaps(const Scenario& sc, std::uint32_t last) {
  if (sc.mission.gap_policy != GapPolicy::Skip) return false;
  const auto end = sc.bars.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(last + 1ULL, sc.bars.size()));
  return std::any_of(sc.bars.begin(), end, [](const Bar& b) { return b.continuity; });
}

TradingBars trading_bars(const Scenario& sc, std::uint32_t last) {
  TradingBars t;
  t.sc.contract = sc.contract;
  t.sc.mission = sc.mission;
  const std::uint32_t end = std::min<std::uint32_t>(last + 1, static_cast<std::uint32_t>(sc.bars.size()));
  for (std::uint32_t i = 0; i < end; ++i) {
    if (sc.bars[i].continuity) continue;
    t.sc.bars.push_back(sc.bars[i]);
    t.index.push_back(i);
  }
  return t;
}

}  // namespace tradelab::detail
