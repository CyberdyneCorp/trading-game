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
#include "viewmodel/screen_model.hpp"
#include "viewmodel/shell_state.hpp"

namespace tradelab::desktop {

// Children of the view's root Switcher, in this order.
enum class Screen : std::uint8_t {
  Menu = 0,
  Briefing = 1,
  Game = 2,
  Debrief = 3,
  Curriculum = 4,
  Journal = 5,
  Lesson = 6,
  Settings = 7
};

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
  [[nodiscard]] std::uint64_t revision() const { return revision_; }
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
  [[nodiscard]] const std::string& qty_text() const { return qty_text_; }
  void set_qty_text(std::string text);
  void advance(int steps = 1);
  void buy();
  void sell();
  // Returns true when the key was used. Shortcuts are ignored while the player types in a field.
  bool handle_key(Key key, bool typing);

  // Debrief
  void play_again();

  // Called on the UI thread with each snapshot from the engine worker.
  void on_snapshot(std::shared_ptr<const Snapshot> snap);
  [[nodiscard]] const Snapshot* snapshot() const { return snap_.get(); }
  [[nodiscard]] bool waiting() const { return in_flight_ > 0; }
  [[nodiscard]] const std::string& notice() const { return notice_; }

  // Texts of the named widgets of the current screen (see widget_names.hpp).
  [[nodiscard]] std::vector<Binding> bindings() const;
  [[nodiscard]] ChartGeometry chart(float width, float height) const;

  // ---- Shell: menu panels, campaign, curriculum map, journal, Academy lessons, settings ----
  // (viewmodel/shell.cpp; the screens' texts in viewmodel/screens/).
  // Progress and settings come from repositories (in-memory until the SQLite store of task 1.9).
  void attach_repositories(ProgressRepository* progress, SettingsRepository* settings);
  void set_clock(std::function<std::int64_t()> now_ms);
  [[nodiscard]] const ShellState& shell() const { return shell_; }
  [[nodiscard]] const Progress& progress() const { return shell_.progress; }
  [[nodiscard]] const Settings& settings() const { return shell_.settings; }
  [[nodiscard]] const Catalog& catalog() const { return catalog_; }
  // Everything the current screen shows (texts, names, lines, tables, flags).
  [[nodiscard]] ScreenModel screen_model() const;

  // A run left from the terminal (still active in the engine) can be resumed from the menu.
  [[nodiscard]] bool resumable() const;
  void resume();
  void open_curriculum();
  void open_journal();
  void open_settings();

  void campaign_start();
  void campaign_continue();
  void campaign_end_ask();
  void campaign_end_answer(bool yes);
  void campaign_next();  // debrief: the campaign's next mission

  void select_lesson(int index);
  void start_selected_lesson();
  void open_lesson(const std::string& id);
  void lesson_pick(int choice);
  void lesson_next();
  void lesson_prev();
  void lesson_leave();
  void lesson_finish();

  void set_journal_filter(int index);
  // The terminal's plan dialog records the plan of an entry in the run's journal.
  void add_journal_entry(PlanEntry entry);

  void set_crt(CrtSettings crt);
  void set_text_scale(float scale);
  void set_colorblind(bool on);
  // The system asks for reduced motion: the CRT effects stay off.
  void set_reduce_motion(bool on);
  [[nodiscard]] CrtState crt_state() const { return effective_crt(shell_.settings.crt, shell_.reduce_motion); }
  void select_key_row(int row);
  void begin_key_change();
  // While a key change waits: the next key press (key_name()); "Escape" cancels. True when consumed.
  bool capture_key(const std::string& key);
  void reset_keys();

 private:
  void touch() { ++revision_; }
  bool send(const std::function<void()>& command);
  void send_order(Side side);
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

  // Shell hooks into the run's life (shell.cpp).
  void init_shell();
  void start_mission(const std::string& id, const StartOptions& options, std::optional<CampaignLink> link);
  void leave_active_run();
  void record_started_run();
  void settle_ended_run();
  void save_progress();
  void save_settings();

  Catalog catalog_;
  std::vector<MissionInfo> missions_;
  EngineCommands& engine_;
  SeedSource seeds_;

  Screen screen_ = Screen::Menu;
  std::uint64_t revision_ = 1;
  int selected_ = 0;
  int in_flight_ = 0;
  bool start_pending_ = false;
  std::string qty_text_ = "1";
  std::string notice_;
  std::string run_mission_;
  std::shared_ptr<const Snapshot> snap_;
  ShellState shell_;
};

}  // namespace tradelab::desktop
