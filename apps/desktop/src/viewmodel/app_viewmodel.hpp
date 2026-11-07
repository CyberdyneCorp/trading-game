// Application viewmodel: screen flow menu -> briefing -> terminal -> debrief (as the web's
// routes/+page.svelte), commands sent to the engine worker, and the localized texts of the named
// widgets of the current screen. Plain C++: no Raven, testable without a window.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/engine_host.hpp"
#include "model/i18n.hpp"
#include "model/packs/pack_service.hpp"
#include "model/snapshot.hpp"
#include "model/update/update_service.hpp"
#include "viewmodel/academy/ticket_sandbox.hpp"
#include "viewmodel/audio/audio_director.hpp"
#include "viewmodel/chart_geometry.hpp"
#include "viewmodel/game/game_viewmodel.hpp"
#include "viewmodel/pack_screens.hpp"
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
  Settings = 7,
  Tutorial = 8,  // a mission's tutorial cards, before its briefing (the first time only)
  Catalog = 9,
  Import = 10,
  Academy = 11,  // lessons by stage, status, retake, reviews due (add-academy-and-strategies D8)
  Glossary = 12,  // the glossary's search and terms (D6)
  Lab = 13  // the Academy's options lab (web LabScreen.svelte; viewmodel/academy_lab.cpp)
};

// A run left unfinished in an earlier launch (the active run of the store), offered by the menu: a
// mission (free play or a campaign mission), a pack run or an assisted attempt.
struct SavedRun {
  std::string mission;  // built-in mission id, or the pack's scenario id
  std::uint32_t seed = 0;
  std::string run_id;
  std::optional<CampaignLink> campaign;
  std::string pack_digest;
  std::optional<AssistedLink> assisted;
  std::string equity;  // last known equity (a left campaign mission carries it)
};

// The CSV wizard's text fields and choices (web CsvImport.svelte).
enum class ImportField : std::uint8_t { Path, Asset, Pair, Venue, Leverage, Label };
enum class ImportChoice : std::uint8_t { Interval, TimestampUnit, VolumeUnit, GapFill };

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
  // A key press by its web name ("Space", "Shift+Space", "b"...), through the terminal's remappable key
  // map. Returns true when the key was used. Shortcuts are ignored while the player types in a field.
  bool handle_key(const std::string& name, bool typing);

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

  // ---- Shell: menu panels, campaign, curriculum map, journal, Academy lessons, settings ----
  // (viewmodel/shell.cpp; the screens' texts in viewmodel/screens/).
  // Progress and settings come from repositories (in-memory until the SQLite store of task 1.9).
  void attach_repositories(ProgressRepository* progress, SettingsRepository* settings);
  void set_clock(std::function<std::int64_t()> now_ms);
  [[nodiscard]] const ShellState& shell() const { return shell_; }
  [[nodiscard]] const Progress& progress() const { return shell_.progress; }
  [[nodiscard]] const Settings& settings() const { return shell_.settings; }
  // Everything the current screen shows (texts, names, lines, tables, flags).
  [[nodiscard]] ScreenModel screen_model() const;

  // A run left from the terminal (still active in the engine), or one saved in an earlier launch
  // (offer_saved_run, before any other run starts), can be resumed from the menu.
  [[nodiscard]] bool resumable() const;
  void resume();
  void offer_saved_run(std::optional<SavedRun> run);
  [[nodiscard]] const std::optional<SavedRun>& saved_run() const { return saved_run_; }
  void open_curriculum();
  void open_journal();
  void open_settings();

  // ---- The Academy's what-if lab (viewmodel/academy_lab.cpp): an illustrative market, no run needed ----
  void open_lab();
  [[nodiscard]] game::LabViewModel& academy_lab() { return academy_lab_; }
  [[nodiscard]] const game::LabViewModel& academy_lab() const { return academy_lab_; }
  // The legs or a slider changed: the engine values them again (tl_option_whatif).
  void academy_lab_changed();
  // Each option leg's entry becomes the engine's model value now (no shift).
  void academy_lab_price_at_model();

  void campaign_start();
  void campaign_continue();
  void campaign_end_ask();
  void campaign_end_answer(bool yes);
  void campaign_next();  // debrief: the campaign's next mission

  void select_lesson(int index);
  void start_selected_lesson();
  void lesson_pick(int choice);
  void lesson_next();
  void lesson_prev();

  // ---- Academy, lesson widgets, glossary, review prompt, guided first trade (viewmodel/academy_flow.cpp) ----
  // A lesson opened from a screen (it returns there), as a full lesson or a spaced review.
  void open_lesson(const std::string& id, Screen from = Screen::Curriculum, LessonMode mode = LessonMode::Lesson);
  void lesson_leave();
  // Records the finished lesson (completed and scheduled, or the review moved) and returns.
  void lesson_finish();
  void lesson_set_typed(std::string text);
  void lesson_submit_number();
  void lesson_check_set();
  // Ticket sandbox edits (only the fields the step lets the player change).
  void sandbox_set_side(bool buy);
  enum class SandboxField : std::uint8_t { Qty, Leverage, Stop, Target };
  void sandbox_set_text(SandboxField field, std::string text);
  [[nodiscard]] std::optional<academy::SandboxOutputs> sandbox_outputs() const;

  void open_academy();
  void academy_select(int index);
  void academy_start_selected();  // start, or retake
  void academy_due_select(int index);
  void academy_review_selected();
  // Today's local calendar day (the review schedule uses the device clock, never a run's).
  [[nodiscard]] std::string today() const;
  // Lessons whose review is due today, in Academy order.
  [[nodiscard]] std::vector<std::string> reviews_due() const;
  [[nodiscard]] bool review_prompt_shown() const;
  void dismiss_review_prompt();
  // The debrief's recommended lesson ("" = none): the stage's first lesson not completed, else its first.
  [[nodiscard]] std::string debrief_lesson() const;
  void open_debrief_lesson();

  void open_glossary(std::optional<std::string> term = std::nullopt);
  void leave_glossary();
  void glossary_search(std::string query);
  void glossary_select(int index);  // a row of the search results
  void glossary_open(const std::string& id);
  [[nodiscard]] std::vector<std::string> glossary_hits() const;

  // Guided first trade (coach marks on the real terminal and its debrief).
  [[nodiscard]] const academy::CoachModel& coach() const { return shell_.academy.coach; }
  void coach_next();
  void coach_skip();
  void coach_replay();  // help overlay (starts now in the terminal) or briefing (when the terminal opens)
  // Once per frame: screen changes, a typed stop and an advance drive the tour (like tick_audio).
  void tick_coach();

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

  // ---- Tutorial before the briefing (web Tutorial.svelte; viewmodel/assisted.cpp) ----
  // Shown automatically the first time a mission with tutorial cards starts on this device.
  [[nodiscard]] int tutorial_index() const { return shell_.tutorial_index; }
  void tutorial_step(int delta);  // past the last card: on to the briefing
  void tutorial_finish();         // done or skipped: remembered as seen, then the briefing
  void show_tutorial();           // from the briefing (replay)

  // ---- Assisted practice (web assisted.ts + AssistPanel.svelte; viewmodel/assisted.cpp) ----
  [[nodiscard]] bool assist_allowed() const;
  // Decision steps the run can rewind to, [min, max]; none before the second decision.
  [[nodiscard]] std::optional<std::pair<int, int>> rewind_range() const;
  [[nodiscard]] const std::string& assist_turn() const { return shell_.assist_turn; }
  void set_assist_turn(std::string text);
  // The turn the rewind goes to (1 = first decision), brought into the range.
  [[nodiscard]] int assist_target_turn() const;
  void rewind();
  void practice_new_seed();
  [[nodiscard]] const std::optional<AssistedLink>& run_assisted() const { return shell_.run_assisted; }

  // ---- Debrief chart (the terminal's chart, read only, with the trades' entries and exits) ----
  [[nodiscard]] game::ChartScene debrief_scene(float width, float height);
  void debrief_page(int delta);
  [[nodiscard]] game::BarPage debrief_bar_page() const;

  // ---- Packs: catalog and CSV-import screens (viewmodel/packs.cpp) ----
  void attach_packs(packs::PackCommands* packs);
  void on_pack_event(const packs::PackEvent& e);  // UI thread
  [[nodiscard]] const PackScreens& packs() const { return packs_; }
  [[nodiscard]] bool packs_available() const { return pack_commands_ != nullptr; }
  void open_catalog();
  void catalog_select(int index);
  void catalog_download();  // the selected entry: download, or resume what arrived
  void catalog_pause();
  void catalog_play();
  void catalog_retry();
  void library_select(int index);
  void library_play();
  void library_remove();
  void start_pack(const std::string& digest);
  // The label of the pack a run plays ("" for a built-in mission).
  [[nodiscard]] std::string pack_label_of(const std::string& mission) const;
  [[nodiscard]] const std::string& run_pack() const { return shell_.run_pack; }

  void open_import();
  void import_set(ImportField field, std::string value);
  void import_choose(ImportChoice choice, int index);
  void import_open_file();  // reads the file at the typed path
  void import_validate();
  void import_back();
  void import_create();
  void import_cancel();

  // ---- Sound (viewmodel/audio): the web's soundtrack; mute and volumes live in the terminal's prefs ----
  void attach_audio(audio::AudioOut* out) { soundtrack_ = audio::Soundtrack(out); }
  // Once per frame: music by screen and danger, stings of trades that closed, fades.
  void tick_audio(double dt);
  [[nodiscard]] const audio::AudioDirector& audio() const { return soundtrack_.director(); }
  void set_sound_on(bool on);
  void set_music_volume(double v);
  void set_sfx_volume(double v);

  // ---- Updates (viewmodel/updates.cpp): offered in the menu only, installed only on the click ----
  void attach_updates(update::UpdateCommands* commands) { updates_ = commands; }
  void on_update(const update::UpdateState& state);  // UI thread
  void install_update();
  [[nodiscard]] const update::UpdateState& update_state() const { return update_; }

 private:
  void touch() { ++revision_; }
  bool send(const std::function<void()>& command);
  void apply_results(const Snapshot& s);
  void apply_result(const CommandResult& r);
  void resumed(const CommandResult& r);
  [[nodiscard]] std::string mission_text(const std::string& id, std::string_view field) const;
  // The run's title or tagline: a schema-v2 pack's own texts in the shown locale, else mission_text.
  [[nodiscard]] std::string run_text(const Snapshot& s, std::string_view field) const;
  [[nodiscard]] std::string reason_text(const std::string& code) const;
  [[nodiscard]] std::string abi_refusal(const std::string& detail) const;

  [[nodiscard]] std::vector<Binding> menu_bindings() const;
  [[nodiscard]] std::vector<Binding> briefing_bindings() const;
  [[nodiscard]] std::vector<Binding> game_bindings() const;
  [[nodiscard]] std::vector<Binding> debrief_bindings() const;
  [[nodiscard]] std::string account_line() const;
  [[nodiscard]] std::string position_text() const;
  [[nodiscard]] std::string game_notice() const;

  // Shell hooks into the run's life (shell.cpp).
  void init_shell();
  void start_mission(const std::string& id, const RunOptions& options, std::optional<CampaignLink> link);
  void leave_active_run();
  void leave_saved_run();
  void record_started_run();
  void settle_ended_run();
  void save_progress();
  void save_settings();
  void enter_briefing();
  void run_started(const CommandResult& r);
  void apply_rewind(const CommandResult& r);
  void apply_pack_result(const CommandResult& r);
  void library_event(const packs::PackEvent& e);
  void pause_downloads();
  void csv_report(const packs::PackEvent& e);
  void csv_imported(const packs::PackEvent& e);
  // Academy (academy_flow.cpp).
  void reset_sandbox();
  void mark_coach_done();
  void coach_watch_terminal();

  Catalog catalog_;
  std::vector<MissionInfo> missions_;
  EngineCommands& engine_;
  SeedSource seeds_;

  Screen screen_ = Screen::Menu;
  std::uint64_t revision_ = 1;
  int selected_ = 0;
  int in_flight_ = 0;
  bool start_pending_ = false;
  bool resume_pending_ = false;
  std::optional<SavedRun> saved_run_;
  std::string notice_;
  std::string run_mission_;
  std::shared_ptr<const Snapshot> snap_;
  std::unique_ptr<game::GameViewModel> game_;
  ShellState shell_;
  PackScreens packs_;
  packs::PackCommands* pack_commands_ = nullptr;
  game::ChartViewModel debrief_chart_;
  update::UpdateCommands* updates_ = nullptr;
  update::UpdateState update_;
  audio::Soundtrack soundtrack_;
  game::LabViewModel academy_lab_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // replies of report-only engine calls
};

}  // namespace tradelab::desktop
