#include "viewmodel/game/tutorial_art.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>
#include <initializer_list>
#include <string>
#include <utility>

namespace tradelab::desktop::game {

namespace {

constexpr float kBoxW = 160;
constexpr float kBoxH = 100;
constexpr float kSmall = 9;  // the web's .small font size, in view-box units

using Pt = std::pair<float, float>;

// The web's SVG primitives on the 160x100 view box, scaled and centred into the widget.
class Canvas {
 public:
  Canvas(float width, float height)
      : s_(std::min(width / kBoxW, height / kBoxH)), ox_((width - kBoxW * s_) / 2), oy_((height - kBoxH * s_) / 2) {
    scene.empty = false;
  }

  // .axis / .strike / .liq / .bracket are dashed 3 2; .payoff and .arrow are solid.
  void line(float x0, float y0, float x1, float y1, Ink ink, float width = 1, bool dashed = false) {
    scene.lines.push_back({x(x0), y(y0), x(x1), y(y1), ink, 1, width * s_, dashed ? 3 * s_ : 0, dashed ? 2 * s_ : 0});
  }
  void path(std::initializer_list<Pt> pts, Ink ink, float width = 1, bool dashed = false) {
    const Pt* prev = nullptr;
    for (const Pt& p : pts) {
      if (prev != nullptr) line(prev->first, prev->second, p.first, p.second, ink, width, dashed);
      prev = &p;
    }
  }
  // An SVG <text>: `base` is its baseline. The anchor is resolved here on the text font's advance (IBM
  // Plex Mono: 0.6 em a character), so the label ends where the web's does whatever font measures it.
  // `size` is the web's .small (9) unless a label is drawn at the art's 12-unit body size.
  void text(float tx, float base, std::string s, Ink ink, Anchor anchor = Anchor::Left, float size = kSmall) {
    const auto chars = static_cast<float>(std::count_if(s.begin(), s.end(), [](char ch) { return (ch & 0xC0) != 0x80; }));
    const float w = chars * size * 0.6f;
    const float left = anchor == Anchor::Left ? tx : tx - (anchor == Anchor::Right ? w : w / 2);
    scene.texts.push_back({x(left), y(base - size * 0.8f), std::move(s), ink, size * s_, Anchor::Left});
  }
  void box(float bx, float by, float w, float h, Ink ink) { scene.rects.push_back({x(bx), y(by), w * s_, h * s_, ink, 1, true}); }
  // A breakeven marker (the web's r=3 circle).
  void dot(float cx, float cy, Ink ink) { scene.rects.push_back({x(cx - 3), y(cy - 3), 6 * s_, 6 * s_, ink, 1, false}); }
  // A filled <rect> (the web's .bar-up / .bar-down at 0.7 opacity, .bar-pause opaque).
  void fill(float bx, float by, float w, float h, Ink ink, float alpha = 1) {
    scene.rects.push_back({x(bx), y(by), w * s_, h * s_, ink, alpha, false});
  }
  // SVG Q and C curves, drawn as polylines of kCurveSteps segments.
  void quad(Pt p0, Pt p1, Pt p2, Ink ink, float width = 1, bool dashed = false) {
    curve([&](float t) { return bezier(t, {p0, p1, p2}); }, ink, width, dashed);
  }
  void cubic(Pt p0, Pt p1, Pt p2, Pt p3, Ink ink, float width = 1, bool dashed = false) {
    curve([&](float t) { return bezier(t, {p0, p1, p2, p3}); }, ink, width, dashed);
  }

  ChartScene scene;

 private:
  static constexpr int kCurveSteps = 24;

  // De Casteljau on 3 or 4 control points.
  static Pt bezier(float t, std::vector<Pt> p) {
    for (std::size_t n = p.size(); n > 1; --n) {
      for (std::size_t i = 0; i + 1 < n; ++i) p[i] = {p[i].first + (p[i + 1].first - p[i].first) * t, p[i].second + (p[i + 1].second - p[i].second) * t};
    }
    return p.front();
  }
  template <class At>
  void curve(At at, Ink ink, float width, bool dashed) {
    Pt prev = at(0);
    for (int i = 1; i <= kCurveSteps; ++i) {
      const Pt next = at(static_cast<float>(i) / kCurveSteps);
      line(prev.first, prev.second, next.first, next.second, ink, width, dashed);
      prev = next;
    }
  }

  [[nodiscard]] float x(float v) const { return ox_ + v * s_; }
  [[nodiscard]] float y(float v) const { return oy_ + v * s_; }
  float s_, ox_, oy_;
};

std::string tr(const Texts& t, std::string_view key) { return t.tr("tut.art." + std::string(key)); }

// Bull call spread at expiry: the debit below the lower strike, a capped gain above the upper one.
void spread(Canvas& c, const Texts& t) {
  c.line(8, 58, 152, 58, Ink::Muted, 1, true);
  c.path({{8, 72}, {62, 72}, {102, 30}, {152, 30}}, Ink::Up, 2);
  c.line(62, 8, 62, 92, Ink::Muted, 1, true);
  c.line(102, 8, 102, 92, Ink::Muted, 1, true);
  c.dot(75, 58, Ink::Info);
  c.text(75, 52, "BE", Ink::Info, Anchor::Right);
  c.text(150, 24, tr(t, "cap"), Ink::Up, Anchor::Right);
  c.text(10, 84, "\xE2\x88\x92" + tr(t, "premium"), Ink::Down);  // −
  c.text(82, 98, "K1 \xC2\xB7 K2", Ink::Muted, Anchor::Center);
}

// Long + written call at expiry: the long's line (dashed) bent flat at the strike, lifted by the premium.
void covered(Canvas& c, const Texts& t) {
  c.line(8, 56, 152, 56, Ink::Muted, 1, true);
  c.line(8, 88, 152, 14, Ink::Muted, 1, true);
  c.path({{8, 80}, {96, 34}, {152, 34}}, Ink::Up, 2);
  c.line(96, 8, 96, 92, Ink::Muted, 1, true);
  c.text(150, 44, tr(t, "cap"), Ink::Up, Anchor::Right);
  c.text(150, 12, "long", Ink::Muted, Anchor::Right);
  c.text(12, 96, "+" + tr(t, "premium"), Ink::Up);
}

// Long + put at expiry: a floor below the strike, the long's open loss (dashed) without it.
void insurance(Canvas& c, const Texts& t) {
  c.line(8, 50, 152, 50, Ink::Muted, 1, true);
  c.line(8, 92, 152, 16, Ink::Muted, 1, true);
  c.path({{8, 70}, {64, 70}, {152, 24}}, Ink::Up, 2);
  c.line(64, 8, 64, 92, Ink::Muted, 1, true);
  c.text(10, 64, tr(t, "floor"), Ink::Attention);
  c.text(68, 96, "strike", Ink::Muted);
  c.text(150, 64, "\xE2\x88\x92" + tr(t, "premium"), Ink::Down, Anchor::Right);
}

// Iron condor (solid, flat wings) against a short strangle (dashed, open on both sides).
void condor(Canvas& c, const Texts& t) {
  c.line(8, 44, 152, 44, Ink::Muted, 1, true);
  c.path({{8, 96}, {52, 30}, {108, 30}, {152, 96}}, Ink::Danger, 1, true);
  c.path({{8, 66}, {30, 66}, {49, 38}, {111, 38}, {130, 66}, {152, 66}}, Ink::Up, 2);
  c.text(80, 24, "+" + tr(t, "premium"), Ink::Up, Anchor::Center);
  c.text(80, 58, tr(t, "condor"), Ink::Attention, Anchor::Center);
  c.text(80, 92, tr(t, "strangle") + " \xC2\xB7 " + tr(t, "open"), Ink::Danger, Anchor::Center);
  c.line(140, 44, 140, 66, Ink::Attention, 1, true);
}

// A roll: close the near expiry's option, open the next one's — two trades.
void roll(Canvas& c, const Texts& t) {
  c.line(8, 70, 152, 70, Ink::Muted, 1, true);
  c.line(62, 20, 62, 76, Ink::Muted, 1, true);
  c.line(134, 20, 134, 76, Ink::Muted, 1, true);
  c.text(62, 88, tr(t, "near"), Ink::Muted, Anchor::Center);
  c.text(134, 88, tr(t, "next"), Ink::Muted, Anchor::Center);
  c.box(20, 40, 34, 18, Ink::Line);
  c.text(37, 53, "\xE2\x9C\x97", Ink::Danger, Anchor::Center);  // ✗
  c.box(90, 40, 34, 18, Ink::Info);
  c.text(107, 53, "+", Ink::Info, Anchor::Center);
  c.line(56, 49, 84, 49, Ink::Attention, 2);
  c.path({{84, 45}, {90, 49}, {84, 53}, {84, 45}}, Ink::Attention, 1.5f);  // the arrow head
  c.text(98, 34, "2 \xC3\x97 " + tr(t, "spread"), Ink::Attention, Anchor::Center);
  c.text(150, 14, tr(t, "expiry"), Ink::Muted, Anchor::Right);
}

// ---- Wave 2 missions (m18-m25): scalp, carry, parity, crush, calendarSpread, scale, cooldown ----

// [open, high, low, close] on a 0..100 scale (y up), as TutorialArt.svelte's candles snippet.
using Candle = std::array<float, 4>;
constexpr std::array<Candle, 6> kRange = {{{40, 52, 34, 48}, {48, 56, 42, 44}, {44, 50, 36, 38}, {38, 50, 34, 47}, {47, 58, 44, 55}, {55, 60, 46, 49}}};
constexpr std::array<Candle, 7> kTrend = {
    {{20, 30, 16, 28}, {28, 40, 26, 38}, {38, 42, 30, 33}, {33, 48, 31, 46}, {46, 60, 44, 57}, {57, 60, 48, 51}, {51, 70, 50, 68}}};

float cy(float v) { return 92 - v * 0.84f; }

// A rising candle is hollow (up outline), a falling one filled (down), with a 1.5-wide wick.
void candles(Canvas& c, std::span<const Candle> cs, float x0, float step) {
  for (std::size_t i = 0; i < cs.size(); ++i) {
    const auto [o, h, l, cl] = cs[i];
    const float x = x0 + static_cast<float>(i) * step;
    const bool up = cl >= o;
    const Ink ink = up ? Ink::Up : Ink::Down;
    c.line(x, cy(h), x, cy(l), ink, 1.5f);
    const float top = cy(std::max(o, cl));
    const float height = std::max(1.5f, std::abs(o - cl) * 0.84f);
    if (up) {
      c.box(x - 4, top, 8, height, Ink::Up);
    } else {
      c.fill(x - 4, top, 8, height, Ink::Down);
    }
  }
}

// An upward arrow head below a bar (the web's filled .arrow-head), drawn as its outline.
void arrow_up(Canvas& c, float x, float base, Ink ink = Ink::Attention) {
  c.path({{x - 4, base}, {x, base - 8}, {x + 4, base}, {x - 4, base}}, ink, 1.5f);
}

constexpr float kBody = 12;  // the art's own font size (labels without .small)
constexpr std::array<Candle, 7> kSqueeze = {
    {{50, 62, 38, 41}, {41, 55, 36, 52}, {52, 56, 46, 48}, {48, 52, 46, 50}, {50, 52, 48, 49}, {49, 51, 48, 50}, {50, 74, 49, 72}}};

// ---- Cards the wave 2 missions share with earlier ones: straddle, decay, trail, risk, plan, margin,
// funding, volatility ----

// Call + put at one strike: a V that loses at most the premium, with a breakeven on each side.
void straddle(Canvas& c, const Texts& t) {
  c.line(8, 56, 152, 56, Ink::Muted, 1, true);
  c.path({{14, 14}, {80, 78}, {146, 14}}, Ink::Up, 2);
  c.line(80, 8, 80, 92, Ink::Muted, 1, true);
  c.dot(37, 56, Ink::Info);
  c.dot(123, 56, Ink::Info);
  c.text(37, 50, "BE", Ink::Info, Anchor::Center);
  c.text(123, 50, "BE", Ink::Info, Anchor::Center);
  c.text(76, 94, "strike", Ink::Muted, Anchor::Right);
  c.text(90, 76, "\xE2\x88\x92" + tr(t, "premium"), Ink::Down);  // −
  c.text(150, 98, tr(t, "breakeven"), Ink::Muted, Anchor::Right);
}

// An option's time value melts faster as expiry nears.
void decay(Canvas& c, const Texts& t) {
  c.line(10, 88, 150, 88, Ink::Muted, 1, true);
  c.cubic({10, 18}, {80, 26}, {120, 44}, {150, 86}, Ink::Up, 2);
  c.text(150, 16, "\xCE\xB8", Ink::Attention, Anchor::Right);  // θ
  c.text(12, 98, tr(t, "today"), Ink::Muted);
  c.text(150, 98, tr(t, "expiry"), Ink::Muted, Anchor::Right);
}

// A trailing stop ratchets up behind the best close, one bar late, and never moves down.
void trail(Canvas& c, const Texts& /*t*/) {
  candles(c, kTrend, 14, 18);
  c.path({{8, cy(10)}, {32, cy(10)}, {32, cy(18)}, {50, cy(18)}, {50, cy(28)}, {86, cy(28)}, {86, cy(34)}, {104, cy(34)},
          {104, cy(45)}, {140, cy(45)}, {140, cy(56)}, {156, cy(56)}},
         Ink::Attention, 1.5f, true);
  c.text(12, 14, "TRAIL", Ink::Attention);
}

// Entry, a stop 1% of the account away and the distance between them.
void risk(Canvas& c, const Texts& /*t*/) {
  candles(c, std::span(kTrend).first(5), 16, 14);
  c.line(8, cy(46), 152, cy(46), Ink::Info, 1, true);
  c.line(8, cy(26), 152, cy(26), Ink::Attention, 1, true);
  c.text(150, cy(46) - 3, "ENTRY", Ink::Info, Anchor::Right);
  c.text(150, cy(26) - 3, "SL \xC2\xB7 1%", Ink::Attention, Anchor::Right);
  c.line(104, cy(46), 104, cy(26), Ink::Attention, 1, true);
}

// The plan written before the entry: hypothesis, invalidation, confidence.
void plan(Canvas& c, const Texts& t) {
  c.box(40, 10, 80, 80, Ink::Line);
  c.text(50, 32, "\xE2\x9C\x93 " + tr(t, "hypothesis"), Ink::Up);       // ✓
  c.text(50, 52, "\xE2\x9C\x97 " + tr(t, "invalidation"), Ink::Attention);  // ✗
  c.text(50, 72, "% " + tr(t, "confidence"), Ink::Info);
}

// Margin backing the position, and the liquidation line it must stay away from.
void margin(Canvas& c, const Texts& t) {
  c.box(20, 20, 40, 64, Ink::Line);
  c.fill(20, 60, 40, 24, Ink::Up, 0.4f);
  c.text(40, 14, tr(t, "margin"), Ink::Muted, Anchor::Center);
  c.line(70, 62, 150, 62, Ink::Danger, 1, true);
  c.text(150, 56, "LIQ", Ink::Danger, Anchor::Right);
  c.text(40, 54, "+", Ink::Up, Anchor::Center, kBody);
}

// Funding: longs pay shorts (or the other way) every interval.
void funding(Canvas& c, const Texts& /*t*/) {
  c.box(12, 30, 44, 40, Ink::Up);
  c.text(34, 55, "L", Ink::Up, Anchor::Center, kBody);
  c.box(104, 30, 44, 40, Ink::Down);
  c.text(126, 55, "S", Ink::Down, Anchor::Center, kBody);
  c.line(60, 50, 98, 50, Ink::Attention, 2);
  c.path({{98, 50}, {91, 46}, {91, 54}, {98, 50}}, Ink::Attention, 1.5f);
  c.text(80, 42, "+0.01%", Ink::Attention, Anchor::Center);
}

// Bands narrowing into a squeeze before the range breaks.
void volatility(Canvas& c, const Texts& /*t*/) {
  candles(c, kSqueeze, 14, 19);
  c.cubic({8, cy(66)}, {50, cy(60)}, {90, cy(54)}, {128, cy(53)}, Ink::Bb, 1, true);
  c.cubic({8, cy(32)}, {50, cy(40)}, {90, cy(46)}, {128, cy(47)}, Ink::Bb, 1, true);
}

// A long straddle's value is convex in the price: each rehedge (dots) sells after a rise and buys after a fall.
void scalp(Canvas& c, const Texts& t) {
  c.line(8, 88, 152, 88, Ink::Muted, 1, true);
  c.quad({16, 22}, {80, 138}, {144, 22}, Ink::Up, 2);
  c.dot(54, 71, Ink::Info);
  c.dot(80, 80, Ink::Info);
  c.dot(106, 71, Ink::Info);
  c.text(80, 16, "\xC2\xBD\xC2\xB7\xCE\x93\xC2\xB7\xCE\x94S\xC2\xB2", Ink::Attention, Anchor::Center);  // ½·Γ·ΔS²
  c.text(150, 98, tr(t, "hedge"), Ink::Muted, Anchor::Right);
}

// Funding received each interval (bars above the line) against the legs' costs (below).
void carry(Canvas& c, const Texts& t) {
  c.line(8, 56, 152, 56, Ink::Muted, 1, true);
  for (int k = 0; k < 6; ++k) c.fill(14 + static_cast<float>(k) * 14, 40, 9, 16, Ink::Up, 0.7f);
  c.fill(112, 56, 22, 26, Ink::Down, 0.7f);
  c.text(12, 30, tr(t, "carryIn"), Ink::Up);
  c.text(123, 94, tr(t, "costs"), Ink::Down, Anchor::Center);
}

// A bought call (dashed) and a written put (dashed) at one strike add up to a straight long line.
void parity(Canvas& c, const Texts& /*t*/) {
  c.line(8, 56, 152, 56, Ink::Muted, 1, true);
  c.path({{8, 56}, {80, 56}, {152, 20}}, Ink::Muted, 1, true);
  c.path({{8, 92}, {80, 56}, {152, 56}}, Ink::Muted, 1, true);
  c.line(8, 92, 152, 20, Ink::Up, 2);
  c.line(80, 60, 80, 96, Ink::Muted);
  c.text(12, 16, "C \xE2\x88\x92 P = F \xE2\x88\x92 K", Ink::Attention);  // C − P = F − K
  c.text(86, 96, "K", Ink::Muted);
}

// Implied volatility climbs into a known release and drops right after it.
void crush(Canvas& c, const Texts& t) {
  c.line(8, 88, 152, 88, Ink::Muted, 1, true);
  c.path({{8, 70}, {50, 62}, {80, 42}, {102, 24}, {106, 70}, {152, 74}}, Ink::Up, 2);
  c.line(105, 8, 105, 88, Ink::Muted);
  c.text(12, 16, "IV", Ink::Attention);
  c.text(100, 16, tr(t, "release"), Ink::Muted, Anchor::Right);
}

// Two expiries: the near option (sold) loses time value faster than the far one (bought).
void calendar_spread(Canvas& c, const Texts& t) {
  c.line(8, 88, 152, 88, Ink::Muted, 1, true);
  c.cubic({8, 34}, {46, 38}, {68, 54}, {80, 86}, Ink::Danger, 1, true);
  c.cubic({8, 20}, {80, 24}, {124, 38}, {150, 72}, Ink::Up, 2);
  c.line(80, 8, 80, 88, Ink::Muted);
  c.text(80, 98, tr(t, "near"), Ink::Muted, Anchor::Center);
  c.text(150, 98, tr(t, "far"), Ink::Muted, Anchor::Right);
  c.text(12, 52, "\xE2\x88\x92", Ink::Down);  // −
  c.text(122, 22, "+", Ink::Up);
}

// Two entries on a trend, then part of the position taken off higher up.
void scale(Canvas& c, const Texts& /*t*/) {
  candles(c, kTrend, 14, 18);
  arrow_up(c, 14, 90);
  arrow_up(c, 32, 90);
  c.path({{82, 30}, {86, 38}, {90, 30}, {82, 30}}, Ink::Down, 1.5f);  // the partial exit, pointing down
  c.text(22, 99, "+ +", Ink::Info, Anchor::Center);
  c.text(86, 24, "\xC2\xBD", Ink::Attention, Anchor::Center);  // ½
}

// A stop fills, a pause, and only then a fresh setup and a new entry.
void cooldown(Canvas& c, const Texts& t) {
  candles(c, std::span(kRange).first(3), 12, 12);
  c.line(6, 66, 46, 66, Ink::Danger, 1, true);
  c.text(8, 80, "stop", Ink::Danger);
  c.box(62, 34, 30, 26, Ink::Line);
  c.fill(70, 40, 5, 14, Ink::Muted);
  c.fill(79, 40, 5, 14, Ink::Muted);
  c.text(77, 74, tr(t, "wait"), Ink::Muted, Anchor::Center);
  candles(c, std::span(kTrend).subspan(3), 110, 14);
  arrow_up(c, 110, 92);
}

using Painter = void (*)(Canvas&, const Texts&);

constexpr std::array<std::pair<std::string_view, Painter>, 20> kPainters = {{
    {"spread", spread},
    {"covered", covered},
    {"insurance", insurance},
    {"condor", condor},
    {"roll", roll},
    {"straddle", straddle},
    {"decay", decay},
    {"trail", trail},
    {"risk", risk},
    {"plan", plan},
    {"margin", margin},
    {"funding", funding},
    {"volatility", volatility},
    {"scalp", scalp},
    {"carry", carry},
    {"parity", parity},
    {"crush", crush},
    {"calendarSpread", calendar_spread},
    {"scale", scale},
    {"cooldown", cooldown}}};

Painter painter_of(std::string_view art) {
  const auto* it = std::ranges::find(kPainters, art, &std::pair<std::string_view, Painter>::first);
  return it == kPainters.end() ? nullptr : it->second;
}

}  // namespace

bool has_tutorial_art(std::string_view art) { return painter_of(art) != nullptr; }

ChartScene tutorial_art_scene(const Texts& t, std::string_view art, float width, float height) {
  const Painter paint = painter_of(art);
  if (paint == nullptr || width <= 0 || height <= 0) return {};
  Canvas c(width, height);
  paint(c, t);
  return std::move(c.scene);
}

}  // namespace tradelab::desktop::game
