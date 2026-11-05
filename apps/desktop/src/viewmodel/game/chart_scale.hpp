// Port of the web chart's pure helpers: chartScale.ts (price range, pinned levels, tag spreading,
// merged candles), chartDrag.ts (draggable levels, valid sides) and chartA11y.ts (bar table pages,
// candle announcements, keyboard focus). Same names and test vectors (tests/viewmodel/game/test_chart_scale.cpp).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::game {

inline constexpr double kNearFraction = 0.5;
inline constexpr float kLabelH = 16;

struct Range {
  double lo = 0;
  double hi = 0;
};

Range candle_range(const std::vector<GameBar>& bars, std::size_t first);
Range price_range(double lo, double hi, const std::vector<double>& levels);

enum class Pin : unsigned char { None, Above, Below };
Pin pin_of(double price, double lo, double hi);

std::vector<float> spread_labels(const std::vector<float>& ys, float top, float bottom);
bool hits_tag(float y, const std::vector<float>& tag_ys);

struct DrawnCandle {
  double at = 0;  // fractional slot index of its centre
  double o = 0, h = 0, l = 0, c = 0, v = 0;
};
// One candle per pixel column when bars are narrower than a pixel; skip[j] leaves continuity bars out.
std::vector<DrawnCandle> drawn_candles(const std::vector<GameBar>& bars, std::size_t first, double slot_w,
                                       bool skip_continuity);

// ---- chartDrag.ts ----

struct DragTarget {
  bool draft = true;    // a ticket level (SL? / TP?) or a working order
  bool stop = true;     // draft: stop (true) or target (false)
  int order_id = 0;     // working order
  bool operator==(const DragTarget&) const = default;
};

struct DragZone {
  DragTarget target;
  double price = 0;
  float line_y = 0;
  float tag_y = 0;
};

inline constexpr float kLineGrabPx = 5;
bool is_draggable_order(const Order& o);
std::optional<DragTarget> hit_test(const std::vector<DragZone>& zones, float px, float py, float plot_w);
bool exit_on_valid_side(std::string_view side, std::string_view type, double price, double mark);
bool stop_moved_beyond_liquidation(std::string_view side, std::string_view type, double price,
                                   std::optional<double> liquidation);

// ---- chartA11y.ts ----

inline constexpr int kTablePage = 50;
struct BarPage {
  int start = 0;
  int end = 0;
  int page = 0;
  int pages = 1;
};
BarPage bar_page(int total, std::optional<int> page, int size = kTablePage);
std::string candle_text(const Texts& t, int i, const GameBar& b, const std::vector<std::string>& patterns);
std::string chart_summary(const Texts& t, const std::vector<GameBar>& bars);
// Bar index after a navigation key ("ArrowLeft", "ArrowRight", "PageUp", "PageDown", "Home", "End").
std::optional<int> step_focus(std::string_view key, std::optional<int> at, int total, int page = 10);
// O, H, L, C as prices and the volume, in the locale (screen-reader table cells).
std::vector<std::string> candle_cells(const GameBar& b, Locale locale);

}  // namespace tradelab::desktop::game
