#include "view/chart_painter.hpp"

#include <algorithm>

namespace tradelab::desktop {

namespace {

struct Rgb {
  float r, g, b;
};

// terminal-ui tokens (apps/web/src/app.css): --up, --down, --line, --muted.
constexpr Rgb kUp{0x39 / 255.0f, 0xFF / 255.0f, 0x88 / 255.0f};
constexpr Rgb kDown{0xFF / 255.0f, 0x5F / 255.0f, 0x7A / 255.0f};
constexpr Rgb kLine{0x24 / 255.0f, 0x41 / 255.0f, 0x3A / 255.0f};
constexpr Rgb kMuted{0x8F / 255.0f, 0xB3 / 255.0f, 0xA1 / 255.0f};

void grid(RPainter* p, float width, float height) {
  for (int i = 1; i < 4; ++i) {
    const float y = height * static_cast<float>(i) / 4.0f;
    Rdraw_line(p, 0, y, width, y, 1.0f, kLine.r, kLine.g, kLine.b, 1.0f);
  }
}

}  // namespace

void paint_chart(RPainter* painter, const ChartGeometry& g, float width, float height) {
  grid(painter, width, height);
  if (g.empty) return;
  for (const Candle& c : g.candles) {
    const Rgb col = c.up ? kUp : kDown;
    Rdraw_rect(painter, c.wick_x, c.wick_top, 1.0f, std::max(1.0f, c.wick_bottom - c.wick_top), 0, col.r, col.g, col.b,
               1.0f);
    Rdraw_rect(painter, c.x, c.body_top, c.width, c.body_bottom - c.body_top, 0, col.r, col.g, col.b, 1.0f);
  }
  Rdraw_line(painter, 0, g.last_close_y, width, g.last_close_y, 1.0f, kMuted.r, kMuted.g, kMuted.b, 0.6f);
}

}  // namespace tradelab::desktop
