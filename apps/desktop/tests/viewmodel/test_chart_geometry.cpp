#include <doctest/doctest.h>

#include "viewmodel/chart_geometry.hpp"

using namespace tradelab::desktop;

TEST_CASE("chart: no closed bar -> empty geometry ('no data')") {
  CHECK(chart_geometry({}, 800, 400).empty);
  CHECK(chart_geometry({Bar{1, 2, 0.5, 1.5, 10}}, 0, 400).empty);
}

TEST_CASE("chart: candles inside the box, highest high near the top, lowest low near the bottom") {
  std::vector<Bar> bars = {{100, 102, 99, 101, 1}, {101, 101.5, 97, 98, 1}, {98, 104, 98, 103, 1}};
  const ChartGeometry g = chart_geometry(bars, 800, 400, 80);
  REQUIRE(g.candles.size() == 3);
  CHECK(g.candles[0].up);
  CHECK_FALSE(g.candles[1].up);
  CHECK(g.price_max > 104);
  CHECK(g.price_min < 97);
  for (const Candle& c : g.candles) {
    CHECK(c.x >= 0);
    CHECK(c.x + c.width <= 800);
    CHECK(c.wick_top >= 0);
    CHECK(c.wick_bottom <= 400);
    CHECK(c.wick_top <= c.body_top);
    CHECK(c.body_bottom <= c.wick_bottom + 1);
    CHECK(c.body_bottom > c.body_top);  // a doji still shows one pixel
  }
  CHECK(g.candles[2].wick_top < g.candles[1].wick_top);      // 104 above 101.5
  CHECK(g.candles[1].wick_bottom > g.candles[0].wick_bottom);  // 97 below 99
  CHECK(g.candles[2].x > g.candles[1].x);                    // time runs left to right, newest at the right
}

TEST_CASE("chart: only the last max_bars bars are drawn") {
  std::vector<Bar> bars;
  for (int i = 0; i < 200; ++i) bars.push_back(Bar{100.0 + i, 101.0 + i, 99.0 + i, 100.5 + i, 1});
  const ChartGeometry g = chart_geometry(bars, 800, 400, 80);
  CHECK(g.candles.size() == 80);
  CHECK(g.price_min < 219.0);
  CHECK(g.price_min > 210.0);  // bars before the window do not stretch the scale
}
