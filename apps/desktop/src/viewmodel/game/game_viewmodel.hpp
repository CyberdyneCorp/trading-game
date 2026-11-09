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
#include "viewmodel/game/builder_vm.hpp"
#include "viewmodel/game/portfolio_vm.hpp"
#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/combos.hpp"
#include "viewmodel/game/dialogs_vm.hpp"
#include "viewmodel/game/dock_layout.hpp"
#include "viewmodel/game/options_vm.hpp"
#include "viewmodel/game/panels_vm.hpp"
#include "viewmodel/game/progress.hpp"
#include "viewmodel/game/shortcuts.hpp"
#include "viewmodel/game/texts.hpp"
#include "viewmodel/game/lab_vm.hpp"
#include "viewmodel/game/ticket_vm.hpp"

namespace tradelab::desktop::game {

enum class Modal : std::uint8_t {
  None,
  Plan,
  Calibration,
  ConfirmMove,
  Help,
  Journal,
  Pattern,
  Lesson,
  Tutorial,
  Perks,
  Lab,
  ConfirmCombo,
  Confirm,    // add-practice-and-advanced-strategies wave 1: an OCO link, a delta hedge, a roll or adjustment
  Portfolio   // D9: the portfolio panel
};
enum class PickTarget : std::uint8_t { None, Stop, Target, ProtStop, ProtTarget };
enum class Tab : std::uint8_t { Perp, Options, Builder };

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
  // The replay viewer plays a run file (add-run-sharing-and-journal-stats D6): it asks no question.
  [[nodiscard]] bool replay() const { return replay_; }
  [[nodiscard]] bool question_due() const { return state_ && state_->calibration.due && !replay_; }
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
  // Terminal.svelte: practice packs and assisted attempts start without the advisor, so it is not offered.
  [[nodiscard]] bool advisor_offered() const { return state_ && !state_->known_scenario && !assisted_; }
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

  // ---- strategy builder and what-if lab (game_combos.cpp; add-academy-and-strategies D3, D4) ----
  BuilderViewModel builder;
  LabViewModel lab;
  // The view edited `builder` (a template, a leg, the expiry): asks the engine for the new analysis.
  void builder_changed();
  // Opens the plan of the builder's combo; its legs are sent once the plan is recorded.
  void builder_submit();
  [[nodiscard]] BuilderView builder_view() const;
  // The terminal lab over the player's positions, or over the builder's legs.
  void open_lab_positions();
  void open_lab_builder();
  // A lab slider moved (the view set `lab.shift` through lab.slide): asks for the new valuation.
  void lab_changed();
  // The run's combos (from the action log's tags) and how their legs went.
  [[nodiscard]] std::vector<ComboRecord> combos() const;
  [[nodiscard]] std::vector<ComboStatus> combo_statuses() const;
  struct ComboNotice {
    std::string id, title, open_legs, cancelled_legs, risk, legging;
  };
  // The latest incomplete combo the player has not dismissed (ComboNotice.svelte).
  [[nodiscard]] std::optional<ComboNotice> combo_notice() const;
  void combo_keep();
  void combo_close();  // asks for a confirmation first
  void combo_close_confirm(bool yes);
  [[nodiscard]] std::string combo_close_text() const;
  // The journal line of a combo: its state and net premium so far (RunJournal's comboNote).
  [[nodiscard]] std::string combo_note(const std::string& id) const;

  // ---- add-practice-and-advanced-strategies wave 1 (game_wave1.cpp) ----
  // The planned trailing stop's notice (TrailNotice.svelte): on the first decision after the planned
  // entry filled, the rule and the engine's first trigger; "place" fills the protective ticket (the
  // trailing tab), nothing is sent from here.
  struct TrailNotice {
    std::string text, place, dismiss;
  };
  [[nodiscard]] std::optional<TrailNotice> trail_notice() const;
  void trail_notice_place();
  void trail_notice_dismiss();
  // Player-built OCO links (OrdersPanel.svelte): tick two working orders, "Link OCO", confirm.
  void toggle_link_pick(int order_id);
  [[nodiscard]] bool link_picked(int order_id) const;
  struct LinkView {
    bool shown = false;  // two working orders or more
    bool can_link = false;
    std::string button, hint;
  };
  [[nodiscard]] LinkView link_view() const;
  void link_oco();  // asks first
  // The confirmation dialog (Modal::Confirm) for a link, a hedge, a roll or an adjustment.
  struct ConfirmView {
    std::string title, text, yes, no;
    bool danger = false;
  };
  [[nodiscard]] ConfirmView confirm_view() const;
  void confirm_answer(bool yes);
  // The portfolio (D9): the engine's holdings what-if with its risk block, refreshed on every state.
  [[nodiscard]] const std::optional<WhatIfResult>& portfolio() const { return portfolio_; }
  [[nodiscard]] PortfolioView portfolio_view() const;
  [[nodiscard]] RailPortfolio rail_portfolio_view() const;
  void open_portfolio();
  void hedge_delta();  // asks first, then a normal perpetual ticket through the plan dialog
  // The P&L heatmap (D4) of the builder's legs, or of the holdings in the portfolio panel.
  [[nodiscard]] const std::optional<WhatIfHeatmap>& builder_heatmap() const { return builder_heatmap_; }
  [[nodiscard]] const std::optional<WhatIfHeatmap>& portfolio_heatmap() const { return portfolio_ ? portfolio_heatmap_ : no_heatmap_; }
  bool heatmap_table = false;
  std::size_t heatmap_focus_t = 0, heatmap_focus_p = 0;
  // Live combos (LiveCombos.svelte, D3): roll a combo's near expiry to a later one or adjust it (close a
  // leg, add a perpetual hedge), as one tagged submission under the combo's id.
  struct LiveCombo {
    std::string id, text;
  };
  [[nodiscard]] std::vector<LiveCombo> live_combos() const;
  void open_roll(const std::string& id);
  void open_adjust(const std::string& id);
  void live_close();
  void set_roll_target(int index);
  void set_roll_recenter(bool on);
  void set_roll_limit_ioc(bool on);
  void adjust_close_leg(std::size_t held_index);
  void adjust_add_hedge();
  struct LiveView {
    bool shown = false;  // a roll or an adjustment is being prepared
    std::string title, none;
    bool roll = false;
    std::vector<std::string> targets;
    int target_index = 0;
    std::string recenter, limit_ioc;
    bool recenter_on = false, limit_on = false;
    std::vector<std::string> held;  // adjust: one per held leg (a "close leg" button each)
    std::string close_leg, add_hedge, hedge_none;
    std::vector<Line> orders;  // the dry run, order by order
    std::vector<std::pair<std::string, std::string>> totals;
    std::string send;
    bool can_send = false;
  };
  [[nodiscard]] LiveView live_view() const;
  void live_send();  // asks first when a short would be left uncovered
  // Limit legs still working after their first open (ComboWorking.svelte): "Cancel working legs".
  struct WorkingCombo {
    std::string title, text, cancel;
  };
  [[nodiscard]] std::optional<WorkingCombo> working_combo() const;
  void cancel_working_legs();

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
  bool replay_ = false;  // the snapshot is the replay viewer's
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

  // Strategy builder combos (game_combos.cpp).
  struct ComboRun {
    ComboDraft draft;
    std::vector<int> accepted;  // order ids of the legs accepted so far
  };
  void plan_combo(const PlanEntry& e);
  void place_combo_leg(const std::shared_ptr<ComboRun>& run, std::size_t i);
  void withdraw_combo(const ComboRun& run, std::size_t refused, const std::string& reason);
  void reset_combos();
  void refresh_analysis();
  void request_lab();
  void request_combo_risk();
  // Replies of report-only engine calls reach this object only while it lives.
  [[nodiscard]] EngineCommands::AnalysisReply guarded(std::function<void(GameViewModel&, const std::string&)> fn);
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::shared_ptr<const std::vector<std::string>> log_;
  std::optional<ComboDraft> pending_combo_;
  std::vector<std::string> dismissed_;
  std::string risk_combo_;  // the incomplete combo whose risk was asked
  std::optional<WhatIfResult> combo_risk_;
  AnalysisTicket risk_ticket_;

  // Wave 1 (game_wave1.cpp).
  enum class ConfirmKind : std::uint8_t { None, LinkOco, Hedge, Uncovered };
  ConfirmKind confirm_ = ConfirmKind::None;
  std::vector<int> link_picks_;
  std::vector<int> trail_handled_;  // plan steps whose trailing notice was used or dismissed
  std::optional<WhatIfResult> portfolio_;
  std::optional<WhatIfHeatmap> portfolio_heatmap_;
  std::optional<WhatIfHeatmap> builder_heatmap_;
  std::optional<WhatIfHeatmap> no_heatmap_;
  AnalysisTicket portfolio_ticket_;
  AnalysisTicket heatmap_ticket_;
  AnalysisTicket live_ticket_;
  void refresh_wave1();
  void refresh_portfolio();
  void refresh_builder_heatmap();
  void refresh_live();
  [[nodiscard]] const PlanEntry* trail_plan() const;
  [[nodiscard]] std::optional<OrderLeg> hedge_now() const;
  // The live combo being rolled or adjusted.
  struct Live {
    std::string id;
    bool roll = true;
    int to = 0;
    bool recenter = false;
    bool limit_ioc = false;
    std::vector<OrderLeg> adjustment;
    bool adjust_close = true;
    std::optional<Analysis<ComboPreview>> preview;
    std::optional<WhatIfResult> before, after;
  };
  std::optional<Live> live_;
  [[nodiscard]] std::optional<ComboStatus> live_status() const;
  [[nodiscard]] std::vector<OrderLeg> live_orders() const;
  [[nodiscard]] std::optional<ComboTif> live_tif() const;
  [[nodiscard]] std::optional<std::string> live_uncovered() const;
  void send_live();
};

}  // namespace tradelab::desktop::game
