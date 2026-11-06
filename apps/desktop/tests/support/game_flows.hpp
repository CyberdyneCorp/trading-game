// Scripted terminal flows driven by injected input (clicks, keys, typed text, drags on the chart), the
// desktop counterpart of the web's Playwright game specs. Shared by the headless tests (Raven null
// platform) and the windowed tests (Vulkan, with captures).
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_widget.h>

#include <algorithm>
#include <functional>
#include <string>

#include "support/driver.hpp"
#include "view/app.hpp"
#include "viewmodel/format.hpp"
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

  float chart_y(double price) {
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(d_.widget(names::kGameChart), &x, &y, &w, &h);
    return y + vm().chart.y_of(price);
  }

 private:
  App& app_;
  Driver& d_;
  Capture capture_;
};

inline bool has_event(const GameState& g, std::string_view type) {
  return std::any_of(g.events.begin(), g.events.end(), [&](const EngineEvent& e) { return e.type == type; });
}

inline const Order* find_order(const GameState& g, std::string_view type) {
  const auto it = std::find_if(g.orders.begin(), g.orders.end(), [&](const Order& o) { return o.type == type; });
  return it == g.orders.end() ? nullptr : &*it;
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
  const int stop_id = stop->id;
  d.frames(2);
  REQUIRE(t.drag_level([&](const game::DragZone& z) { return !z.target.draft && z.target.order_id == stop_id; }, -12));
  REQUIRE(d.idle());
  const Order* moved = find_order(t.g(), "stop_market");
  REQUIRE(moved != nullptr);
  CHECK(moved->id == stop_id);
  CHECK(moved->price > before);
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
