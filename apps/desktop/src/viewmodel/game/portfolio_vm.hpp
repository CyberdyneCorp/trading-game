// Portfolio panel, rail summary and P&L heatmap views (web PortfolioPanel.svelte, RightRail.svelte's
// portfolio section and HeatmapPanel.svelte; add-practice-and-advanced-strategies D4, D9). Every number
// is the engine's holdings what-if (positions, the risk block, the heatmap grid); these functions only
// word and lay it out. Warnings never block an order.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/game_state.hpp"
#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/heatmap.hpp"
#include "viewmodel/game/texts.hpp"
#include "viewmodel/game/whatif_lab.hpp"

namespace tradelab::desktop::game {

struct PortfolioView {
  std::string title;
  std::string empty;                 // "No open positions." when there are none
  std::vector<std::string> header;   // position, qty, value, P&L, Δ, Γ, Θ, ν
  std::vector<std::vector<std::string>> rows;
  std::vector<Tone> row_tones;       // the P&L's sign
  std::string per_unit;
  std::vector<std::pair<std::string, std::string>> totals;
  std::string limits_title;
  std::string no_limits;             // shown when the mission declares none
  std::vector<std::pair<std::string, bool>> limits;  // a row per declared limit, exceeded flag
  std::vector<std::string> warnings;
  std::string hedge_label, hedge_note;
  bool can_hedge = false;
};

PortfolioView portfolio_view(const Texts& t, const GameState& g, const std::optional<WhatIfResult>& r, bool busy);

// The rail's portfolio section: delta in money and the next funding, and a warning per exceeded limit.
struct RailPortfolio {
  std::string title;
  std::vector<std::pair<std::string, std::string>> totals;
  std::vector<std::string> warnings;
  std::string open;
};
RailPortfolio rail_portfolio(const Texts& t, const std::optional<WhatIfResult>& r);

// The briefing's risk-limit line ("Delta em dinheiro ≤ 500 bp · ..."), "" when none is declared.
std::string brief_risk_limits(const Texts& t, const MissionRules& rules);

// The heatmap grid as cells (the painter's display list) and as a table (the screen-reader view).
struct HeatmapView {
  std::string title, caption, legend, toggle;
  bool table = false;  // the table view is shown instead of the map
  std::vector<std::string> prices;       // column heads
  std::vector<std::string> times;        // row heads ("0", "5", ...)
  std::vector<std::vector<HeatmapCell>> cells;
  std::vector<std::vector<std::string>> texts;  // each cell's P&L text
  std::string focus;                     // the focused cell's sentence ("Price 101.5, after 5 bars: +12.30")
};
HeatmapView heatmap_view(const Texts& t, const WhatIfHeatmap& m, bool table, std::size_t focus_t, std::size_t focus_p);

// The map as a display list: a cell per price (columns) and bars (rows), the gain or loss ink at the
// cell's share over the background, the focused cell outlined, the first, middle and last prices under
// it and the bars on the left.
ChartScene heatmap_scene(const HeatmapView& v, float width, float height, std::size_t focus_t, std::size_t focus_p);
// The cell under a point of the map, if any: {row, column}.
std::optional<std::pair<std::size_t, std::size_t>> heatmap_hit(const HeatmapView& v, float width, float height, float x, float y);

}  // namespace tradelab::desktop::game
