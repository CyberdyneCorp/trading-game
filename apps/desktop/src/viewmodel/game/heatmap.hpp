// P&L heatmap and portfolio view models (ports of the web's heatmap.ts and portfolio.ts,
// add-practice-and-advanced-strategies D4, D9). Every cell, total and limit value is the engine's
// (tl_run_option_whatif's `heatmap` and `risk`); these helpers only choose the request and how a cell or
// a row is shown. Parity fixture: heatmap.json.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "model/i18n.hpp"
#include "viewmodel/game/strategies.hpp"
#include "viewmodel/game/whatif_lab.hpp"

namespace tradelab::desktop::game {

inline constexpr int kHeatmapPoints = 21;
inline constexpr int kHeatmapMaxRows = 9;
inline constexpr int kEngineMaxPoints = 61;  // the engine's caps (D4): 61 prices x 25 times
inline constexpr int kEngineMaxTimes = 25;

// Bars from now to show: 0, then evenly up to `max_bars` (the last expiry), whole bars, at most `rows`.
std::vector<int> heatmap_times(double max_bars, int rows = kHeatmapMaxRows);

// The price range: the forward ±15%, on the tick.
struct HeatmapRange {
  std::string lo, hi;
};
HeatmapRange heatmap_range(double forward, double tick);

// The run what-if request of a heatmap: the legs (or the holdings), the prices and the times.
std::string heatmap_request(const std::vector<AnalysisLeg>& legs, double forward, double tick, double max_bars,
                            bool holdings = false);

// The largest |P&L| on the grid (the colour scale's end), at least 1e-9.
double heatmap_scale(const WhatIfHeatmap& m);

// A cell's colour on a diverging palette: the gain or the loss ink mixed into the background by
// `share` percent (|P&L| / scale x 85, plus 10 off zero). The colorblind setting swaps the inks.
struct HeatmapCell {
  int share = 0;
  bool up = true;
};
HeatmapCell heatmap_cell(double pnl, double scale);

// The text a cell is read with: price, bars and P&L in the player's locale.
struct HeatmapCellText {
  std::string price;
  int bars = 0;
  std::string pnl;
};
HeatmapCellText heatmap_cell_text(const WhatIfHeatmap& m, std::size_t t, std::size_t p, Locale locale);

// ---- portfolio (D9) ----

// The request the panel and the rail refresh on every state change.
std::string portfolio_request();

struct PortfolioRow {
  WhatIfLeg leg;
  double qty = 0;  // signed contracts, > 0 long
};
std::vector<PortfolioRow> portfolio_rows(const WhatIfResult& r);

// The limits a total is above (warnings, never blocks).
std::vector<RiskLimitCheck> exceeded_limits(const std::optional<WhatIfRisk>& risk);

// The limits the mission declares (for the briefing and a panel without holdings).
struct DeclaredLimit {
  std::string kind;  // delta | gamma | vega | theta
  int bp = 0;
};
std::vector<DeclaredLimit> declared_limits(const MissionRules& rules);

// "Hedge delta": the perpetual order to neutral, -deltaUnits rounded toward zero to the lot.
std::optional<OrderLeg> hedge_ticket(const GameState& g, const std::optional<WhatIfRisk>& risk);
// Delta after the hedge fills (display): the units plus the hedge's units.
double delta_after_hedge(const GameState& g, const WhatIfRisk& risk, const OrderLeg& hedge);

}  // namespace tradelab::desktop::game
