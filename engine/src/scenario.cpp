#include "tradelab/scenario.hpp"

#include <algorithm>

namespace tradelab {
namespace {

bool on_grid(std::int64_t v, std::int64_t step) { return step > 0 && v % step == 0; }

bool bar_valid(const Bar& b, Price tick) {
  const Price lo = std::min(b.open, b.close);
  const Price hi = std::max(b.open, b.close);
  if (b.low.raw <= 0 || b.low > lo || b.high < hi || b.volume.raw < 0) return false;
  return on_grid(b.open.raw, tick.raw) && on_grid(b.high.raw, tick.raw) && on_grid(b.low.raw, tick.raw) &&
         on_grid(b.close.raw, tick.raw);
}

bool contract_valid(const ContractSpec& c) {
  if (c.tick_size.raw <= 0 || c.lot_size.raw <= 0 || c.multiplier.raw <= 0) return false;
  if (c.min_qty.raw < c.lot_size.raw || !on_grid(c.min_qty.raw, c.lot_size.raw)) return false;
  if (c.max_leverage < 1 || c.max_leverage > 125 || c.funding_interval == 0) return false;
  const bool fees_ok = c.maker_fee.raw >= 0 && c.taker_fee.raw >= 0 && c.taker_fee < rate_one();
  return fees_ok && c.maintenance_margin.raw > 0 && c.maintenance_margin < rate_one();
}

bool mission_valid(const Scenario& s) {
  const MissionRules& m = s.mission;
  if (m.initial_balance.raw <= 0 || m.leverage_cap < 1) return false;
  return m.warmup_bars + 2 <= s.bars.size();
}

}  // namespace

ScenarioError validate(const Scenario& s) {
  if (s.bars.size() < 3) return ScenarioError::TooFewBars;
  if (!contract_valid(s.contract)) return ScenarioError::InvalidContract;
  for (const Bar& b : s.bars) {
    if (!bar_valid(b, s.contract.tick_size)) return ScenarioError::InvalidBar;
  }
  if (s.funding_rate.size() != s.bars.size()) return ScenarioError::FundingLengthMismatch;
  if (!mission_valid(s)) return ScenarioError::InvalidMission;
  return ScenarioError::Ok;
}

}  // namespace tradelab
