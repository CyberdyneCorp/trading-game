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

bool series_valid(const Scenario& s, const OptionSeries& x, std::uint32_t index) {
  const bool ids_ok = x.id == index + 1;  // ids are 1..n in order (stable references in commands)
  const bool steps_ok = x.list_step < x.expiry_step && x.expiry_step < s.bars.size();
  return ids_ok && steps_ok && on_grid(x.strike.raw, s.contract.tick_size.raw);
}

bool options_valid(const Scenario& s) {
  const OptionSpec& o = s.options;
  if (o.series.empty()) return true;
  if (o.multiplier.raw <= 0 || o.premium_tick.raw <= 0 || o.lot_size.raw <= 0 || o.minutes_per_step == 0) return false;
  if (!o.iv_base.empty() && o.iv_base.size() != s.bars.size()) return false;
  for (std::uint32_t i = 0; i < o.series.size(); ++i) {
    if (!series_valid(s, o.series[i], i)) return false;
  }
  return o.im_floor.raw > 0 && o.mm_base.raw > 0 && o.settlement_bars >= 1;
}

bool news_valid(const Scenario& s) {
  for (const NewsItem& n : s.news) {
    if (n.release_step >= s.bars.size() || n.credibility > 100 || n.kind > NewsKind::NetworkUpgrade) return false;
    if (n.announce_lead > n.release_step) return false;
    if (n.expectation < -2 || n.expectation > 2 || n.actual < -2 || n.actual > 2 || n.tone < -1 || n.tone > 1) return false;
  }
  return true;
}

bool liquidity_valid(const Scenario& s) {
  for (const LiquidityShock& l : s.liquidity) {
    const bool span_ok = l.start >= 1 && l.bars >= 1 && l.start < s.bars.size() && l.bars <= s.bars.size() - l.start;
    if (!span_ok || l.spread_pct < 10 || l.spread_pct > 1000 || l.capacity_pct > 1000) return false;
  }
  return true;
}

std::int64_t scaled(std::int64_t raw, std::uint32_t pct, bool up) {
  const std::int64_t num = raw * static_cast<std::int64_t>(pct);  // raw <= 1e10, pct <= 1000: no overflow
  return up ? (num + 99) / 100 : num / 100;
}

}  // namespace

CostModel costs_at(const Scenario& s, std::uint32_t bar) {
  CostModel c = s.costs;
  for (const LiquidityShock& l : s.liquidity) {
    if (bar < l.start || bar - l.start >= l.bars) continue;
    // Capped so overlapping shocks stay finite: a cost never exceeds half the price.
    c.half_spread = Rate::from_raw(std::min(scaled(c.half_spread.raw, l.spread_pct, true), Rate::kScale / 2));
    c.slippage = Rate::from_raw(std::min(scaled(c.slippage.raw, l.spread_pct, true), Rate::kScale / 2));
    c.capacity_fraction = Rate::from_raw(std::min(scaled(c.capacity_fraction.raw, l.capacity_pct, false), Rate::kScale));
  }
  return c;
}

ScenarioError validate(const Scenario& s) {
  if (s.bars.size() < 3) return ScenarioError::TooFewBars;
  if (!contract_valid(s.contract)) return ScenarioError::InvalidContract;
  for (const Bar& b : s.bars) {
    if (!bar_valid(b, s.contract.tick_size)) return ScenarioError::InvalidBar;
  }
  if (s.funding_rate.size() != s.bars.size()) return ScenarioError::FundingLengthMismatch;
  if (!mission_valid(s)) return ScenarioError::InvalidMission;
  if (!options_valid(s)) return ScenarioError::InvalidOptions;
  if (!news_valid(s)) return ScenarioError::InvalidNews;
  if (!liquidity_valid(s)) return ScenarioError::InvalidLiquidity;
  return ScenarioError::Ok;
}

}  // namespace tradelab
