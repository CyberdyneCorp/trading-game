#include "viewmodel/app_viewmodel.hpp"

#include <algorithm>
#include <utility>

#include "model/progress_json.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop {

AppViewModel::AppViewModel(Catalog catalog, std::vector<MissionInfo> missions, EngineCommands& engine,
                           SeedSource seeds)
    : catalog_(std::move(catalog)),
      missions_(missions_only(missions)),
      rounds_(round_markets(missions)),
      engine_(engine),
      seeds_(std::move(seeds)) {
  std::vector<std::string> ids;
  for (const auto& m : missions_) ids.push_back(m.id);
  game_ = std::make_unique<game::GameViewModel>(catalog_, engine_, nullptr, std::move(ids));
  game_->set_plan_sink([this](const PlanEntry& e) { add_journal_entry(e); });  // the Journal screen
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
  const RunLinks links{link ? progress_json::link(*link) : std::string(), {}};
  if (send([&] { engine_.start_linked(id, seeds_(), run, links); })) {
    saved_run_.reset();  // the new run replaces it as the store's active run
    start_pending_ = true;
    run_mission_ = id;
    shell_.run_campaign = std::move(link);
    shell_.pending_assisted.reset();
    shell_.pending_pack.clear();
    notice_.clear();
  }
}

void AppViewModel::open_terminal() {
  if (screen_ != Screen::Briefing || !snap_) return;
  screen_ = Screen::Game;
  touch();
}

void AppViewModel::back_to_menu() {
  if (viewing()) return leave_replay();
  if (screen_ == Screen::Catalog) pause_downloads();
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

bool AppViewModel::handle_key(const std::string& name, bool typing) {
  if (screen_ != Screen::Game || name.empty()) return false;
  return game_->handle_key({name, typing, false, false});
}

void AppViewModel::tick_audio(double dt) {
  const AudioPrefs& a = game_->prefs().audio;
  const audio::AudioSettings s{a.muted, static_cast<float>(a.music), static_cast<float>(a.sfx)};
  soundtrack_.update(screen_ == Screen::Game, game_->state(), game_->run_id(), s, dt);
}

void AppViewModel::set_sound_on(bool on) {
  AudioPrefs a = game_->prefs().audio;
  a.muted = !on;
  game_->set_audio(a);
}

void AppViewModel::set_music_volume(double v) {
  AudioPrefs a = game_->prefs().audio;
  a.music = std::clamp(v, 0.0, 1.0);
  game_->set_audio(a);
}

void AppViewModel::set_sfx_volume(double v) {
  AudioPrefs a = game_->prefs().audio;
  a.sfx = std::clamp(v, 0.0, 1.0);
  game_->set_audio(a);
}

// The same mission (or the same pack) again, as a new ordinary attempt.
void AppViewModel::play_again() {
  if (screen_ != Screen::Debrief || shell_.run_campaign) return;
  if (!shell_.run_pack.empty()) return start_pack(shell_.run_pack);
  screen_ = Screen::Menu;
  start_mission(run_mission_.empty() ? missions_.at(static_cast<std::size_t>(selected_)).id : run_mission_, {},
                std::nullopt);
}

void AppViewModel::on_snapshot(std::shared_ptr<const Snapshot> snap) {
  if (!snap) return;
  snap_ = std::move(snap);
  apply_results(*snap_);
  game_->on_snapshot(*snap_);
  if (screen_ == Screen::Game && !snap_->active() && snap_->debrief) {
    screen_ = Screen::Debrief;
    debrief_moment_.reset();
    if (snap_->game) debrief_chart_.show_from(*snap_->game, snap_->warmup_bars);
  }
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
  switch (r.kind) {
    case CommandKind::Start:
    case CommandKind::StartPack: run_started(r); break;
    case CommandKind::Resume: resumed(r); break;
    case CommandKind::Rewind: apply_rewind(r); break;
    case CommandKind::Watch: watch_result(r); break;
    default: break;
  }
}

// A run this viewmodel asked for has started (a mission, a pack, an assisted attempt on another
// market): recorded, then its tutorial (the first time) or its briefing.
void AppViewModel::run_started(const CommandResult& r) {
  if (!start_pending_) return;
  start_pending_ = false;
  if (!r.accepted) {
    notice_ = reason_text(r.reason);
    shell_.pending_assisted.reset();
    return;
  }
  shell_.run_files.viewing.reset();  // a new run leaves the replay viewer
  shell_.run_assisted = std::exchange(shell_.pending_assisted, std::nullopt);
  shell_.run_pack = std::exchange(shell_.pending_pack, {});
  if (!shell_.run_pack.empty() && snap_) run_mission_ = snap_->mission;
  record_started_run();
  enter_briefing();
}

// A built-in mission's text; a pack run has its label as title and the practice note as topic.
std::string AppViewModel::mission_text(const std::string& id, std::string_view field) const {
  const std::string key = "mission." + id + "." + std::string(field);
  if (catalog_.has(key)) return tr(key);
  if (field == "title") {
    const std::string label = pack_label_of(id);
    return label.empty() ? id : label;
  }
  return field == "topic" ? tr("menu.practiceNote") : std::string();
}

std::string AppViewModel::run_text(const Snapshot& s, std::string_view field) const {
  const std::string_view code = locale_code(locale());
  for (const PackText& t : s.pack_texts) {
    if (t.locale != code) continue;
    if (field == "title") return t.title;
    if (field == "tagline") return t.briefing;
  }
  return mission_text(s.mission, field);
}

std::string AppViewModel::reason_text(const std::string& code) const {
  if (code == "pack_missing") return tr("pack.missing");
  const std::string key = "reason." + code;
  return catalog_.has(key) ? tr(key) : code;
}

ChartGeometry AppViewModel::chart(float width, float height) const {
  return snap_ ? chart_geometry(snap_->bars, width, height) : ChartGeometry{};
}

}  // namespace tradelab::desktop
