// The terminal on Raven's null platform: every terminal widget loads, the main flows play with
// injected input (place, move, cancel, liquidation with the beyond-liquidation confirmation, option
// buy), the keyboard reaches the shortcuts and dialogs, and the bar table never shows a future bar.
#include <doctest/doctest.h>

#include <raven/raven_controls.h>
#include <raven/raven_shell.h>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/storage_fixtures.hpp"
#include "view/app.hpp"
#include "viewmodel/game/dock_layout.hpp"
#include "viewmodel/screen_names.hpp"

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

// The window the hosted macOS runners give (their 1024x768 screen shrinks every window to 1024x645): a
// short chart, where a drag of a fixed 12 pixels carried the stop past the market and was refused.
TEST_CASE("headless terminal: the stop drag lands on its price in a short chart (1024x645)") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1024, 645));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  testing::flow_place_move_cancel(app, d, t);
}

// The amount field's label follows the sizing mode (OrderTicket.svelte), also after the shell redraws its
// own texts (a screen change): the shell's bindings once wrote "Quantity" over it whatever the mode.
TEST_CASE("headless terminal: the amount label follows the sizing mode across a trip to the menu") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  t.open(0);
  t.skip_question();
  d.frames(3);
  REQUIRE(app.vm().game().ticket.size_by == game::SizeBy::Risk);
  CHECK(d.text(names::kGameQtyLabel) == "Risco (% patrimônio)");
  REQUIRE(d.click(names::kGameMenu));
  REQUIRE(d.click(names::kMenuResume));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Game; }));
  d.frames(3);
  CHECK(d.text(names::kGameQtyLabel) == "Risco (% patrimônio)");
  d.choose(gn::kSizeBy, 1);
  d.frames(3);
  CHECK(d.text(names::kGameQtyLabel) == "Quantidade (contratos)");
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

namespace {

// Visible inputs, buttons and combo boxes that stick out of the scroll panel they live in.
void collect_overflows(Rid w, float left, float right, std::vector<std::string>& out) {
  if (Rwidget_get_visibility(w) != R_VISIBLE) return;
  float x = 0, y = 0, width = 0, height = 0;
  Rwidget_get_rect(w, &x, &y, &width, &height);
  const RWidgetType type = Rwidget_get_type(w);
  if (type == R_WIDGET_SCROLL_BOX && width > 0) {
    left = x;
    right = x + width;
  }
  const bool control = type == R_WIDGET_TEXT_INPUT || type == R_WIDGET_BUTTON || type == R_WIDGET_COMBO_BOX;
  if (control && width > 0 && (x < left - 0.5F || x + width > right + 0.5F)) {
    const char* name = Rwidget_get_name(w);
    out.push_back(std::string(name != nullptr ? name : "(unnamed)") + " x=" + std::to_string(x) + ".." +
                  std::to_string(x + width) + " panel=" + std::to_string(left) + ".." + std::to_string(right));
  }
  for (int i = 0; i < Rwidget_get_child_count(w); ++i) collect_overflows(Rwidget_get_child(w, i), left, right, out);
}

}  // namespace

// Regression: with the theme's monospace font the ticket's label + field rows overflowed the 340 px
// ticket column, so a click on the amount field landed in the next panel and typing went nowhere.
TEST_CASE("headless terminal: with the retro theme every terminal control fits inside its panel") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.to_briefing());
  REQUIRE(d.click(names::kBriefGo));
  t.skip_question();
  d.frames(2);
  std::vector<std::string> overflows;
  collect_overflows(Rwindow_get_content(d.window()), -1e9F, 1e9F, overflows);
  std::string all;
  for (const auto& o : overflows) all += o + "\n";
  CHECK_MESSAGE(overflows.empty(), all);
}

namespace {

float width_of(Driver& d, std::string_view name) {
  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(d.widget(name), &x, &y, &w, &h);
  return w;
}

}  // namespace

// Docking (desktop-client "Terminal UX parity"): the default keeps the web's column widths at any window
// size, a moved panel is stored with the prefs and comes back at the next launch, "Reset layout" restores.
TEST_CASE("headless terminal: docked panels keep their default widths, a moved layout persists, Reset restores") {
  testing::TempDir dir;
  storage::ProfileStore profile(dir.path() / "tradelab.db");  // where the app keeps the layout
  storage::GamePrefsStore prefs(profile);
  std::string moved;
  {
    RavenSession raven;
    App app(builtin_catalog(), [] { return 1u; });
    app.vm().game().attach_prefs(&prefs);
    REQUIRE(app.build(1024, 768));
    Driver d(app);
    d.frames(2);
    Terminal t(app, d);
    t.open(0);
    d.frames(4);
    CHECK(width_of(d, game::kPanelTicket) == doctest::Approx(340).epsilon(0.02));
    CHECK(width_of(d, game::kPanelRail) == doctest::Approx(290).epsilon(0.02));
    CHECK(prefs.load().layout.empty());  // the default is not stored
    const Rid dock = d.widget(gn::kDock);
    // The player puts the feed behind the ticket's tab (what a tab drag does), as Raven reports it.
    Rdock_area_move_panel(dock, d.widget(game::kPanelFeed), R_DOCK_TAB, d.widget(game::kPanelTicket), 0);
    app.vm().game().dock.changed();
    app.vm().game().set_layout(Rdock_area_save_layout(dock));
    moved = prefs.load().layout;
    CHECK(moved.find("game-panel-ticket,game-panel-feed") != std::string::npos);
  }
  {
    RavenSession raven;
    App app(builtin_catalog(), [] { return 1u; });
    app.vm().game().attach_prefs(&prefs);
    REQUIRE(app.build(1024, 768));
    Driver d(app);
    d.frames(2);
    Terminal t(app, d);
    t.open(0);
    d.frames(4);
    const Rid dock = d.widget(gn::kDock);
    CHECK(std::string(Rdock_area_save_layout(dock)) == moved);
    t.skip_question();
    REQUIRE(d.click(gn::kLayoutReset));
    d.frames(4);
    CHECK(prefs.load().layout.empty());
    CHECK(Rdock_area_is_panel_shown(dock, d.widget(game::kPanelFeed)));
    CHECK(Rdock_area_is_panel_shown(dock, d.widget(game::kPanelTicket)));
    CHECK(width_of(d, game::kPanelTicket) == doctest::Approx(340).epsilon(0.02));
  }
}
