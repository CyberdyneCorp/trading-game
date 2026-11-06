#include "view/game/game_view.hpp"

#include <algorithm>
#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_draw.h>
#include <raven/raven_widget.h>
#include <raven/raven_style.h>
#include <raven/raven_shell.h>
#include <raven/raven_window.h>

#include <cctype>

#include "view/game/scene_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/dock_layout.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/game/hotkeys.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::view {

namespace n = names::game;
using game::Modal;

namespace {

std::string named_key(RKey k) {
  switch (k) {
    case R_KEY_SPACE: return "Space";
    case R_KEY_ESCAPE: return "Escape";
    case R_KEY_ENTER:
    case R_KEY_KP_ENTER: return "Enter";
    case R_KEY_TAB: return "Tab";
    case R_KEY_LEFT: return "ArrowLeft";
    case R_KEY_RIGHT: return "ArrowRight";
    case R_KEY_UP: return "ArrowUp";
    case R_KEY_DOWN: return "ArrowDown";
    case R_KEY_HOME: return "Home";
    case R_KEY_END: return "End";
    case R_KEY_PAGE_UP: return "PageUp";
    case R_KEY_PAGE_DOWN: return "PageDown";
    case R_KEY_BACKSPACE: return "Backspace";
    case R_KEY_DELETE: return "Delete";
    default: break;
  }
  return {};
}

// US layout: what Shift makes of a punctuation key (the web reads the character).
struct Punctuation {
  RKey key;
  char plain;
  char shifted;
};
constexpr Punctuation kPunctuation[] = {
    {R_KEY_SLASH, '/', '?'},       {R_KEY_COMMA, ',', '<'},         {R_KEY_PERIOD, '.', '>'},
    {R_KEY_MINUS, '-', '_'},       {R_KEY_EQUAL, '=', '+'},         {R_KEY_SEMICOLON, ';', ':'},
    {R_KEY_APOSTROPHE, '\'', '"'}, {R_KEY_LEFT_BRACKET, '[', '{'}, {R_KEY_RIGHT_BRACKET, ']', '}'},
    {R_KEY_BACKSLASH, '\\', '|'},  {R_KEY_GRAVE, '`', '~'}};

char printable(RKey k, bool shift) {
  if (k >= R_KEY_A && k <= R_KEY_Z) return static_cast<char>('a' + (k - R_KEY_A));
  if (k >= R_KEY_0 && k <= R_KEY_9) return shift ? ")!@#$%^&*("[k - R_KEY_0] : static_cast<char>('0' + (k - R_KEY_0));
  for (const auto& p : kPunctuation) {
    if (p.key == k) return shift ? p.shifted : p.plain;
  }
  return 0;
}

}  // namespace

GameView::GameView(AppViewModel& app, Rid root, Rid window) : app_(app), root_(root), window_(window) {}

Rid GameView::find(std::string_view name) const { return ui::find(root_, name); }

std::string GameView::key_name(const REvent* e) {
  const bool shift = (e->modifiers & R_MOD_SHIFT) != 0;
  const std::string named = named_key(e->key);
  if (!named.empty()) return shift ? "Shift+" + named : named;
  const char c = printable(e->key, shift);
  return c != 0 ? std::string(1, c) : std::string();
}

std::vector<std::string> GameView::missing_names() const {
  std::vector<std::string> missing;
  for (std::string_view w : n::kRequired) {
    if (find(w) == R_INVALID_ID) missing.push_back("widget " + std::string(w));
  }
  for (std::string_view w : game::kPanels) {
    if (find(w) == R_INVALID_ID) missing.push_back("widget " + std::string(w));
  }
  for (std::string_view w : {"game-stop-chip-4", "game-tp-chip-2", "game-pstop-chip-4", "game-ptp-chip-2", "game-trail-chip-3"}) {
    if (find(w) == R_INVALID_ID) missing.push_back("widget " + std::string(w));
  }
  return missing;
}

void GameView::bind() {
  notes_ = LineList(find(n::kNotes), 12);
  opt_notes_ = LineList(find(n::kOptNotes), 12);
  rail_options_ = LineList(find(n::kRailOptions), 12);
  adv_read_ = LineList(find(n::kAdvRead), 12);
  adv_reasons_ = LineList(find(n::kAdvReasons), 12);
  news_ = LineList(find(n::kNews), 12);
  feed_ = LineList(find(n::kFeed), 12);
  journal_ = LineList(find(n::kJournalLines), 13);
  bind_dock();
  bind_chart();
  bind_top();
  bind_ticket();
  bind_levels();
  bind_protect();
  bind_options();
  bind_rail_and_panels();
  bind_dialogs();
}

// ---- chart ----

void GameView::bind_chart() {
  const Rid chart = find(names::kGameChart);
  Rcustom_set_paint_callback(chart, on_chart_paint, this);
  Rwidget_set_event_callback(chart, on_chart_event, this);
  Rwidget_set_focusable(chart, R_TRUE);
  Rwidget_set_accessible_role(chart, R_ROLE_SLIDER);
  Rcustom_set_paint_callback(find(n::kOptPayoff), on_payoff_paint, this);
  Rwidget_set_accessible_hidden(find(n::kOptPayoff), R_TRUE);
  const auto toggle = [this](std::string_view name, bool game::ChartViewModel::*flag) {
    cb_.check(find(name), [this, flag](bool on) {
      vm().chart.*flag = on;
      vm().touch();
    });
  };
  toggle(n::kSma, &game::ChartViewModel::show_sma);
  toggle(n::kEma, &game::ChartViewModel::show_ema);
  toggle(n::kVwap, &game::ChartViewModel::show_vwap);
  toggle(n::kBb, &game::ChartViewModel::show_bb);
  toggle(n::kShowPatterns, &game::ChartViewModel::show_patterns);
  cb_.choose(find(n::kOsc), [this](int i) {
    vm().chart.osc = static_cast<game::Oscillator>(std::min(std::max(0, i), 3));
    vm().touch();
  });
  cb_.click(find(n::kTableToggle), [this] {
    vm().chart.table_open = !vm().chart.table_open;
    vm().touch();
  });
  const auto page = [this](std::string_view name, int delta) {
    cb_.click(find(name), [this, delta] {
      if (vm().state()) vm().chart.page_by(*vm().state(), delta);
      vm().touch();
    });
  };
  page(n::kTablePrev, -1);
  page(n::kTableNext, 1);
  page(n::kTableLast, 1'000'000);
  const Rid table = find(n::kTable);
  for (const char* header : {"#", "O", "H", "L", "C", "V"}) Rtable_add_column(table, header, 70);
  Rwidget_set_accessible_role(table, R_ROLE_TABLE);
}

void GameView::on_chart_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* self) {
  auto* view = static_cast<GameView*>(self);
  paint_scene(painter, view->vm().chart_scene(width, height));
}

void GameView::on_payoff_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* self) {
  auto* view = static_cast<GameView*>(self);
  const GameState* g = view->vm().state();
  if (g == nullptr) return;
  paint_payoff(painter, view->vm().option_ticket.view(view->vm().texts(), *g, false), width, height);
}

int GameView::on_chart_event(Rid /*widget*/, const REvent* e, void* self) {
  static_cast<GameView*>(self)->chart_event(e);
  return e->type == R_EVENT_MOUSE_WHEEL || e->type == R_EVENT_MOUSE_DOWN ? R_TRUE : R_FALSE;
}

void GameView::chart_event(const REvent* e) {
  const GameState* g = vm().state();
  if (g == nullptr) return;
  game::GameViewModel& m = vm();
  const bool picking = m.picking() != game::PickTarget::None;
  switch (e->type) {
    case R_EVENT_MOUSE_MOVE: m.chart_action(m.chart.pointer_move(*g, e->local_x, e->local_y, picking)); break;
    case R_EVENT_MOUSE_DOWN:
      if (e->button == R_MOUSE_LEFT) m.chart.pointer_down(e->local_x, e->local_y, picking);
      break;
    case R_EVENT_MOUSE_UP:
      if (e->button != R_MOUSE_LEFT) break;
      if (m.chart.dragging()) m.chart_action(m.chart.pointer_up());
      else m.chart_action(m.chart.click(*g, e->local_x, e->local_y, picking));
      break;
    case R_EVENT_MOUSE_WHEEL: m.chart.wheel(*g, e->wheel_y); break;
    case R_EVENT_MOUSE_LEAVE: m.chart.pointer_leave(); break;
    case R_EVENT_FOCUS_GAINED: m.chart.focus_gained(*g); break;
    case R_EVENT_FOCUS_LOST: m.chart.focus_lost(); break;
    default: return;
  }
  m.touch();
}

const char* GameView::on_table_cell(Rid /*table*/, int row, int column, void* self) {
  auto* view = static_cast<GameView*>(self);
  const GameState* g = view->vm().state();
  view->cell_.clear();
  if (g == nullptr) return view->cell_.c_str();
  const game::BarPage p = view->vm().chart.page(*g);
  const int i = p.start + row;
  if (i < 0 || i >= p.end) return view->cell_.c_str();
  const auto& b = g->bars[static_cast<std::size_t>(i)];
  if (column == 0) {
    view->cell_ = std::to_string(i) + (b.continuity ? " *" : "");
  } else {
    view->cell_ = game::candle_cells(b, view->vm().texts().locale())[static_cast<std::size_t>(column - 1)];
  }
  return view->cell_.c_str();
}

// ---- top bar ----

void GameView::bind_top() {
  cb_.click(find(n::kAuto), [this] { vm().toggle_auto(); });
  cb_.click(find(n::kHelp), [this] { vm().open_help(); });
  cb_.click(find(n::kPatterns), [this] { vm().open_patterns(std::nullopt); });
  cb_.click(find(n::kJournal), [this] { vm().open_journal(); });
  cb_.click(find(n::kLessons), [this] { vm().open_lessons(); });
  cb_.click(find(n::kTutorial), [this] { vm().open_tutorial(); });
  cb_.click(find(n::kPerks), [this] { vm().open_perks(); });
  cb_.click(find(n::kSound), [this] { vm().toggle_mute(); });
  cb_.choose(find(n::kAutoSpeed), [this](int i) {
    vm().autoplay.set_speed(i);
    vm().touch();
  });
  cb_.check(find(n::kAutoEvents), [this](bool on) {
    vm().autoplay.pause_on_events = on;
    vm().touch();
  });
}

// ---- docked panels ----

void GameView::bind_dock() {
  const Rid area = find(n::kDock);
  for (std::string_view panel : game::kPanels) Rdock_area_set_panel_closable(area, find(panel), R_FALSE);
  Rdock_area_set_changed_callback(area, on_dock_changed, this);
  cb_.click(find(n::kLayoutReset), [this] { vm().reset_layout(); });
}

// The player moved or resized a panel: the arrangement is kept with the prefs.
void GameView::on_dock_changed(Rid area, void* self) {
  auto* view = static_cast<GameView*>(self);
  view->vm().dock.changed();
  view->vm().set_layout(Rdock_area_save_layout(area));
}

// Each frame: the stored arrangement once, else the default for the area's size whenever it changes.
void GameView::apply_dock() {
  const Rid area = find(n::kDock);
  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(area, &x, &y, &w, &h);
  game::DockMetrics m;
  m.gap = std::max(Rwidget_get_metric(area, R_METRIC_SPLITTER_SIZE), 4.0F);
  m.tab_bar = Rwidget_get_metric(area, R_METRIC_CONTROL_HEIGHT) + 2;
  const auto step = vm().dock.frame(w, h, vm().prefs().layout, m);
  if (!step.apply) return;
  // A stored text Raven cannot read (damaged prefs) falls back to the default.
  if (Rdock_area_load_layout(area, step.text.c_str()) == R_FALSE) vm().reset_layout();
}

// ---- keyboard ----

namespace {

game::Focus focus_kind(Rid widget) {
  if (widget == R_INVALID_ID) return game::Focus::None;
  switch (Rwidget_get_type(widget)) {
    case R_WIDGET_TEXT_INPUT:
    case R_WIDGET_TEXT_EDITOR:
    case R_WIDGET_SPIN_BOX: return game::Focus::Text;
    case R_WIDGET_COMBO_BOX: return game::Focus::Select;
    case R_WIDGET_SLIDER: return game::Focus::Slider;
    case R_WIDGET_CHECKBOX: return game::Focus::Checkbox;
    case R_WIDGET_BUTTON: return game::Focus::Button;
    default: return game::Focus::Other;
  }
}

}  // namespace

bool GameView::on_key(const REvent* e) {
  if (e->type != R_EVENT_KEY_DOWN) return false;
  const std::string name = key_name(e);
  if (name.empty()) return false;
  const Ruint chord = R_MOD_CTRL | R_MOD_ALT | R_MOD_SUPER;
  // The focused control keeps the keys it uses (web hotkeys.ts keepsKey): a text field all of them, a
  // combo box all but Space, a slider its arrows; a checkbox or button none.
  const game::Focus focus = Rwindow_wants_text_input(window_) != 0 ? game::Focus::Text : focus_kind(Rwindow_get_focused_widget(window_));
  const bool typing = game::keeps_key(focus, name);
  const bool chart_focused = Rwidget_is_focused(find(names::kGameChart)) != 0;
  const game::KeyPress k{name, typing, (e->modifiers & chord) != 0, chart_focused};
  if (!vm().handle_key(k)) return false;
  if (chart_focused && vm().state()) {
    const std::string said = vm().chart.announcement(vm().texts(), *vm().state());
    if (!said.empty()) Raccessibility_announce(said.c_str(), R_FALSE);
  }
  return true;
}

// ---- refresh ----

void GameView::refresh() {
  const auto now = std::chrono::steady_clock::now();
  const double dt = std::chrono::duration<double>(now - last_tick_).count();
  last_tick_ = now;
  if (app_.screen() != Screen::Game) return;
  apply_dock();
  vm().tick(dt);
  const GameState* g = vm().state();
  if (g == nullptr) return;
  const game::Texts t = vm().texts();
  if (!labels_done_ || shown_locale_ != t.locale()) {
    apply_labels();
    labels_done_ = true;
    shown_locale_ = t.locale();
    shown_ = 0;
  }
  if (vm().revision() == shown_) return;
  shown_ = vm().revision();
  apply_top(t, *g);
  apply_chart(t, *g);
  apply_ticket(t, *g);
  apply_options(t, *g);
  apply_chain(t, *g);
  apply_rail(t, *g);
  apply_orders(t, *g);
  apply_advisor(t, *g);
  apply_lists(t, *g);
  apply_modal(t, *g);
  Rwidget_invalidate(find(names::kGameChart));
  Rwidget_invalidate(find(n::kOptPayoff));
}

}  // namespace tradelab::desktop::view
