// Application viewmodel: screen flow menu -> briefing -> terminal -> debrief (as the web's
// routes/+page.svelte), commands sent to the engine worker, and the localized texts of the named
// widgets of the current screen. Plain C++: no Raven, testable without a window.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "model/engine_host.hpp"
#include "model/i18n.hpp"
#include "model/snapshot.hpp"
#include "viewmodel/chart_geometry.hpp"
#include "viewmodel/game/game_viewmodel.hpp"

namespace tradelab::desktop {

enum class Screen : std::uint8_t { Menu = 0, Briefing = 1, Game = 2, Debrief = 3 };

// Keys the terminal reacts to (web defaults, shortcuts.ts): Space advances one bar, Shift+Space five.
enum class Key : std::uint8_t { Space, ShiftSpace, Other };

struct Binding {
  std::string_view widget;
  std::string text;
};

class AppViewModel {
 public:
  using SeedSource = std::function<std::uint32_t()>;

  AppViewModel(Catalog catalog, std::vector<MissionInfo> missions, EngineCommands& engine, SeedSource seeds);

  [[nodiscard]] Screen screen() const { return screen_; }
  // Increases with every change the view must show.
  [[nodiscard]] std::uint64_t revision() const { return revision_ + game_->revision(); }
  [[nodiscard]] Locale locale() const { return catalog_.locale(); }
  void set_locale(Locale locale);
  void toggle_locale();
  [[nodiscard]] std::string tr(std::string_view key, const Params& params = {}) const;

  // Menu
  [[nodiscard]] const std::vector<MissionInfo>& missions() const { return missions_; }
  [[nodiscard]] std::vector<std::string> mission_titles() const;
  [[nodiscard]] int selected() const { return selected_; }
  void select(int index);
  void start_selected();

  // Briefing
  void open_terminal();
  void back_to_menu();

  // Terminal
  // The ticket's amount field of its sizing mode (the full terminal is game(): viewmodel/game).
  [[nodiscard]] const std::string& qty_text() const { return game_->ticket.amount(); }
  void set_qty_text(std::string text);
  void advance(int steps = 1);
  void buy();   // ticket side: buy
  void sell();  // ticket side: sell
  [[nodiscard]] game::GameViewModel& game() { return *game_; }
  [[nodiscard]] const game::GameViewModel& game() const { return *game_; }
  [[nodiscard]] const Catalog& catalog() const { return catalog_; }
  // Returns true when the key was used. Shortcuts are ignored while the player types in a field.
  bool handle_key(Key key, bool typing);

  // Debrief
  void play_again();

  // Called on the UI thread with each snapshot from the engine worker.
  void on_snapshot(std::shared_ptr<const Snapshot> snap);
  [[nodiscard]] const Snapshot* snapshot() const { return snap_.get(); }
  [[nodiscard]] bool waiting() const { return in_flight_ > 0 || game_->busy(); }
  [[nodiscard]] const std::string& notice() const { return notice_; }

  // Texts of the named widgets of the current screen (see widget_names.hpp).
  [[nodiscard]] std::vector<Binding> bindings() const;
  [[nodiscard]] ChartGeometry chart(float width, float height) const;

 private:
  void touch() { ++revision_; }
  bool send(const std::function<void()>& command);
  void apply_results(const Snapshot& s);
  void apply_result(const CommandResult& r);
  [[nodiscard]] std::string mission_text(const std::string& id, std::string_view field) const;
  [[nodiscard]] std::string reason_text(const std::string& code) const;

  [[nodiscard]] std::vector<Binding> menu_bindings() const;
  [[nodiscard]] std::vector<Binding> briefing_bindings() const;
  [[nodiscard]] std::vector<Binding> game_bindings() const;
  [[nodiscard]] std::vector<Binding> debrief_bindings() const;
  [[nodiscard]] std::string account_line() const;
  [[nodiscard]] std::string position_text() const;
  [[nodiscard]] std::string game_notice() const;

  Catalog catalog_;
  std::vector<MissionInfo> missions_;
  EngineCommands& engine_;
  SeedSource seeds_;

  Screen screen_ = Screen::Menu;
  std::uint64_t revision_ = 1;
  int selected_ = 0;
  int in_flight_ = 0;
  bool start_pending_ = false;
  std::string notice_;
  std::string run_mission_;
  std::shared_ptr<const Snapshot> snap_;
  std::unique_ptr<game::GameViewModel> game_;
};

}  // namespace tradelab::desktop
