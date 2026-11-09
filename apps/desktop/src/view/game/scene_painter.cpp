#include "view/game/scene_painter.hpp"

#include <raven/raven_resources.h>

#include <algorithm>
#include <cmath>

namespace tradelab::desktop::view {

namespace {

struct Rgb {
  float r, g, b;
};

constexpr Rgb hex(unsigned v) {
  return {static_cast<float>((v >> 16) & 0xFF) / 255.0f, static_cast<float>((v >> 8) & 0xFF) / 255.0f,
          static_cast<float>(v & 0xFF) / 255.0f};
}

// terminal-ui tokens (apps/web/src/app.css), in game::Ink order.
constexpr Rgb kInk[] = {hex(0x0B1010), hex(0xE3F5EA), hex(0x8FB3A1), hex(0x24413A), hex(0x39FF88), hex(0xFF5F7A),
                        hex(0x4FD8FF), hex(0xFFC857), hex(0x39FF88), hex(0xFF5F7A), hex(0xC792FF), hex(0x7F9CFF)};

// The colorblind palette (web app.css [data-palette='cb']): gains blue, losses and danger orange.
constexpr Rgb kInkColorblind[] = {hex(0x0B1010), hex(0xE3F5EA), hex(0x8FB3A1), hex(0x24413A), hex(0x4FD8FF), hex(0xFFA64D),
                                  hex(0x4FD8FF), hex(0xFFC857), hex(0x39FF88), hex(0xFFA64D), hex(0xC792FF), hex(0x7F9CFF)};

bool g_colorblind = false;

Rgb ink(game::Ink i) { return (g_colorblind ? kInkColorblind : kInk)[static_cast<int>(i)]; }

void dashed(RPainter* p, const game::SceneLine& l, Rgb c) {
  const float dx = l.x1 - l.x0;
  const float dy = l.y1 - l.y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0) return;
  const float period = l.dash + l.gap;
  for (float at = 0; at < len; at += period) {
    const float to = std::min(len, at + l.dash);
    Rdraw_line(p, l.x0 + dx * at / len, l.y0 + dy * at / len, l.x0 + dx * to / len, l.y0 + dy * to / len, l.width, c.r, c.g,
               c.b, l.alpha);
  }
}

void text(RPainter* p, const game::SceneText& t) {
  float x = t.x;
  if (t.anchor != game::Anchor::Left) {
    float w = 0;
    float h = 0;
    Rfont_measure_text(R_INVALID_ID, t.size, t.text.c_str(), &w, &h);
    x -= t.anchor == game::Anchor::Right ? w : w / 2;
  }
  const Rgb c = ink(t.ink);
  Rdraw_text(p, R_INVALID_ID, t.size, x, t.y, t.text.c_str(), c.r, c.g, c.b, 1.0f);
}

}  // namespace

void set_scene_colorblind(bool on) { g_colorblind = on; }

void paint_scene(RPainter* p, const game::ChartScene& s) {
  for (const auto& r : s.rects) {
    const Rgb c = ink(r.ink);
    if (r.outline) Rdraw_rect_outline(p, r.x, r.y, r.w, r.h, 0, 1, c.r, c.g, c.b, r.alpha);
    else Rdraw_rect(p, r.x, r.y, r.w, r.h, 0, c.r, c.g, c.b, r.alpha);
  }
  for (const auto& l : s.lines) {
    const Rgb c = ink(l.ink);
    if (l.dash > 0) dashed(p, l, c);
    else Rdraw_line(p, l.x0, l.y0, l.x1, l.y1, l.width, c.r, c.g, c.b, l.alpha);
  }
  for (const auto& t : s.texts) text(p, t);
}

void paint_payoff(RPainter* p, const game::OptionTicketView& v, float width, float height) {
  if (v.curve.size() < 2 || !(v.hi > v.lo)) return;
  double max_abs = 1e-9;
  for (const auto& pt : v.curve) max_abs = std::max(max_abs, std::fabs(pt.pnl));
  const auto x = [&](double s) { return static_cast<float>((s - v.lo) / (v.hi - v.lo)) * width; };
  const auto y = [&](double pnl) { return height / 2 - static_cast<float>(pnl / max_abs) * (height / 2 - 6); };
  const Rgb line = ink(game::Ink::Line);
  const Rgb mark = ink(game::Ink::Muted);
  const Rgb be = ink(game::Ink::Attention);
  Rdraw_line(p, 0, y(0), width, y(0), 1, line.r, line.g, line.b, 1);
  Rdraw_line(p, x(v.mark), 0, x(v.mark), height, 1, mark.r, mark.g, mark.b, 0.8f);
  Rdraw_line(p, x(v.breakeven_price), 0, x(v.breakeven_price), height, 1, be.r, be.g, be.b, 0.8f);
  for (std::size_t i = 1; i < v.curve.size(); ++i) {
    const Rgb c = ink(v.curve[i].pnl >= 0 ? game::Ink::Up : game::Ink::Down);
    Rdraw_line(p, x(v.curve[i - 1].s), y(v.curve[i - 1].pnl), x(v.curve[i].s), y(v.curve[i].pnl), 2, c.r, c.g, c.b, 1);
  }
}

}  // namespace tradelab::desktop::view
