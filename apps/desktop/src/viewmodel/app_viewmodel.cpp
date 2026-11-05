#include "viewmodel/app_viewmodel.hpp"

#include <utility>

#include "viewmodel/format.hpp"

namespace tradelab::desktop {

AppViewModel::AppViewModel(Catalog catalog, std::vector<MissionInfo> missions, EngineCommands& engine,
                           SeedSource seeds)
    : catalog_(std::move(catalog)), missions_(std::move(missions)), engine_(engine), seeds_(std::move(seeds)) {
  std::vector<std::string> ids;
  for (const auto& m : missions_) ids.push_back(m.id);
  game_ = std::make_unique<game::GameViewModel>(catalog_, engine_, nullptr, std::move(ids));
  init_shell();
}

void AppViewModel::set_locale(Locale locale) {
  catalog_.set_locale(locale);
  shell_.settings.locale = locale;
  save_settings();
  touch();
}

void AppViewModel::toggle_locale() { set_locale(locale() == Locale::PtBR ? Locale::En : Locale::PtBR); }

std::string AppViewModel::tr(std::string_view key, const Params& params) const { return catalog_.tr(key, params); }

std::vector<std::string> AppViewModel::mission_titles() const {
  std::vector<std::string> out;
  out.reserve(missions_.size());
  for (const auto& m : missions_) out.push_back(mission_text(m.id, "title"));
  return out;
}

void AppViewModel::select(int index) {
  if (index < 0 || index >= static_cast<int>(missions_.size()) || index == selected_) return;
  selected_ = index;
  touch();
}

// One command at a time: the next waits for the worker's acknowledgement (no queue of stale clicks).
bool AppViewModel::send(const std::function<void()>& command) {
  if (in_flight_ > 0) return false;
  ++in_flight_;
  command();
  touch();
  return true;
}

void AppViewModel::start_selected() {
  if (missions_.empty()) return;
  start_mission(missions_[static_cast<std::size_t>(selected_)].id, {}, std::nullopt);
}

// Every run carries the intel perks the player has unlocked (GameViewModel); a campaign mission adds
// its capital and leverage gate.
void AppViewModel::start_mission(const std::string& id, const RunOptions& options, std::optional<CampaignLink> link) {
  if (screen_ != Screen::Menu || in_flight_ > 0) return;
  leave_active_run();
  RunOptions run = game_->options_for_new_run();
  run.starting_balance = options.starting_balance;
  run.leverage_cap = options.leverage_cap;
  if (send([&] { engine_.start_with(id, seeds_(), run); })) {
    start_pending_ = true;
    run_mission_ = id;
    shell_.run_campaign = std::move(link);
    notice_.clear();
  }
}

void AppViewModel::open_terminal() {
  if (screen_ != Screen::Briefing || !snap_) return;
  screen_ = Screen::Game;
  touch();
}

void AppViewModel::back_to_menu() {
  screen_ = Screen::Menu;
  notice_.clear();
  touch();
}

void AppViewModel::set_qty_text(std::string text) {
  if (text == game_->ticket.amount()) return;
  game_->ticket.amount() = std::move(text);
  game_->touch();
}

// The terminal's commands live in game::GameViewModel; these keep the app-level entry points.
void AppViewModel::advance(int steps) {
  if (screen_ != Screen::Game) return;
  game_->advance(steps);
}

void AppViewModel::buy() { game_->select_side(game::Side::Buy); }

void AppViewModel::sell() { game_->select_side(game::Side::Sell); }

bool AppViewModel::handle_key(Key key, bool typing) {
  if (screen_ != Screen::Game || key == Key::Other) return false;
  return game_->handle_key({key == Key::ShiftSpace ? "Shift+Space" : "Space", typing, false, false});
}

void AppViewModel::play_again() {
  if (screen_ != Screen::Debrief || shell_.run_campaign) return;
  screen_ = Screen::Menu;
  start_mission(run_mission_.empty() ? missions_.at(static_cast<std::size_t>(selected_)).id : run_mission_, {},
                std::nullopt);
}

void AppViewModel::on_snapshot(std::shared_ptr<const Snapshot> snap) {
  if (!snap) return;
  snap_ = std::move(snap);
  apply_results(*snap_);
  game_->on_snapshot(*snap_);
  if (screen_ == Screen::Game && !snap_->active() && snap_->debrief) screen_ = Screen::Debrief;
  if (!snap_->active() && snap_->debrief) settle_ended_run();
  touch();
}

void AppViewModel::apply_results(const Snapshot& s) {
  for (const auto& r : s.results) {
    in_flight_ = in_flight_ > 0 ? in_flight_ - 1 : 0;
    apply_result(r);
  }
}

void AppViewModel::apply_result(const CommandResult& r) {
  if (r.kind == CommandKind::Start) {
    if (start_pending_ && r.accepted && screen_ == Screen::Menu) {
      screen_ = Screen::Briefing;
      record_started_run();
    }
    start_pending_ = false;
  }
}

std::string AppViewModel::mission_text(const std::string& id, std::string_view field) const {
  return tr("mission." + id + "." + std::string(field));
}

std::string AppViewModel::reason_text(const std::string& code) const {
  const std::string key = "reason." + code;
  return catalog_.has(key) ? tr(key) : code;
}

ChartGeometry AppViewModel::chart(float width, float height) const {
  return snap_ ? chart_geometry(snap_->bars, width, height) : ChartGeometry{};
}

}  // namespace tradelab::desktop
