// GameViewModel: dialogs, intel perks, tutorial, keyboard shortcuts and auto-advance
// (web Terminal.svelte onKey/modalKey/actions, HelpOverlay, ShortcutSettings, PerkPanel, Tutorial).
#include <algorithm>

#include "viewmodel/game/game_viewmodel.hpp"

namespace tradelab::desktop::game {

Modal GameViewModel::modal() const {
  // The calibration question shows whenever it is due, except over the plan of an order being placed.
  if (state_ && state_->active() && state_->calibration.due && modal_ != Modal::Plan) return Modal::Calibration;
  return modal_;
}

void GameViewModel::close_modal() {
  if (modal_ == Modal::Plan) return plan_cancel();
  if (modal_ == Modal::ConfirmMove) return confirm_move(false);
  if (modal_ == Modal::ConfirmCombo) return combo_close_confirm(false);
  if (modal_ == Modal::Tutorial) return tutorial_step(1'000);
  remap.waiting.reset();
  modal_ = Modal::None;
  touch();
}

void GameViewModel::open_help() {
  modal_ = Modal::Help;
  touch();
}

void GameViewModel::open_journal() {
  modal_ = Modal::Journal;
  touch();
}

void GameViewModel::open_patterns(std::optional<std::string> id) {
  if (!id && state_) {
    // The pattern on the focused or hovered bar, else the latest one detected.
    const auto bar = chart.focus_bar() ? chart.focus_bar() : chart.hover_bar(*state_);
    for (const auto& p : state_->indicators.patterns) {
      if (!bar || p.step == *bar) id = p.id;
    }
  }
  pattern_ = id;
  modal_ = Modal::Pattern;
  touch();
}

void GameViewModel::open_lessons() {
  modal_ = Modal::Lesson;
  touch();
}

void GameViewModel::lesson_answer(int question) {
  if (!state_ || busy()) return;
  const auto cmd = lessons.answer(state_->lesson, question);
  touch();
  if (!cmd) return;
  run({{*cmd, false, [question](GameViewModel& vm, const CommandResult& r) {
          if (r.accepted) vm.lessons.answered.push_back(question);
          else vm.notify(r.reason);
        }}});
}

void GameViewModel::lesson_skip(int question) {
  if (!state_ || busy()) return;
  run({{LessonDialog::skip(question), false, [question](GameViewModel& vm, const CommandResult& r) {
          if (r.accepted) vm.lessons.answered.push_back(question);
        }}});
}

void GameViewModel::open_tutorial() {
  if (!state_ || tutorial_cards(texts(), state_->mission).empty()) return;
  tutorial_index_ = 0;
  modal_ = Modal::Tutorial;
  touch();
}

void GameViewModel::tutorial_step(int delta) {
  if (!state_) return;
  const int count = static_cast<int>(tutorial_cards(texts(), state_->mission).size());
  tutorial_index_ = std::max(0, tutorial_index_ + delta);
  if (tutorial_index_ >= count) {
    tutorial_index_ = 0;
    modal_ = Modal::None;
    auto& seen = prefs_.tutorials_seen;
    if (std::find(seen.begin(), seen.end(), state_->mission) == seen.end()) seen.push_back(state_->mission);
    save_prefs();
  }
  touch();
}

bool GameViewModel::tutorial_seen(const std::string& mission) const {
  return std::find(prefs_.tutorials_seen.begin(), prefs_.tutorials_seen.end(), mission) != prefs_.tutorials_seen.end();
}

void GameViewModel::mark_tutorial_seen(const std::string& mission) {
  if (tutorial_seen(mission)) return;
  prefs_.tutorials_seen.push_back(mission);
  save_prefs();
}

// ---- intel perks ----

void GameViewModel::open_perks() {
  modal_ = Modal::Perks;
  // The perks this terminal explains count as introduced from now on.
  const auto unlocked = unlocked_perks(distinct_finished(prefs_.finished, built_in_));
  for (const Perk p : new_perks(unlocked, prefs_.perks_introduced)) {
    prefs_.perks_introduced.emplace_back(kPerks[static_cast<std::size_t>(p)].name);
  }
  save_prefs();
  touch();
}

bool GameViewModel::perks_editable() const {
  return state_ && state_->active() && !state_->known_scenario && actions_ == 0 && !busy();
}

std::vector<PerkRow> GameViewModel::perk_rows() const {
  const Texts t = texts();
  const int finished = distinct_finished(prefs_.finished, built_in_);
  const auto unlocked = unlocked_perks(finished);
  const auto used = perks_used(run_options_);
  std::vector<PerkRow> rows;
  for (const auto& p : kPerks) {
    const std::string name(p.name);
    PerkRow row{p.id, std::find(unlocked.begin(), unlocked.end(), p.id) != unlocked.end(),
                std::find(used.begin(), used.end(), p.id) != used.end(), {}};
    if (row.unlocked) {
      row.text = t.tr("perk." + name) + " \xE2\x80\x94 " + t.tr("perk." + name + ".desc");
    } else {
      row.text = "\xF0\x9F\x94\x92 " + t.tr("perk." + name) + " \xE2\x80\x94 " +
                 t.count("perk.locked", p.missions, {{"have", std::to_string(finished)}});
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

// Perks are start options: switching one restarts the same market (same seed) before the first action.
void GameViewModel::set_perk(Perk id, bool on) {
  if (!perks_editable()) return;
  const auto unlocked = unlocked_perks(distinct_finished(prefs_.finished, built_in_));
  if (std::find(unlocked.begin(), unlocked.end(), id) == unlocked.end()) return;
  auto enabled = perks_used(run_options_);
  std::erase(enabled, id);
  if (on) enabled.push_back(id);
  const RunOptions options = perk_options(enabled, run_options_.advice_allowance);
  run_options_ = options;
  relaunching_ = true;
  in_flight_ = InFlight::Start;
  engine_.start_with(state_->mission, seed_, options);
  touch();
}

// ---- keyboard ----

void GameViewModel::reset_keymap() {
  keymap_.reset();
  remap.message.clear();
  save_prefs();
  touch();
}

bool GameViewModel::blocked_for_keys() const {
  const Modal m = modal();
  return m == Modal::Plan || m == Modal::Calibration || m == Modal::ConfirmMove || m == Modal::Lesson ||
         m == Modal::Tutorial || m == Modal::Perks || m == Modal::Lab || m == Modal::ConfirmCombo || pick_ != PickTarget::None;
}

// The pattern card, the help overlay and the journal are modal: only their own key and Esc act, so the
// advance key cannot move the market behind them.
bool GameViewModel::modal_key(const KeyPress& k, std::optional<Action> action) {
  const auto closes = [&](Action own) { return k.name == "Escape" || action == own; };
  if (modal_ == Modal::Pattern) {
    if (closes(Action::Patterns)) close_modal();
  } else if (modal_ == Modal::Help) {
    if (closes(Action::Help)) close_modal();
  } else if (modal_ == Modal::Journal) {
    if (closes(Action::Journal)) close_modal();
  } else {
    return false;
  }
  return true;
}

void GameViewModel::run_action(Action a) {
  switch (a) {
    // While auto-advance runs, the advance keys take control back instead of advancing.
    case Action::Advance: autoplay.running ? autoplay.stop() : advance(1); break;
    case Action::Advance5: autoplay.running ? autoplay.stop() : advance(5); break;
    case Action::Auto: autoplay.toggle(!active()); break;
    case Action::Mute:
      prefs_.audio.muted = !prefs_.audio.muted;
      save_prefs();
      break;
    case Action::Buy: ticket.side = Side::Buy; break;
    case Action::Sell: ticket.side = Side::Sell; break;
    case Action::Close: close_position(); break;
    case Action::Help: modal_ = modal_ == Modal::Help ? Modal::None : Modal::Help; break;
    case Action::Patterns: open_patterns(std::nullopt); break;
    case Action::Options:
      if (state_ && state_->options.enabled) tab_ = tab_ == Tab::Options ? Tab::Perp : Tab::Options;
      break;
    case Action::Journal: modal_ = Modal::Journal; break;
  }
  touch();
}

// Esc cancels a drag or a pick first, then closes the dialog on top.
bool GameViewModel::escape() {
  if (chart.dragging() || pick_ != PickTarget::None) {
    chart_action(chart.escape(pick_ != PickTarget::None));
    return true;
  }
  const bool closes = modal_ == Modal::Plan || modal_ == Modal::ConfirmMove || modal_ == Modal::Lesson ||
                      modal_ == Modal::Tutorial || modal_ == Modal::Perks || modal_ == Modal::Lab || modal_ == Modal::ConfirmCombo;
  if (closes) close_modal();
  return closes;
}

// The focused chart keeps the navigation keys (per-candle focus) and Enter on a pattern.
bool GameViewModel::chart_key(const std::string& name) {
  chart_action(chart.key(*state_, name));
  return name.rfind("Arrow", 0) == 0 || name == "Home" || name == "End" || name.rfind("Page", 0) == 0;
}

bool GameViewModel::handle_key(const KeyPress& k) {
  if (remap.waiting) {
    const bool done = remap.key(texts(), keymap_, k.name);
    if (remap.message_ok) save_prefs();
    touch();
    return done;
  }
  if (k.name == "Escape" && escape()) return true;
  if (blocked_for_keys()) return false;
  const auto action = keymap_.action_for(k.name);
  if (modal_key(k, action)) return true;
  if (k.chart_focused && state_ && !k.typing && chart_key(k.name)) return true;
  if (k.typing || k.chord || !action) return false;
  run_action(*action);
  return true;
}

void GameViewModel::tick(double dt) {
  if (!autoplay.tick(dt)) return;
  if (!active()) return autoplay.stop(PauseReason::Finished);
  // Waits while any dialog is open, a pick is pending or a command is in flight.
  if (modal() != Modal::None || pick_ != PickTarget::None || busy()) return;
  advance(1);
}

}  // namespace tradelab::desktop::game
