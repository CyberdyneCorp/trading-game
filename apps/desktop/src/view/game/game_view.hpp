// The terminal screen on Raven (resources/views/game.xml): thin glue between the widgets and
// game::GameViewModel. Text fields write into the viewmodel as the player types; refresh() copies the
// viewmodel's texts back once per change; the chart and the payoff are painted from viewmodel
// geometry; keys reach the viewmodel through the window hook.
#pragma once

#include <raven/raven_core.h>
#include <raven/raven_draw.h>
#include <raven/raven_window.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "view/game/lab_panel.hpp"
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

  void bind_dock();
  void apply_dock();
  void fit_text_scale();
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
  void build_order_rows(const game::Texts& t, const std::vector<game::OrderRow>& rows);
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
  // Strategy builder, combo notice and lab dialog (game_view_builder.cpp).
  void bind_builder();
  void apply_builder(const game::Texts& t, const GameState& g);
  void build_leg_rows(const game::BuilderView& v);
  void apply_sizing(const game::Texts& t, const game::BuilderView& v);
  void clear_leg_rows();
  void add_leg_row(const game::BuilderView& v, std::size_t i);
  void add_leg_extra(const game::BuilderView& v, std::size_t i, Rid leg);
  void apply_builder_expiries(const game::Texts& t, const game::BuilderView& v);
  void apply_leg_rows(const game::BuilderView& v);
  void apply_combo_notice();
  void apply_lab_dialog(const game::Texts& t);
  static void on_builder_paint(Rid widget, RPainter* painter, float width, float height, void* self);
  // Wave 1 of add-practice-and-advanced-strategies (game_view_wave1.cpp).
  void bind_wave1();
  void bind_heatmaps();
  void bind_live();
  void apply_tif(const game::TifView& v, std::string_view combo, std::string_view label, std::string_view bars_row,
                 std::string_view bars_label, std::string_view bars, std::string_view hint, const std::string& bars_text);
  void apply_wave1_ticket(const game::Texts& t, const game::TicketView& v);
  void apply_wave1_option(const game::OptionTicketView& v);
  void apply_wave1_book(const game::Texts& t);
  void apply_wave1_builder(const game::Texts& t);
  void apply_heatmap(const game::Texts& t, std::string_view prefix, const std::optional<game::WhatIfHeatmap>& map, bool shown);
  void apply_live(const game::Texts& t);
  void apply_live_panel(const game::Texts& t, const game::GameViewModel::LiveView& v);
  void apply_confirm();
  void apply_portfolio(const game::Texts& t);
  void paint_heatmap(RPainter* painter, float width, float height, const std::optional<game::WhatIfHeatmap>& map);
  int heatmap_event(Rid widget, const REvent* e, const std::optional<game::WhatIfHeatmap>& map);
  bool heatmap_key(int key, std::size_t rows, std::size_t cols);
  static void on_builder_heatmap_paint(Rid widget, RPainter* painter, float width, float height, void* self);
  static void on_portfolio_heatmap_paint(Rid widget, RPainter* painter, float width, float height, void* self);
  static int on_builder_heatmap_event(Rid widget, const REvent* e, void* self);
  static int on_portfolio_heatmap_event(Rid widget, const REvent* e, void* self);

  static void on_dock_changed(Rid area, void* self);
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
  float content_height_ = 0;
  std::string cell_;
  std::chrono::steady_clock::time_point last_tick_ = std::chrono::steady_clock::now();
  LineList builder_preview_, builder_table_;
  std::vector<Rid> leg_rows_;
  std::string legs_key_, expiries_key_, far_key_, tif_key_;
  std::unique_ptr<LabPanel> lab_;
  std::map<std::string, std::string> tif_items_;
  std::map<std::string, LineList> heatmap_tables_;
  LineList live_orders_, portfolio_rows_, portfolio_limits_;
  std::vector<Rid> live_rows_, live_held_rows_;
  std::string live_key_, live_targets_key_, live_held_key_;
};

}  // namespace tradelab::desktop::view
