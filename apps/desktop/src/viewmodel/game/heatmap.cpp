#include "viewmodel/game/heatmap.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "model/storage/json_writer.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

std::vector<int> heatmap_times(double max_bars, int rows) {
  const int n = std::max(0, static_cast<int>(std::floor(max_bars)));
  const int k = std::min({rows, kEngineMaxTimes, n + 1});
  if (k <= 1) return {0};
  std::vector<int> out;
  for (int i = 0; i < k; ++i) {
    const int v = static_cast<int>(std::floor(static_cast<double>(i) * n / (k - 1) + 0.5));  // Math.round
    if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
  }
  return out;
}

HeatmapRange heatmap_range(double forward, double tick) {
  const int decimals = fmt::step_decimals(tick);
  const auto snap = [&](double v) { return fmt::to_fixed_text(std::floor(v / tick + 0.5) * tick, decimals); };
  return {snap(forward * 0.85), snap(forward * 1.15)};
}

std::string heatmap_request(const std::vector<AnalysisLeg>& legs, double forward, double tick, double max_bars, bool holdings) {
  const HeatmapRange r = heatmap_range(forward, tick);
  storage::JsonWriter w;
  w.begin_object();
  w.key("legs").raw(legs_json(legs));
  if (holdings) w.field("holdings", true);
  w.key("heatmap").begin_object().field("lo", r.lo).field("hi", r.hi).field("points", kHeatmapPoints);
  w.key("bars").begin_array();
  for (const int b : heatmap_times(max_bars)) w.integer(b);
  w.end_array().end_object();
  return w.end_object().text();
}

double heatmap_scale(const WhatIfHeatmap& m) {
  double top = 1e-9;
  for (const auto& row : m.pnl) {
    for (const auto& v : row) top = std::max(top, std::fabs(fmt::num(v)));
  }
  return top;
}

HeatmapCell heatmap_cell(double pnl, double scale) {
  const double share = std::floor(std::min(1.0, std::fabs(pnl) / scale) * 85 + 0.5);
  return {static_cast<int>(share) + (pnl == 0 ? 0 : 10), pnl >= 0};
}

HeatmapCellText heatmap_cell_text(const WhatIfHeatmap& m, std::size_t t, std::size_t p, Locale locale) {
  return {fmt::fmt_price(m.prices.at(p), locale), m.times.at(t), fmt::fmt_money(m.pnl.at(t).at(p), locale, true)};
}

std::string portfolio_request() { return R"({"holdings":true,"legs":[]})"; }

std::vector<PortfolioRow> portfolio_rows(const WhatIfResult& r) {
  std::vector<PortfolioRow> out;
  for (const auto& leg : r.legs) out.push_back({leg, fmt::num(leg.qty) * (leg.buy ? 1 : -1)});
  return out;
}

std::vector<RiskLimitCheck> exceeded_limits(const std::optional<WhatIfRisk>& risk) {
  std::vector<RiskLimitCheck> out;
  if (!risk) return out;
  std::copy_if(risk->limits.begin(), risk->limits.end(), std::back_inserter(out), [](const RiskLimitCheck& l) { return l.exceeded; });
  return out;
}

std::vector<DeclaredLimit> declared_limits(const MissionRules& rules) {
  std::vector<DeclaredLimit> out;
  for (const auto& [kind, bp] : {std::pair{"delta", rules.risk_delta_bp}, std::pair{"gamma", rules.risk_gamma_bp},
                                 std::pair{"vega", rules.risk_vega_bp}, std::pair{"theta", rules.risk_theta_bp}}) {
    if (bp != 0) out.push_back({kind, bp});
  }
  return out;
}

std::optional<OrderLeg> hedge_ticket(const GameState& g, const std::optional<WhatIfRisk>& risk) {
  if (!risk) return std::nullopt;
  return hedge_order(g, fmt::num(risk->delta_units));
}

double delta_after_hedge(const GameState& g, const WhatIfRisk& risk, const OrderLeg& hedge) {
  const double units = fmt::num(hedge.qty) * g.contract.multiplier * (hedge.buy ? 1 : -1);
  return std::round((fmt::num(risk.delta_units) + units) * 1e8) / 1e8;
}

}  // namespace tradelab::desktop::game
