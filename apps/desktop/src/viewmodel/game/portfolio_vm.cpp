#include "viewmodel/game/portfolio_vm.hpp"

#include <array>

#include "viewmodel/format.hpp"
#include "viewmodel/report_texts.hpp"

namespace tradelab::desktop::game {

namespace {

std::string row_name(const Texts& t, const WhatIfLeg& l) {
  if (l.kind == "perp") return t.tr("lab.perp");
  return std::string(l.kind == "call" ? "C " : "P ") + fmt::fmt_price(l.strike.value_or("0"), t.locale()) + " @" + std::to_string(l.expiry_step);
}

std::string bp_text(const Texts& t, int bp) { return t.tr("portfolio.bp", {{"bp", fmt::fmt_num(bp, t.locale(), 0)}}); }

std::string kind_name(const Texts& t, const std::string& kind) { return t.tr("portfolio.limit." + kind); }

void totals(const Texts& t, const WhatIfRisk& r, PortfolioView& v) {
  const Locale L = t.locale();
  v.totals.emplace_back(t.tr("portfolio.deltaUnits"), fmt::fmt_num(fmt::num(r.delta_units), L, 4));
  v.totals.emplace_back(t.tr("portfolio.deltaNotional"), fmt::fmt_money(r.delta_notional, L, true) + " \xC2\xB7 " + bp_text(t, r.delta_bp));
  v.totals.emplace_back(t.tr("portfolio.gamma1pct"), fmt::fmt_money(r.gamma_1pct, L) + " \xC2\xB7 " + bp_text(t, r.gamma_bp));
  v.totals.emplace_back(t.tr("portfolio.vegaPoint"), fmt::fmt_money(r.vega_point, L, true) + " \xC2\xB7 " + bp_text(t, r.vega_bp));
  v.totals.emplace_back(t.tr("portfolio.thetaBar"), fmt::fmt_money(r.theta_bar, L, true) + " \xC2\xB7 " + bp_text(t, r.theta_bp));
  v.totals.emplace_back(t.tr("portfolio.fundingNext"), fmt::fmt_money(r.funding_next, L, true) + " \xC2\xB7 " + bp_text(t, r.funding_bp));
}

void limits(const Texts& t, const GameState& g, const std::optional<WhatIfRisk>& risk, PortfolioView& v) {
  const Locale L = t.locale();
  v.limits_title = t.tr("portfolio.limits");
  if (declared_limits(g.rules).empty()) {
    v.no_limits = t.tr("portfolio.noLimits");
    return;
  }
  if (!risk) return;
  for (const auto& l : risk->limits) {
    v.limits.emplace_back(t.tr("portfolio.limitRow", {{"kind", kind_name(t, l.kind)},
                                                      {"value", fmt::fmt_num(l.value_bp, L, 0)},
                                                      {"limit", fmt::fmt_num(l.limit_bp, L, 0)}}),
                          l.exceeded);
  }
  for (const auto& w : exceeded_limits(risk)) {
    v.warnings.push_back(t.tr("portfolio.exceeded", {{"kind", kind_name(t, w.kind)},
                                                     {"value", fmt::fmt_num(w.value_bp, L, 0)},
                                                     {"limit", fmt::fmt_num(w.limit_bp, L, 0)}}));
  }
}

}  // namespace

PortfolioView portfolio_view(const Texts& t, const GameState& g, const std::optional<WhatIfResult>& r, bool busy) {
  const Locale L = t.locale();
  PortfolioView v;
  v.title = t.tr("portfolio.title");
  if (!r) {
    v.empty = t.tr("portfolio.empty");
    limits(t, g, std::nullopt, v);
    return v;
  }
  const auto rows = portfolio_rows(*r);
  if (rows.empty()) v.empty = t.tr("portfolio.empty");
  v.header = {t.tr("portfolio.position"), t.tr("portfolio.qty"), t.tr("portfolio.value"), t.tr("portfolio.pnl"),
              "\xCE\x94", "\xCE\x93", "\xCE\x98", "\xCE\xBD"};
  for (const auto& row : rows) {
    const WhatIfLeg& l = row.leg;
    v.rows.push_back({row_name(t, l), fmt::fmt_qty(js_number(row.qty), L), fmt::fmt_money(l.position_value, L),
                      fmt::fmt_money(l.pnl, L, true), fmt::fmt_num(fmt::num(l.delta), L, 3), fmt::fmt_num(fmt::num(l.gamma), L, 4),
                      fmt::fmt_num(fmt::num(l.theta), L, 3), fmt::fmt_num(fmt::num(l.vega), L, 3)});
    v.row_tones.push_back(fmt::num(l.pnl) >= 0 ? Tone::Up : Tone::Down);
  }
  if (!rows.empty()) v.per_unit = t.tr("portfolio.perUnit");
  if (r->risk) totals(t, *r->risk, v);
  limits(t, g, r->risk, v);
  const auto hedge = hedge_ticket(g, r->risk);
  v.hedge_label = t.tr("portfolio.hedge");
  v.hedge_note = t.tr("portfolio.hedge") + ": " +
                 (hedge ? t.tr("portfolio.hedgeNote", {{"qty", fmt::fmt_qty(hedge->qty, L)}, {"side", t.tr(hedge->buy ? "side.buy" : "side.sell")}})
                        : t.tr("portfolio.hedgeNone"));
  v.can_hedge = hedge.has_value() && !busy && g.active();
  return v;
}

RailPortfolio rail_portfolio(const Texts& t, const std::optional<WhatIfResult>& r) {
  const Locale L = t.locale();
  RailPortfolio v;
  v.title = t.tr("portfolio.title");
  v.open = t.tr("portfolio.open");
  if (!r || !r->risk) return v;
  v.totals.emplace_back(t.tr("portfolio.deltaNotional"), fmt::fmt_money(r->risk->delta_notional, L, true));
  v.totals.emplace_back(t.tr("portfolio.fundingNext"), fmt::fmt_money(r->risk->funding_next, L, true));
  for (const auto& w : exceeded_limits(r->risk)) {
    v.warnings.push_back(t.tr("rail.riskWarn", {{"kind", kind_name(t, w.kind)},
                                                {"value", fmt::fmt_num(w.value_bp, L, 0)},
                                                {"limit", fmt::fmt_num(w.limit_bp, L, 0)}}));
  }
  return v;
}

std::string brief_risk_limits(const Texts& t, const MissionRules& rules) {
  std::string out;
  for (const auto& d : declared_limits(rules)) {
    out += (out.empty() ? "" : " \xC2\xB7 ") + t.tr("brief.riskLimit." + d.kind + "Bp", {{"n", fmt::fmt_num(d.bp, t.locale(), 0)}});
  }
  return out;
}

HeatmapView heatmap_view(const Texts& t, const WhatIfHeatmap& m, bool table, std::size_t focus_t, std::size_t focus_p) {
  const Locale L = t.locale();
  HeatmapView v;
  v.title = t.tr("heatmap.title");
  v.caption = t.tr("heatmap.caption");
  v.legend = t.tr("heatmap.legend");
  v.table = table;
  v.toggle = t.tr(table ? "heatmap.grid" : "heatmap.table");
  for (const auto& p : m.prices) v.prices.push_back(fmt::fmt_price(p, L));
  for (const int b : m.times) v.times.push_back(std::to_string(b));
  const double scale = heatmap_scale(m);
  for (const auto& row : m.pnl) {
    std::vector<HeatmapCell> cells;
    std::vector<std::string> texts;
    for (const auto& c : row) {
      cells.push_back(heatmap_cell(fmt::num(c), scale));
      texts.push_back(fmt::fmt_money(c, L, true));
    }
    v.cells.push_back(std::move(cells));
    v.texts.push_back(std::move(texts));
  }
  if (focus_t < m.times.size() && focus_p < m.prices.size() && focus_t < m.pnl.size() && focus_p < m.pnl[focus_t].size()) {
    const auto c = heatmap_cell_text(m, focus_t, focus_p, L);
    v.focus = t.tr("heatmap.focus",
                   {{"text", t.tr("heatmap.cell", {{"price", c.price}, {"bars", std::to_string(c.bars)}, {"pnl", c.pnl}})}});
  }
  return v;
}

namespace {

constexpr float kAxisLeft = 28;
constexpr float kAxisBottom = 14;

}  // namespace

// One row of cells: the background, then the gain or loss ink at the cell's share.
void heatmap_row(ChartScene& s, const std::vector<HeatmapCell>& cells, float y, float cw, float ch) {
  for (std::size_t c = 0; c < cells.size(); ++c) {
    const float x = kAxisLeft + static_cast<float>(c) * cw;
    s.rects.push_back({x, y, cw, ch, Ink::Bg, 1, false});
    s.rects.push_back({x, y, cw, ch, cells[c].up ? Ink::Up : Ink::Down, static_cast<float>(cells[c].share) / 100.0F, false});
  }
}

// The first, middle and last prices under the grid.
void heatmap_prices(ChartScene& s, const HeatmapView& v, float height, float cw) {
  const std::size_t cols = v.prices.size();
  const std::array<std::pair<std::size_t, Anchor>, 3> at = {{{0, Anchor::Left}, {cols / 2, Anchor::Center}, {cols - 1, Anchor::Right}}};
  const std::array<float, 3> offset = {0.0F, 0.5F, 1.0F};
  for (std::size_t i = 0; i < at.size(); ++i) {
    const float x = kAxisLeft + (static_cast<float>(at[i].first) + offset[i]) * cw;
    s.texts.push_back({x, height - kAxisBottom + 2, v.prices.at(at[i].first), Ink::Muted, 10, at[i].second});
  }
}

ChartScene heatmap_scene(const HeatmapView& v, float width, float height, std::size_t focus_t, std::size_t focus_p) {
  ChartScene s;
  const std::size_t rows = v.cells.size();
  const std::size_t cols = rows ? v.cells.front().size() : 0;
  if (rows == 0 || cols == 0 || v.prices.size() != cols) return s;
  s.empty = false;
  const float cw = (width - kAxisLeft) / static_cast<float>(cols);
  const float ch = (height - kAxisBottom) / static_cast<float>(rows);
  for (std::size_t r = 0; r < rows; ++r) {
    const float y = static_cast<float>(r) * ch;
    s.texts.push_back({0, y + ch / 2 - 5, v.times.at(r), Ink::Muted, 10, Anchor::Left});
    heatmap_row(s, v.cells[r], y, cw, ch);
  }
  if (focus_t < rows && focus_p < cols) {
    s.rects.push_back({kAxisLeft + static_cast<float>(focus_p) * cw, static_cast<float>(focus_t) * ch, cw, ch, Ink::Text, 1, true});
  }
  heatmap_prices(s, v, height, cw);
  return s;
}

std::optional<std::pair<std::size_t, std::size_t>> heatmap_hit(const HeatmapView& v, float width, float height, float x, float y) {
  const std::size_t rows = v.cells.size();
  const std::size_t cols = rows ? v.cells.front().size() : 0;
  if (rows == 0 || cols == 0 || x < kAxisLeft || y < 0 || y >= height - kAxisBottom || x >= width) return std::nullopt;
  const auto c = static_cast<std::size_t>((x - kAxisLeft) / ((width - kAxisLeft) / static_cast<float>(cols)));
  const auto r = static_cast<std::size_t>(y / ((height - kAxisBottom) / static_cast<float>(rows)));
  if (r >= rows || c >= cols) return std::nullopt;
  return std::pair{r, c};
}

}  // namespace tradelab::desktop::game
