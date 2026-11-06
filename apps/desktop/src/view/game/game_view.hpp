// The terminal screen on Raven (resources/views/game.xml): thin glue between the widgets and
// game::GameViewModel. Text fields write into the viewmodel as the player types; refresh() copies the
// viewmodel's texts back once per change; the chart and the payoff are painted from viewmodel
// geometry; keys reach the viewmodel through the window hook.
#pragma once

#include <raven/raven_core.h>
#include <raven/raven_draw.h>
#include <raven/raven_window.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "view/game/widgets.hpp"
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop::view {

class GameView {
 public:
  GameView(AppViewModel& app, Rid root, Rid window);
  GameView(const GameView&) = delete;
  GameView& operator=(const GameView&) = delete;

  void bind();
  // Pushes viewmodel changes into the widgets (cheap when nothing changed); also runs auto-advance.
  void refresh();
  // Window key hook while the terminal shows: true when the key was used.
  bool on_key(const REvent* e);

  [[nodiscard]] Rid find(std::string_view name) const;
  // Named terminal widgets missing from the loaded view (headless test).
  [[nodiscard]] std::vector<std::string> missing_names() const;
  // The web's name of a key event ("Space", "Shift+Space", "b", "?", "Escape"...), "" for a modifier.
  static std::string key_name(const REvent* e);

 private:
  game::GameViewModel& vm() { return app_.game(); }
  [[nodiscard]] const game::GameViewModel& vm() const { return app_.game(); }

  void bind_chart();
  void bind_top();
  void bind_ticket();
  void bind_levels();
  void bind_protect();
  void bind_options();
  void bind_rail_and_panels();
  void bind_dialogs();
  void bind_plan_dialog();
  void bind_input(std::string_view name, std::string game::TicketViewModel::*field);
  void bind_chips(std::string_view field, int count, bool stop_level, bool protect);
  void use_chip(int i, bool stop_level, bool protect);
  void build_chain(Rid box, const game::Texts& t, const std::vector<game::ChainExpiry>& chain);
  void add_chain_row(Rid box, const game::ChainRow& r);
  void add_lesson(Rid box, const game::Texts& t, const game::LessonQuestionView& q);

  void apply_labels();
  void apply_top(const game::Texts& t, const GameState& g);
  void apply_chart(const game::Texts& t, const GameState& g);
  void apply_table(const game::Texts& t, const GameState& g);
  void apply_ticket(const game::Texts& t, const GameState& g);
  void apply_levels(const game::TicketView& v);
  void apply_protect(const game::Texts& t, const game::TicketView& v);
  void apply_options(const game::Texts& t, const GameState& g);
  void apply_chain(const game::Texts& t, const GameState& g);
  void apply_rail(const game::Texts& t, const GameState& g);
  void apply_orders(const game::Texts& t, const GameState& g);
  void apply_advisor(const game::Texts& t, const GameState& g);
  void apply_lists(const game::Texts& t, const GameState& g);
  void apply_modal(const game::Texts& t, const GameState& g);
  void apply_plan(const game::Texts& t);
  void apply_help(const game::Texts& t);
  void apply_lessons(const game::Texts& t, const GameState& g);
  void apply_perks(const game::Texts& t);
  void apply_tutorial(const game::Texts& t, const GameState& g);
  void set_chips(std::string_view field, const std::vector<game::LevelChip>& chips);
  void clear_rows(Rid box, std::vector<Rid>& rows);

  static void on_chart_paint(Rid widget, RPainter* painter, float width, float height, void* self);
  static void on_payoff_paint(Rid widget, RPainter* painter, float width, float height, void* self);
  static int on_chart_event(Rid widget, const REvent* e, void* self);
  static const char* on_table_cell(Rid table, int row, int column, void* self);
  void chart_event(const REvent* e);

  AppViewModel& app_;
  Rid root_;
  Rid window_;
  Callbacks cb_;
  LineList notes_, opt_notes_, rail_options_, adv_read_, adv_reasons_, news_, feed_, journal_;
  std::vector<Rid> order_rows_, chain_rows_, help_rows_, lesson_rows_, perk_rows_;
  std::vector<int> order_ids_;
  std::string chain_key_;
  std::string lesson_key_;
  std::uint64_t shown_ = 0;
  Locale shown_locale_ = Locale::PtBR;
  bool labels_done_ = false;
  std::string cell_;
  std::chrono::steady_clock::time_point last_tick_ = std::chrono::steady_clock::now();
};

}  // namespace tradelab::desktop::view
