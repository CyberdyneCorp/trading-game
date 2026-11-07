#include "viewmodel/game/tutorial_art.hpp"

#include <algorithm>
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
  void text(float tx, float base, std::string s, Ink ink, Anchor anchor = Anchor::Left) {
    const auto chars = static_cast<float>(std::count_if(s.begin(), s.end(), [](char ch) { return (ch & 0xC0) != 0x80; }));
    const float w = chars * kSmall * 0.6f;
    const float left = anchor == Anchor::Left ? tx : tx - (anchor == Anchor::Right ? w : w / 2);
    scene.texts.push_back({x(left), y(base - kSmall * 0.8f), std::move(s), ink, kSmall * s_, Anchor::Left});
  }
  void box(float bx, float by, float w, float h, Ink ink) { scene.rects.push_back({x(bx), y(by), w * s_, h * s_, ink, 1, true}); }
  // A breakeven marker (the web's r=3 circle).
  void dot(float cx, float cy, Ink ink) { scene.rects.push_back({x(cx - 3), y(cy - 3), 6 * s_, 6 * s_, ink, 1, false}); }

  ChartScene scene;

 private:
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
  c.text(150, 28, tr(t, "cap"), Ink::Up, Anchor::Right);
  c.text(150, 12, "long", Ink::Muted, Anchor::Right);
  c.text(12, 96, "+" + tr(t, "premium"), Ink::Down);
}

// Long + put at expiry: a floor below the strike, the long's open loss (dashed) without it.
void insurance(Canvas& c, const Texts& t) {
  c.line(8, 50, 152, 50, Ink::Muted, 1, true);
  c.line(8, 92, 152, 16, Ink::Muted, 1, true);
  c.path({{8, 70}, {64, 70}, {152, 24}}, Ink::Up, 2);
  c.line(64, 8, 64, 92, Ink::Muted, 1, true);
  c.text(10, 64, tr(t, "floor"), Ink::Attention);
  c.text(68, 96, "strike", Ink::Muted);
  c.text(150, 44, "\xE2\x88\x92" + tr(t, "premium"), Ink::Down, Anchor::Right);
}

// Iron condor (solid, flat wings) against a short strangle (dashed, open on both sides).
void condor(Canvas& c, const Texts& t) {
  c.line(8, 44, 152, 44, Ink::Muted, 1, true);
  c.path({{8, 96}, {52, 30}, {108, 30}, {152, 96}}, Ink::Danger, 1, true);
  c.path({{8, 66}, {30, 66}, {49, 38}, {111, 38}, {130, 66}, {152, 66}}, Ink::Up, 2);
  c.text(80, 24, "+" + tr(t, "premium"), Ink::Up, Anchor::Center);
  c.text(80, 58, tr(t, "condor"), Ink::Attention, Anchor::Center);
  c.text(150, 92, tr(t, "strangle") + " \xC2\xB7 " + tr(t, "open"), Ink::Danger, Anchor::Right);
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
  c.text(72, 34, "2 \xC3\x97 " + tr(t, "spread"), Ink::Attention, Anchor::Center);
  c.text(150, 14, tr(t, "expiry"), Ink::Muted, Anchor::Right);
}

using Painter = void (*)(Canvas&, const Texts&);

Painter painter_of(std::string_view art) {
  if (art == "spread") return spread;
  if (art == "covered") return covered;
  if (art == "insurance") return insurance;
  if (art == "condor") return condor;
  if (art == "roll") return roll;
  return nullptr;
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
