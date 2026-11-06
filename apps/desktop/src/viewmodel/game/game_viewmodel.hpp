// The trading terminal (web Terminal.svelte + the terminal half of session.svelte.ts): owns the
// panel viewmodels, sends one engine command at a time (a plan and its order, or a leverage change and
// its order, go as one sequence; advances pressed meanwhile wait and run afterwards, and never pass a
// calibration question), turns results into notices with their numbers, runs the dialogs, the
// remappable shortcuts and auto-advance. Plain C++: no Raven, unit-tested with a fake engine.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/engine_host.hpp"
#include "model/game_command.hpp"
#include "model/game_prefs.hpp"
#include "model/i18n.hpp"
#include "model/snapshot.hpp"
#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/dialogs_vm.hpp"
#include "viewmodel/game/dock_layout.hpp"
#include "viewmodel/game/options_vm.hpp"
#include "viewmodel/game/panels_vm.hpp"
#include "viewmodel/game/progress.hpp"
#include "viewmodel/game/shortcuts.hpp"
#include "viewmodel/game/texts.hpp"
#include "viewmodel/game/ticket_vm.hpp"

namespace tradelab::desktop::game {

enum class Modal : std::uint8_t { None, Plan, Calibration, ConfirmMove, Help, Journal, Pattern, Lesson, Tutorial, Perks };
enum class PickTarget : std::uint8_t { None, Stop, Target, ProtStop, ProtTarget };
enum class Tab : std::uint8_t { Perp, Options };

// A key press as the web names it (shortcuts.ts keyName): "Space", "Shift+Space", "b", "?", "Escape",
// "Enter", "ArrowLeft"... `typing`: a text field has the focus; `chord`: Ctrl, Alt or Super is held.
struct KeyPress {
  std::string name;
  bool typing = false;
  bool chord = false;
  bool chart_focused = false;
};

struct PerkRow {
  Perk id = Perk::Calendar;
  bool unlocked = false;
  bool enabled = false;
  std::string text;
};

class GameViewModel {
 public:
  // `prefs` may be null (no persistence: tests). `built_in` lists the built-in mission ids.
  GameViewModel(const Catalog& catalog, EngineCommands& engine, PrefsStore* prefs, std::vector<std::string> built_in);

  // Loads the shortcut map and progress from `store` and saves there from now on (null: none).
  void attach_prefs(PrefsStore* store);
  // Called with each plan the player records (AppViewModel keeps it in the run's journal).
  void set_plan_sink(std::function<void(const PlanEntry&)> sink) { plan_sink_ = std::move(sink); }

  [[nodiscard]] Texts texts() const { return Texts(*catalog_); }
  [[nodiscard]] std::uint64_t revision() const { return revision_ + chart.revision(); }
  [[nodiscard]] const GameState* state() const { return state_.get(); }
  [[nodiscard]] bool busy() const { return in_flight_ != InFlight::None || !chain_.empty(); }
  [[nodiscard]] const Line& notice() const { return notice_; }
  void set_notice(Line l);

  // ---- run lifecycle (AppViewModel) ----
  // Start options of a new free-play run: advisor allowance from the balance, every unlocked perk on.
  RunOptions options_for_new_run();
  void on_snapshot(const Snapshot& s);

  // ---- terminal commands ----
  void advance(int steps);
  void select_side(Side s);
  void submit();  // the perpetual ticket (Enter)
  void protect();
  void close_position();
  void cancel_order(int order_id);
  void adjust_margin(int sign);  // + add, - remove `margin_amount`
  void ask_advice();
  void apply_advice();
  void set_tab(Tab t);
  [[nodiscard]] Tab tab() const { return tab_; }
  void pick_option(int series, bool buy);
  void submit_option();
  void start_pick(PickTarget target);
  [[nodiscard]] PickTarget picking() const { return pick_; }

  // ---- chart ----
  void chart_action(const ChartAction& a);
  [[nodiscard]] ChartScene chart_scene(float width, float height);

  // ---- dialogs ----
  [[nodiscard]] Modal modal() const;
  void close_modal();
  void plan_confirm(bool skipped);
  void plan_cancel();
  void calibration_answer(bool skipped);
  void confirm_move(bool yes);
  void open_help();
  void open_journal();
  void open_patterns(std::optional<std::string> id);
  void open_lessons();
  void lesson_answer(int question);
  void lesson_skip(int question);
  void open_tutorial();
  void tutorial_step(int delta);  // past the last card closes it and marks it seen
  [[nodiscard]] bool tutorial_seen(const std::string& mission) const;
  void mark_tutorial_seen(const std::string& mission);
  // The run in play is an assisted practice attempt: it settles no points and finishes no mission.
  [[nodiscard]] bool assisted() const { return assisted_; }
  [[nodiscard]] int tutorial_index() const { return tutorial_index_; }
  void open_perks();
  void set_perk(Perk id, bool on);
  [[nodiscard]] std::vector<PerkRow> perk_rows() const;
  [[nodiscard]] bool perks_editable() const;
  [[nodiscard]] std::optional<std::string> pattern_id() const { return pattern_; }
  [[nodiscard]] const std::vector<PlanEntry>& journal() const { return journal_; }
  [[nodiscard]] const OrderDraft* pending_order() const { return pending_order_ ? &*pending_order_ : nullptr; }
  // A stop dragged past liquidation, waiting for the player's confirmation.
  struct PendingMove {
    int order_id = 0;
    std::string to;
  };
  [[nodiscard]] const PendingMove* pending_move() const { return pending_move_ ? &*pending_move_ : nullptr; }

  // ---- keyboard ----
  // Returns true when the key was used (the view then stops it).
  bool handle_key(const KeyPress& k);
  [[nodiscard]] const Keymap& keymap() const { return keymap_; }
  void reset_keymap();
  // A map remapped elsewhere (Settings): sanitized like a stored one, then saved with the prefs.
  void set_keymap(const std::map<std::string, std::string>& map);
  // Frame time for auto-advance.
  void tick(double dt);
  [[nodiscard]] bool muted() const { return prefs_.audio.muted; }
  void toggle_mute() { run_action(Action::Mute); }
  // Sound settings (Settings screen) and the terminal's panel layout, kept with the prefs.
  void set_audio(const AudioPrefs& a);
  void set_layout(std::string layout);
  // "Reset layout": forgets the player's arrangement; the default for the window shows again.
  void reset_layout();
  void toggle_auto() { run_action(Action::Auto); }

  // Panel viewmodels and dialog state, edited by the view (text fields) and read back.
  ChartViewModel chart;
  TicketViewModel ticket;
  OptionTicketViewModel option_ticket;
  PlanDialog plan;
  CalibrationDialog calibration;
  LessonDialog lessons;
  Remap remap;
  AutoPlay autoplay;
  DockLayout dock;
  std::string margin_amount = "100";
  void touch() { ++revision_; }

  [[nodiscard]] const SeriesNames& series_names() const { return names_; }
  [[nodiscard]] std::optional<double> planned_risk() const;
  [[nodiscard]] const GamePrefs& prefs() const { return prefs_; }
  [[nodiscard]] const std::string& run_id() const { return run_id_; }

 private:
  enum class InFlight : std::uint8_t { None, Advance, Game, Start };
  using Done = std::function<void(GameViewModel&, const CommandResult&)>;
  struct Step {
    GameCommand cmd;
    bool abort_on_reject = false;
    Done done;
  };

  void run(std::vector<Step> steps);
  void send_next();
  void flush_queue();
  void on_result(const CommandResult& r);
  void reset_run(const GameState& g);
  void on_run_end(const Snapshot& s);
  void save_prefs();
  void notify(const std::string& reason, const RejectionFacts& facts = {});
  void send_order(const OrderDraft& d);
  void send_option(const OptionDraft& d);
  void move_order(int order_id, double price);
  void picked(double price);
  bool modal_key(const KeyPress& k, std::optional<Action> action);
  bool escape();
  bool chart_key(const std::string& name);
  void run_action(Action a);
  [[nodiscard]] bool blocked_for_keys() const;
  [[nodiscard]] bool active() const { return state_ && state_->active(); }

  const Catalog* catalog_;
  EngineCommands& engine_;
  PrefsStore* prefs_file_;
  GamePrefs prefs_;
  std::vector<std::string> built_in_;
  Keymap keymap_;

  std::shared_ptr<const GameState> state_;
  std::string run_key_;  // mission + seed of the run on screen
  std::uint32_t seed_ = 0;
  RunOptions run_options_;
  RunOptions next_options_;
  std::string run_id_;
  bool run_settled_ = false;
  bool assisted_ = false;
  void begin_run(const Snapshot& s, bool rewound);
  int run_counter_ = 0;
  int actions_ = 0;  // non-lesson commands this run (perks can change only before the first)

  InFlight in_flight_ = InFlight::None;
  std::optional<Step> current_;
  std::deque<Step> chain_;
  int queued_ = 0;
  bool relaunching_ = false;

  Line notice_;
  Tab tab_ = Tab::Perp;
  PickTarget pick_ = PickTarget::None;
  Modal modal_ = Modal::None;
  std::optional<OrderDraft> pending_order_;
  std::optional<OptionDraft> pending_option_;
  std::optional<PendingMove> pending_move_;
  std::optional<std::string> pattern_;
  int tutorial_index_ = 0;
  std::vector<PlanEntry> journal_;
  std::function<void(const PlanEntry&)> plan_sink_;
  SeriesNames names_;
  std::uint64_t revision_ = 1;
};

}  // namespace tradelab::desktop::game
