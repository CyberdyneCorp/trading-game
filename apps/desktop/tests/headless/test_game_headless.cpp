// The terminal on Raven's null platform: every terminal widget loads, the main flows play with
// injected input (place, move, cancel, liquidation with the beyond-liquidation confirmation, option
// buy), the keyboard reaches the shortcuts and dialogs, and the bar table never shows a future bar.
#include <doctest/doctest.h>

#include <raven/raven_controls.h>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "view/app.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
namespace gn = tradelab::desktop::names::game;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

struct RavenSession {
  RavenSession() { REQUIRE(init_headless()); }
  ~RavenSession() { Rshutdown(); }
};

}  // namespace

TEST_CASE("headless terminal: place, move the attached stop by dragging it, cancel a limit order") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  testing::flow_place_move_cancel(app, d, t);
}

TEST_CASE("headless terminal: a stop dragged past liquidation is confirmed, then the long is liquidated") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 5u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  testing::flow_liquidation(app, d, t);
}

TEST_CASE("headless terminal: a call bought from the chain with its plan") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 7u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  testing::flow_option_buy(app, d, t);
}

TEST_CASE("headless terminal: shortcuts, help with remapping, chart focus and the bar table") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 2u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  t.open(0);
  t.skip_question();
  auto& vm = t.vm();
  d.focus(names::kGameAdvance);
  d.key(R_KEY_S);
  CHECK(vm.ticket.side == game::Side::Sell);
  d.key(R_KEY_B);
  CHECK(vm.ticket.side == game::Side::Buy);
  d.key(R_KEY_SLASH, R_MOD_SHIFT);  // "?"
  REQUIRE(vm.modal() == game::Modal::Help);
  const int step = t.g().step;
  d.key(R_KEY_SPACE);  // the help overlay keeps the market still
  CHECK(t.g().step == step);
  d.key(R_KEY_ESCAPE);
  CHECK(vm.modal() == game::Modal::None);

  // Remap advance to N from the help overlay; Space then does nothing, N advances.
  REQUIRE(d.click(gn::kHelp));
  const Rid rows = d.widget(gn::kHelpRows);
  const Rid change = Rwidget_get_child(Rwidget_get_child(rows, 0), 2);
  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(change, &x, &y, &w, &h);
  d.click_at(x + w / 2, y + h / 2);
  REQUIRE(vm.remap.waiting.has_value());
  d.key(R_KEY_N);
  CHECK(vm.keymap().key(game::Action::Advance) == "n");
  REQUIRE(d.click(gn::kHelpOk));
  d.focus(gn::kReadout);  // a label: Space is nobody's key now
  d.key(R_KEY_SPACE);
  d.frames(3);
  CHECK(t.g().step == step);
  d.key(R_KEY_N);
  REQUIRE(d.idle());
  CHECK(t.g().step == step + 1);
  vm.reset_keymap();

  // Chart keyboard focus walks the closed bars; the table pages end with the current bar.
  t.skip_question();
  d.focus(names::kGameChart);
  REQUIRE(vm.chart.focus_bar().has_value());
  CHECK(*vm.chart.focus_bar() == static_cast<int>(t.g().bars.size()) - 1);
  d.key(R_KEY_LEFT);
  CHECK(*vm.chart.focus_bar() == static_cast<int>(t.g().bars.size()) - 2);
  d.key(R_KEY_HOME);
  CHECK(*vm.chart.focus_bar() == 0);
  REQUIRE(d.click(gn::kTableToggle));
  CHECK(vm.chart.table_open);
  const Rid table = d.widget(gn::kTable);
  CHECK(Rtable_get_row_count(table) == 50);
  CHECK(std::string(Rtable_get_cell(table, 49, 0)) == std::to_string(t.g().bars.size() - 1));
}
