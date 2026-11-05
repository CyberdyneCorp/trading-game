#include "viewmodel/chart_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace tradelab::desktop {

float ChartGeometry::y_of(double price, float height) const {
  const double span = price_max - price_min;
  if (span <= 0) return height / 2;
  return static_cast<float>((price_max - price) / span * height);
}

ChartGeometry chart_geometry(const std::vector<Bar>& bars, float width, float height, int max_bars) {
  ChartGeometry g;
  if (bars.empty() || width <= 0 || height <= 0 || max_bars <= 0) return g;
  const std::size_t count = std::min(bars.size(), static_cast<std::size_t>(max_bars));
  const auto first = bars.end() - static_cast<std::ptrdiff_t>(count);

  double lo = first->low;
  double hi = first->high;
  for (auto it = first; it != bars.end(); ++it) {
    lo = std::min(lo, it->low);
    hi = std::max(hi, it->high);
  }
  const double pad = std::max((hi - lo) * 0.05, std::fabs(hi) * 1e-4);
  g.price_min = lo - pad;
  g.price_max = hi + pad;
  g.empty = false;

  const float slot = width / static_cast<float>(max_bars);
  const float body = std::max(1.0f, std::floor(slot * 0.7f));
  float x = width - slot * static_cast<float>(count);
  for (auto it = first; it != bars.end(); ++it, x += slot) {
    Candle c;
    c.up = it->close >= it->open;
    c.x = std::round(x + (slot - body) / 2);
    c.width = body;
    c.body_top = std::round(g.y_of(std::max(it->open, it->close), height));
    c.body_bottom = std::max(c.body_top + 1, std::round(g.y_of(std::min(it->open, it->close), height)));
    c.wick_x = c.x + std::floor(body / 2);
    c.wick_top = std::round(g.y_of(it->high, height));
    c.wick_bottom = std::round(g.y_of(it->low, height));
    g.candles.push_back(c);
  }
  g.last_close_y = std::round(g.y_of(bars.back().close, height));
  return g;
}

}  // namespace tradelab::desktop
