// Scripted terminal flows driven by injected input (clicks, keys, typed text, drags on the chart), the
// desktop counterpart of the web's Playwright game specs. Shared by the headless tests (Raven null
// platform) and the windowed tests (Vulkan, with captures).
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <string>

#include "support/driver.hpp"
#include "view/app.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/dock_layout.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::testing {

namespace gn = names::game;

class Terminal {
 public:
  using Capture = std::function<void(const std::string&)>;
  Terminal(App& app, Driver& d, Capture capture = {}) : app_(app), d_(d), capture_(std::move(capture)) {}

  game::GameViewModel& vm() { return app_.vm().game(); }
  const GameState& g() { return *vm().state(); }
  void shot(const std::string& name) {
    if (capture_) capture_(name);
  }

  // From the menu to the terminal of the mission at `index`, answering nothing yet.
  void open(int index) {
    app_.vm().select(index);
    d_.frames(2);
    REQUIRE(d_.click(names::kMenuStart));
    REQUIRE(d_.to_briefing());
    REQUIRE(d_.click(names::kBriefGo));
    REQUIRE(app_.vm().screen() == Screen::Game);
    d_.frames(3);
  }

  // A calibration question shows over the terminal whenever one is due: skip it as a player would.
  void skip_question() {
    if (vm().modal() != game::Modal::Calibration) return;
    REQUIRE(d_.click(gn::kCalSkip));
    REQUIRE(d_.idle());
  }

  void advance() {
    skip_question();
    d_.focus(names::kGameAdvance);  // off any text field: Space advances
    d_.key(R_KEY_SPACE);
    REQUIRE(d_.idle());
    skip_question();
  }

  void type(std::string_view field, std::string_view text) { d_.type_into(field, text); }

  // Submits the ticket; an entry opens the plan, saved with a thesis typed in the dialog.
  void submit_with_plan(std::string_view thesis = "") {
    REQUIRE(d_.click(gn::kSubmit));
    REQUIRE(vm().modal() == game::Modal::Plan);
    if (thesis.empty()) {
      REQUIRE(d_.click(gn::kPlanSkip));
    } else {
      type(gn::kPlanThesis, thesis);
      REQUIRE(d_.click(gn::kPlanSave));
    }
    REQUIRE(d_.idle());
  }

  // Drags a level of the chart (found in the last frame's drag zones) by `dy` pixels.
  bool drag_level(const std::function<bool(const game::DragZone&)>& which, float dy) {
    const auto& zones = vm().chart.frame().zones;
    const auto z = std::find_if(zones.begin(), zones.end(), which);
    if (z == zones.end()) return false;
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(d_.widget(names::kGameChart), &x, &y, &w, &h);
    d_.drag(x + 60, y + z->line_y, x + 60, y + z->line_y + dy);
    return true;
  }

  // Drags a level of the chart to `price`, aimed as a player aims: at the line as it is drawn now. The
  // line is read after the hover and the target after the press, from the frame drawn with the drag's
  // frozen price range (the chart keeps its rect when grabbed: flow_grab_keeps_chart). A level under kAimPx from its target first
  // gets a taller chart (enlarge_chart): in the hosted macOS runners' 1024x645 window the price pane is
  // a few pixels tall and no drag there can land on a chosen price.
  bool drag_level_to(const std::function<bool(const game::DragZone&)>& which, double price) {
    float x = 0;
    auto line = hover_level(which, x);
    if (line && std::abs(chart_y(price) - *line) < kAimPx) {
      enlarge_chart();
      line = hover_level(which, x);
    }
    if (!line) return false;
    const Rid window = d_.window();
    Rwindow_inject_mouse_button(window, R_MOUSE_LEFT, R_TRUE);
    d_.settle(d_.widget(names::kGameChart));
    REQUIRE_MESSAGE(vm().chart.dragging(), "the press at y " << *line << " did not grab the level");
    const float to = chart_y(price);
    REQUIRE_MESSAGE(std::abs(to - *line) >= kAimPx, "the chart is too short to aim the drag: " << std::abs(to - *line) << " px");
    aim_price_per_pixel_ = price_per_pixel();
    for (int i = 1; i <= 6; ++i) {
      Rwindow_inject_mouse_move(window, x + 60, *line + (to - *line) * static_cast<float>(i) / 6);
      d_.frames(1);
    }
    Rwindow_inject_mouse_button(window, R_MOUSE_LEFT, R_FALSE);
    d_.frames(2);
    return true;
  }

  // The price of one pixel when the last drag_level_to was aimed (the scale fits the levels again after
  // the drop).
  [[nodiscard]] double aim_price_per_pixel() const { return aim_price_per_pixel_; }

  // The pointer rests on a level's line, followed when the layout moves it; its window y once the chart
  // settled (none without the level).
  std::optional<float> hover_level(const std::function<bool(const game::DragZone&)>& which, float& x) {
    auto line = level_y(which, x);
    for (int i = 0; i < 3 && line; ++i) {
      Rwindow_inject_mouse_move(d_.window(), x + 60, *line);
      d_.settle(d_.widget(names::kGameChart));
      const auto now = level_y(which, x);
      if (now == line) break;
      line = now;
    }
    return line;
  }

  // The bottom row of panels (ticket, book, feed) made shorter, as a player drags the splitter under
  // the chart: the docked layout for the dock's size with a 120 px bottom row.
  void enlarge_chart() {
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(d_.widget(gn::kDock), &x, &y, &w, &h);
    game::DockMetrics m;
    m.bottom = 120;
    vm().set_layout(game::default_layout(w, h, m));
    d_.settle(d_.widget(names::kGameChart));
  }

  // Window y of a level's line in the last frame (and the chart's left edge in `x`); none without it.
  std::optional<float> level_y(const std::function<bool(const game::DragZone&)>& which, float& x) {
    const auto& zones = vm().chart.frame().zones;
    const auto z = std::find_if(zones.begin(), zones.end(), which);
    if (z == zones.end()) return std::nullopt;
    float y = 0, w = 0, h = 0;
    Rwidget_get_rect(d_.widget(names::kGameChart), &x, &y, &w, &h);
    return y + z->line_y;
  }

  // Price covered by one pixel of the chart's price pane.
  double price_per_pixel() { return std::abs(vm().chart.price_at(1) - vm().chart.price_at(0)); }

  // The chart's window rect (x, y, width, height).
  std::array<float, 4> chart_rect() {
    std::array<float, 4> r{};
    Rwidget_get_rect(d_.widget(names::kGameChart), &r[0], &r[1], &r[2], &r[3]);
    return r;
  }

  // Hovers a level's line and presses on it, then releases where it was: a grab without a move.
  // The chart's rect once hovered and once grabbed (none without the level).
  std::optional<std::pair<std::array<float, 4>, std::array<float, 4>>> grab_level(
      const std::function<bool(const game::DragZone&)>& which) {
    float x = 0;
    if (!hover_level(which, x)) return std::nullopt;
    const auto hovered = chart_rect();
    Rwindow_inject_mouse_button(d_.window(), R_MOUSE_LEFT, R_TRUE);
    d_.settle(d_.widget(names::kGameChart));
    REQUIRE(vm().chart.dragging());
    const auto grabbed = chart_rect();
    Rwindow_inject_mouse_button(d_.window(), R_MOUSE_LEFT, R_FALSE);
    d_.frames(2);
    return std::pair{hovered, grabbed};
  }

  float chart_y(double price) {
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(d_.widget(names::kGameChart), &x, &y, &w, &h);
    return y + vm().chart.y_of(price);
  }

 private:
  static constexpr float kAimPx = 12;  // the least distance a drag is aimed over

  App& app_;
  Driver& d_;
  Capture capture_;
  double aim_price_per_pixel_ = 0;
};

inline bool has_event(const GameState& g, std::string_view type) {
  return std::any_of(g.events.begin(), g.events.end(), [&](const EngineEvent& e) { return e.type == type; });
}

inline const Order* find_order(const GameState& g, std::string_view type) {
  const auto it = std::find_if(g.orders.begin(), g.orders.end(), [&](const Order& o) { return o.type == type; });
  return it == g.orders.end() ? nullptr : &*it;
}

// m01: a long with its stop, then the stop grabbed on the chart. Hovering the line and grabbing it fill
// the texts around the chart (bar readout, focused candle); the chart keeps its place and size, so the
// line stays under the pointer.
inline void flow_grab_keeps_chart(Driver& d, Terminal& t) {
  t.open(0);
  t.skip_question();
  d.choose(gn::kSizeBy, 1);  // quantity
  t.type(names::kGameQty, "1");
  REQUIRE(d.click("game-stop-chip-4"));
  t.submit_with_plan("rompimento com volume");
  t.advance();
  const Order* stop = find_order(t.g(), "stop_market");
  REQUIRE(stop != nullptr);
  const int stop_id = stop->id;
  d.frames(2);
  const auto before = t.chart_rect();
  const auto rects = t.grab_level([&](const game::DragZone& z) { return !z.target.draft && z.target.order_id == stop_id; });
  REQUIRE(rects.has_value());
  float sx = 0, sy = 0, sw = 0, sh = 0;
  Rwidget_get_rect(d.widget(gn::kChartStatus), &sx, &sy, &sw, &sh);
  INFO("chart y/height before " << before[1] << "/" << before[3] << ", hovered " << rects->first[1] << "/"
       << rects->first[3] << ", grabbed " << rects->second[1] << "/" << rects->second[3]
       << ", status height " << sh);
  CHECK(rects->first == before);
  CHECK(rects->second == before);
  CHECK_FALSE(d.text(gn::kChartStatus).empty());  // the focused candle is announced
}

// m01: a market buy sized by quantity with comma input and a preset stop, a written plan, the fill,
// the attached stop dragged on the chart, then a limit order placed and cancelled from its row.
inline void flow_place_move_cancel(App& app, Driver& d, Terminal& t) {
  t.open(0);
  t.skip_question();
  d.choose(gn::kSizeBy, 1);  // quantity
  REQUIRE(t.vm().ticket.size_by == game::SizeBy::Qty);
  t.type(names::kGameQty, "1,5");
  CHECK(app.vm().qty_text() == "1,5");
  REQUIRE(d.click("game-stop-chip-4"));  // 2% below the entry
  CHECK(t.vm().ticket.stop.find(',') != std::string::npos);
  t.shot("10-ticket");
  t.submit_with_plan("rompimento com volume");
  CHECK(d.text(names::kGameNotice).find("compra 1,5") != std::string::npos);
  t.advance();  // the market order fills at the next open; its stop becomes a working order
  CHECK(t.g().position.qty == doctest::Approx(1.5));
  CHECK(d.text(names::kGameAccount).find("COMPRADO 1,5") != std::string::npos);
  const Order* stop = find_order(t.g(), "stop_market");
  REQUIRE(stop != nullptr);
  const double before = stop->price;
  const double last = t.g().last_price();
  const int stop_id = stop->id;
  d.frames(2);
  // Halfway to the market: still on the losing side of the long, at any chart height.
  const double target = before + (last - before) / 2;
  REQUIRE(t.drag_level_to([&](const game::DragZone& z) { return !z.target.draft && z.target.order_id == stop_id; }, target));
  REQUIRE(d.idle());
  const Order* moved = find_order(t.g(), "stop_market");
  REQUIRE(moved != nullptr);
  CHECK(moved->id == stop_id);
  CHECK(moved->price > before);
  CHECK(moved->price < last);
  // Where it was dropped: within two pixels of the chart's scale (and the tick rounding).
  CHECK(std::abs(moved->price - target) <= 2 * t.aim_price_per_pixel() + t.g().contract.tick);
  CHECK(d.text(names::kGameNotice).find(std::to_string(stop_id)) != std::string::npos);
  t.shot("11-stop-moved");

  // A limit buy far below the market, then cancelled from the orders panel.
  d.choose(gn::kType, 1);  // limit
  REQUIRE(t.vm().ticket.type == game::TicketType::Limit);
  t.type(gn::kPrice, "90,5");
  t.type(names::kGameQty, "1");
  t.submit_with_plan();
  const Order* limit = find_order(t.g(), "limit");
  REQUIRE(limit != nullptr);
  CHECK(limit->price == doctest::Approx(90.5));
  const int limit_id = limit->id;
  d.frames(2);
  REQUIRE(d.click("game-order-cancel-" + std::to_string(limit_id)));
  REQUIRE(d.idle());
  CHECK(find_order(t.g(), "limit") == nullptr);
  CHECK(has_event(t.g(), "order"));
}

// m03 seed 5: 20x long with a 1% stop, the stop dragged past liquidation (confirmed in the dialog),
// then the market liquidates the position.
inline void flow_liquidation(App& app, Driver& d, Terminal& t) {
  t.open(2);
  REQUIRE(t.g().mission == "m03-volatility-shock");
  t.skip_question();
  d.choose(gn::kSizeBy, 1);
  t.type(gn::kLeverage, "20");
  t.type(names::kGameQty, "1800");
  REQUIRE(d.click("game-stop-chip-3"));  // 1%
  t.submit_with_plan();
  t.advance();
  REQUIRE(t.g().position.qty > 0);  // a thin bar fills part of it (capacity): still 20x
  REQUIRE(t.g().account.liquidation.has_value());
  const double liq = *t.g().account.liquidation;
  const Order* stop = find_order(t.g(), "stop_market");
  REQUIRE(stop != nullptr);
  const int stop_id = stop->id;
  d.frames(2);
  // Below the LIQ line: the move waits for the player's confirmation.
  const float dy = t.chart_y(liq) + 12 - t.chart_y(stop->price);
  REQUIRE(t.drag_level([&](const game::DragZone& z) { return !z.target.draft && z.target.order_id == stop_id; }, dy));
  REQUIRE(t.vm().modal() == game::Modal::ConfirmMove);
  CHECK(d.text(gn::kConfirmText).find(std::to_string(stop_id)) != std::string::npos);
  t.shot("12-confirm-beyond-liq");
  REQUIRE(d.click(gn::kConfirmYes));
  REQUIRE(d.idle());
  REQUIRE(find_order(t.g(), "stop_market") != nullptr);
  CHECK(find_order(t.g(), "stop_market")->price <= liq);
  for (int i = 0; i < 40 && !has_event(t.g(), "liquidation") && app.vm().screen() == Screen::Game; ++i) t.advance();
  REQUIRE(has_event(t.g(), "liquidation"));
  CHECK(t.g().position.qty == 0);
  t.shot("13-liquidated");
}

// m04: the options tab, a call bought from the chain at the ask, its plan, the fill in the rail.
inline void flow_option_buy(App& app, Driver& d, Terminal& t) {
  t.open(3);
  REQUIRE(t.g().options.enabled);
  t.skip_question();
  REQUIRE(d.click(gn::kTabOptions));
  CHECK(t.vm().tab() == game::Tab::Options);
  d.frames(2);
  // The first call's ask: the second button of the first chain row.
  const Rid chain = d.widget(gn::kChain);
  Rid ask = R_INVALID_ID;
  for (int i = 0; i < Rwidget_get_child_count(chain) && ask == R_INVALID_ID; ++i) {
    const Rid row = Rwidget_get_child(chain, i);
    if (Rwidget_get_type(row) == R_WIDGET_HBOX && Rwidget_get_child_count(row) == 5) ask = Rwidget_get_child(row, 1);
  }
  REQUIRE(ask != R_INVALID_ID);
  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(ask, &x, &y, &w, &h);
  d.click_at(x + w / 2, y + h / 2);
  REQUIRE(t.vm().option_ticket.series.has_value());
  CHECK(t.vm().option_ticket.buy);
  t.shot("14-option-ticket");
  REQUIRE(d.click(gn::kOptSubmit));
  REQUIRE(t.vm().modal() == game::Modal::Plan);
  CHECK(t.vm().plan.option());
  REQUIRE(d.click(gn::kPlanSkip));
  REQUIRE(d.idle());
  CHECK(d.text(names::kGameNotice).find("compra C ") != std::string::npos);
  t.advance();
  REQUIRE(t.g().options.positions.size() == 1);
  CHECK(t.g().options.positions[0].qty == doctest::Approx(1));
  CHECK(app.vm().screen() == Screen::Game);
  t.shot("15-option-filled");
}

}  // namespace tradelab::desktop::testing
