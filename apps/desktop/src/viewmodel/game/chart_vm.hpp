// Candle chart viewmodel (web Chart.svelte): builds a display list (rects, lines, texts in logical
// units of the chart box) from the engine state, the ticket preview and the pointer/keyboard state,
// so the Raven painter only draws it. Only closed bars exist: the area right of the last bar is the
// "unknown future" zone. Interaction (hover, crosshair, drag of SL?/TP? and of working STOP/TP orders,
// pick on chart, wheel zoom, per-candle keyboard focus, bar table) is plain state here, unit-tested
// without a window.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "viewmodel/game/chart_scale.hpp"
#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::game {

// terminal-ui palette tokens (apps/web/src/app.css); the painter maps them to colours.
enum class Ink : std::uint8_t { Bg, Text, Muted, Line, Up, Down, Info, Attention, Accent, Danger, Vwap, Bb };

struct SceneRect {
  float x = 0, y = 0, w = 0, h = 0;
  Ink ink = Ink::Line;
  float alpha = 1;
  bool outline = false;
};

struct SceneLine {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  Ink ink = Ink::Line;
  float alpha = 1;
  float width = 1;
  float dash = 0;  // dash length; 0 = solid
  float gap = 0;
};

enum class Anchor : std::uint8_t { Left, Right, Center };

struct SceneText {
  float x = 0, y = 0;  // y = baseline - size (top of the text)
  std::string text;
  Ink ink = Ink::Text;
  float size = 12;
  Anchor anchor = Anchor::Left;
};

struct ChartScene {
  std::vector<SceneRect> rects;
  std::vector<SceneLine> lines;
  std::vector<SceneText> texts;
  bool empty = true;
};

// Draft levels of the ticket, previewed while the player edits it.
struct TicketPreview {
  double entry = 0;
  std::optional<double> stop;
  std::optional<double> take_profit;
  std::optional<double> liquidation;
};

enum class Oscillator : std::uint8_t { Rsi, Macd, Adx, None };

// What a pointer or key did on the chart, for the terminal to act on.
struct ChartAction {
  enum class Kind : std::uint8_t { None, DraftStop, DraftTarget, MoveOrder, Pattern, Picked, CancelPick };
  Kind kind = Kind::None;
  double price = 0;
  int order_id = 0;
  std::string pattern;
};

class ChartViewModel {
 public:
  static constexpr float kAxisW = 72;
  static constexpr int kFutureSlots = 12;

  // Toggles (web defaults: SMA20 and EMA50 on, VWAP and BB20 off, patterns on, RSI pane).
  bool show_sma = true;
  bool show_ema = true;
  bool show_vwap = false;
  bool show_bb = false;
  bool show_patterns = true;
  Oscillator osc = Oscillator::Rsi;

  // Builds the frame for a box of width x height (also remembered for hit tests and drags).
  ChartScene build(const Texts& t, const GameState& g, const std::optional<TicketPreview>& preview, bool picking,
                   float width, float height);

  ChartAction pointer_move(const GameState& g, float x, float y, bool picking);
  void pointer_down(float x, float y, bool picking);
  ChartAction pointer_up();
  void pointer_leave();
  ChartAction click(const GameState& g, float x, float y, bool picking);
  // Wheel: positive = up (zoom in, fewer bars), as Raven reports it.
  void wheel(const GameState& g, float wheel_y);
  // Escape cancels a drag (a draft line goes back) or a pick; returns what to undo.
  ChartAction escape(bool picking);
  // Per-candle focus: arrows, Page Up/Down, Home/End; Enter opens the pattern on the focused bar.
  ChartAction key(const GameState& g, std::string_view key);
  void focus_gained(const GameState& g);
  void focus_lost();

  [[nodiscard]] int zoom() const { return zoom_; }
  // Shows every bar from `step` on (the debrief: the whole run from its first decision).
  void show_from(const GameState& g, int step);
  [[nodiscard]] std::optional<int> hover_bar(const GameState& g) const;
  [[nodiscard]] std::optional<int> focus_bar() const { return focus_; }
  // What a screen reader announces for the focused candle ("" without focus).
  [[nodiscard]] std::string announcement(const Texts& t, const GameState& g) const;
  [[nodiscard]] bool dragging() const { return drag_.has_value(); }
  [[nodiscard]] bool grabbable() const { return grabbable_; }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

  // Text lines around the canvas.
  [[nodiscard]] std::string readout(const Texts& t, const GameState& g) const;
  [[nodiscard]] std::vector<std::pair<std::string, int>> hover_patterns(const Texts& t, const GameState& g) const;
  [[nodiscard]] std::string status_line(const Texts& t, const GameState& g, bool picking) const;

  // Accessible bar table: pages of 50 closed bars, the last page by default.
  bool table_open = false;
  std::optional<int> table_page;
  [[nodiscard]] BarPage page(const GameState& g) const { return bar_page(static_cast<int>(g.bars.size()), table_page); }
  void page_by(const GameState& g, int delta);

  // Last frame geometry (for tests and the view's hit tests).
  struct Frame {
    double lo = 0, hi = 1;
    float price_h = 1, width = 0, height = 0;
    std::size_t first = 0;
    float slot_w = 1;
    std::vector<DragZone> zones;
  };
  [[nodiscard]] const Frame& frame() const { return frame_; }
  [[nodiscard]] double price_at(float py) const;
  [[nodiscard]] float y_of(double price) const;

 private:
  struct Drag {
    DragTarget target;
    double from = 0;
    double price = 0;
    double lo = 0, hi = 1;
  };

  [[nodiscard]] std::size_t first_bar(const GameState& g) const;
  [[nodiscard]] int slots(const GameState& g) const;
  void focus_candle(const GameState& g, int i);
  ChartAction drag_to(float py);
  void touch() { ++revision_; }

  int zoom_ = 100;
  std::optional<int> hover_;  // visible index
  std::optional<float> hover_y_;
  std::optional<int> focus_;
  std::optional<Drag> drag_;
  bool grabbable_ = false;
  bool suppress_click_ = false;
  Frame frame_;
  std::uint64_t revision_ = 1;
};

}  // namespace tradelab::desktop::game
