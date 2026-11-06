// Candle chart geometry computed from closed bars, in logical units of the chart box. The view only
// paints these rectangles and lines (no math in the painter). Minimal first cut of the web chart
// (chartScale.ts): last `max_bars` bars, price range padded by 5%, last-close line.
#pragma once

#include <string>
#include <vector>

#include "model/snapshot.hpp"

namespace tradelab::desktop {

struct Candle {
  float x = 0;  // left edge of the body
  float width = 0;
  float body_top = 0;
  float body_bottom = 0;
  float wick_x = 0;
  float wick_top = 0;
  float wick_bottom = 0;
  bool up = true;  // close >= open
};

struct ChartGeometry {
  std::vector<Candle> candles;
  double price_min = 0;
  double price_max = 0;
  float last_close_y = 0;
  bool empty = true;  // no closed bar yet ("no data")

  [[nodiscard]] float y_of(double price, float height) const;
};

ChartGeometry chart_geometry(const std::vector<Bar>& bars, float width, float height, int max_bars = 80);

}  // namespace tradelab::desktop
