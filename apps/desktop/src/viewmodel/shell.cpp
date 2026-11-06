// AppViewModel's shell: the run's life as progress sees it (started, left, ended: completed missions,
// campaign legs, journal records), the campaign commands, the curriculum map, the journal filter, the
// Academy lesson runner and the settings. Ports the matching parts of the web's session.svelte.ts.
#include <tradelab/c_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "model/content.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/campaign.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/journal.hpp"

namespace tradelab::desktop {

CrtState effective_crt(const CrtSettings& s, bool reduce_motion) {
  if (!s.enabled || reduce_motion) return {};
  return CrtState{true, s.scanlines, s.bloom, s.noise};
}

float clamp_text_scale(float scale) {
  if (!std::isfinite(scale)) return kTextScaleMin;
  const float stepped = std::round(scale / kTextScaleStep) * kTextScaleStep;
  return std::clamp(stepped, kTextScaleMin, kTextScaleMax);
}

void AppViewModel::init_shell() {
  shell_.now = [] {
    using namespace std::chrono;
    return static_cast<std::int64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
  };
  shell_.settings.locale = catalog_.locale();
  shell_.keymap = default_keymap(web_content());
}

void AppViewModel::attach_repositories(ProgressRepository* progress, SettingsRepository* settings) {
  if (progress != nullptr) {
    shell_.progress_repo = progress;
    shell_.progress = progress->load();
  }
  if (settings != nullptr) {
    shell_.settings_repo = settings;
    if (auto stored = settings->load()) {
      shell_.settings = *stored;
      shell_.settings.text_scale = clamp_text_scale(stored->text_scale);
      catalog_.set_locale(stored->locale);
    }
    std::map<std::string, std::string> raw(shell_.settings.keymap.begin(), shell_.settings.keymap.end());
    shell_.keymap = sanitize_keymap(web_content(), raw);
  }
  touch();
}

void AppViewModel::set_clock(std::function<std::int64_t()> now_ms) { shell_.now = std::move(now_ms); }

void AppViewModel::save_progress() { shell_.progress_repo->save(shell_.progress); }

void AppViewModel::save_settings() {
  shell_.settings.keymap = std::map<std::string, std::string>(shell_.keymap.begin(), shell_.keymap.end());
  shell_.settings_repo->save(shell_.settings);
  game_->set_keymap(shell_.settings.keymap);  // the terminal reads the same shortcuts
}

// ---- The run's life ----

namespace {

RunRecord* find_run(Progress& p, const std::string& id) {
  const auto it = std::ranges::find_if(p.runs, [&](const RunRecord& r) { return r.run_id == id; });
  return it == p.runs.end() ? nullptr : &*it;
}

}  // namespace

void AppViewModel::record_started_run() {
  shell_.run_id = "run-" + std::to_string(++shell_.run_counter) + "-" + std::to_string(shell_.now());
  shell_.run_mission = run_mission_;
  shell_.run_settled = false;
  RunRecord r;
  r.run_id = shell_.run_id;
  r.mission_id = run_mission_;
  r.seed = snap_ ? snap_->seed : 0;
  r.created_at = shell_.now();
  r.campaign = shell_.run_campaign;
  r.equity = snap_ ? snap_->equity : std::string();
  shell_.progress.runs.push_back(std::move(r));
  save_progress();
}

// Starting another run replaces the one in play: it is stored as left, and a left campaign mission
// carries the equity it had (the campaign stays on that mission).
void AppViewModel::leave_active_run() {
  if (shell_.run_settled || shell_.run_id.empty() || !snap_ || !snap_->active()) return;
  shell_.run_settled = true;
  if (RunRecord* r = find_run(shell_.progress, shell_.run_id)) {
    r->status = RunStatus::Left;
    r->equity = snap_->equity;
  }
  if (shell_.run_campaign) {
    shell_.progress = settle_campaign_run(web_content(), shell_.progress, *shell_.run_campaign,
                                          RunOutcome{shell_.run_id, shell_.run_mission, LegResult::Left, snap_->equity, {}});
  }
  save_progress();
}

// The engine ended the run: a finished mission is completed; a campaign mission carries its final
// equity on (or ends the campaign run). Once per run.
void AppViewModel::settle_ended_run() {
  if (shell_.run_settled || shell_.run_id.empty() || !snap_ || !snap_->debrief) return;
  shell_.run_settled = true;
  const bool bankrupt = snap_->status == "bankrupt";
  const Debrief& d = *snap_->debrief;
  if (RunRecord* r = find_run(shell_.progress, shell_.run_id)) {
    r->status = bankrupt ? RunStatus::Bankrupt : RunStatus::Finished;
    r->equity = d.final_equity;
  }
  const bool known = snap_->state["knownScenario"].as_bool(false);
  if (!bankrupt && !known && !contains(shell_.progress.completed, shell_.run_mission)) {
    shell_.progress.completed.push_back(shell_.run_mission);
  }
  if (shell_.run_campaign) {
    RunOutcome out{shell_.run_id, shell_.run_mission, bankrupt ? LegResult::Bankrupt : LegResult::Finished,
                   d.final_equity, leg_report(d.raw, snap_->state["totals"])};
    shell_.progress = settle_campaign_run(web_content(), shell_.progress, *shell_.run_campaign, out);
  }
  save_progress();
}

bool AppViewModel::resumable() const {
  if (waiting()) return false;
  return saved_run_.has_value() || (snap_ && snap_->active() && !shell_.run_settled && !shell_.run_id.empty());
}

void AppViewModel::resume() {
  if (screen_ != Screen::Menu || !resumable()) return;
  if (!saved_run_) {
    screen_ = Screen::Game;
    touch();
    return;
  }
  // The engine replays the saved log (checkpoint + tail); the terminal opens when it answers.
  if (send([&] { engine_.resume(); })) {
    resume_pending_ = true;
    run_mission_ = saved_run_->mission;
    shell_.run_campaign.reset();
    notice_.clear();
  }
}

void AppViewModel::offer_saved_run(std::optional<SavedRun> run) {
  saved_run_ = std::move(run);
  touch();
}

// A run recorded under another engine C ABI: why it cannot resume (detail "<engine>/<abi>"), the web's
// engine.abiMismatch.
std::string AppViewModel::abi_refusal(const std::string& detail) const {
  const auto slash = detail.rfind('/');
  const std::string build = slash == std::string::npos ? detail : detail.substr(0, slash);
  const std::string abi = slash == std::string::npos ? std::string("?") : detail.substr(slash + 1);
  return tr("engine.abiMismatch", {{"build", build}, {"abi", abi}, {"expected", std::to_string(tl_abi_version())}});
}

// The answer to resume(): the run in play again (as a new session run of the shell), or the reason it
// could not be restored. Either way the offer is gone.
void AppViewModel::resumed(const CommandResult& r) {
  if (!resume_pending_) return;
  resume_pending_ = false;
  saved_run_.reset();
  if (!r.accepted || !snap_ || !snap_->active()) {
    notice_ = r.reason == "abi_changed" ? abi_refusal(snap_ ? snap_->resume_detail : std::string()) : reason_text(r.reason);
    return;
  }
  if (screen_ != Screen::Menu) return;
  record_started_run();
  screen_ = Screen::Game;
}

void AppViewModel::add_journal_entry(PlanEntry entry) {
  RunRecord* r = find_run(shell_.progress, shell_.run_id);
  if (r == nullptr) return;
  r->journal.push_back(std::move(entry));
  save_progress();
  touch();
}

// ---- Screens ----

void AppViewModel::open_curriculum() {
  shell_.confirming_end = false;
  screen_ = Screen::Curriculum;
  touch();
}

void AppViewModel::open_journal() {
  shell_.journal_filter = 0;
  screen_ = Screen::Journal;
  touch();
}

void AppViewModel::open_settings() {
  shell_.capturing_key = false;
  shell_.key_message.clear();
  // Remaps made in the terminal's help dialog show here too.
  shell_.keymap = sanitize_keymap(web_content(), game_->keymap().to_map());
  screen_ = Screen::Settings;
  touch();
}

// ---- Campaign (web CampaignPanel / CampaignOutcome and session.startCampaign...) ----

void AppViewModel::campaign_start() {
  if (waiting()) return;
  leave_active_run();
  const auto id = "camp-" + std::to_string(shell_.now()) + "-" + std::to_string(++shell_.run_counter);
  shell_.progress = start_campaign(web_content(), shell_.progress, id, shell_.now());
  shell_.confirming_end = false;
  save_progress();
  campaign_next();
}

// Resumes the campaign mission in play, or starts the campaign's next mission.
void AppViewModel::campaign_continue() {
  const auto& c = shell_.progress.campaign;
  if (c && resumable() && shell_.run_campaign && shell_.run_campaign->id == c->id &&
      shell_.run_campaign->index == c->index) {
    resume();
    return;
  }
  campaign_next();
}

void AppViewModel::campaign_next() {
  if (waiting()) return;
  if (screen_ == Screen::Debrief) screen_ = Screen::Menu;
  leave_active_run();  // may move the campaign (a left mission carries its equity)
  const WebContent& content = web_content();
  const auto& c = shell_.progress.campaign;
  const std::string mission = next_campaign_mission(content, c);
  if (mission.empty()) {
    touch();
    return;
  }
  RunOptions options;
  options.starting_balance = c->capital;
  if (auto cap = campaign_permissions(content, mission, shell_.progress.assessed).leverage_cap) options.leverage_cap = *cap;
  start_mission(mission, options, CampaignLink{c->id, c->index});
  touch();
}

void AppViewModel::campaign_end_ask() {
  shell_.confirming_end = true;
  touch();
}

// Ending a campaign run leaves its mission in play with it; passed stages stay.
void AppViewModel::campaign_end_answer(bool yes) {
  shell_.confirming_end = false;
  if (yes) {
    const auto& c = shell_.progress.campaign;
    if (c && shell_.run_campaign && shell_.run_campaign->id == c->id) leave_active_run();
    shell_.progress = abandon_campaign(web_content(), shell_.progress);
    save_progress();
  }
  touch();
}

// ---- Academy lessons ----

void AppViewModel::select_lesson(int index) {
  if (index == shell_.lesson_selected) return;
  shell_.lesson_selected = index;
  touch();
}

void AppViewModel::start_selected_lesson() {
  std::vector<std::string> ids;
  for (const auto& s : web_content().stages) ids.insert(ids.end(), s.lessons.begin(), s.lessons.end());
  if (shell_.lesson_selected < 0 || shell_.lesson_selected >= static_cast<int>(ids.size())) return;
  open_lesson(ids[static_cast<std::size_t>(shell_.lesson_selected)]);
}

void AppViewModel::open_lesson(const std::string& id) {
  shell_.lesson = LessonRunner(web_content().lesson(id));
  screen_ = Screen::Lesson;
  touch();
}

void AppViewModel::lesson_pick(int choice) {
  if (shell_.lesson.pick(choice)) touch();
}

void AppViewModel::lesson_next() {
  if (shell_.lesson.next()) touch();
}

void AppViewModel::lesson_prev() {
  if (shell_.lesson.prev()) touch();
}

void AppViewModel::lesson_leave() { open_curriculum(); }

// Completion is recorded in progress and passes the lesson's stage on the map.
void AppViewModel::lesson_finish() {
  const AcademyLesson* l = shell_.lesson.lesson();
  if (l == nullptr || !shell_.lesson.finished()) return;
  if (!contains(shell_.progress.lessons_done, l->id)) shell_.progress.lessons_done.push_back(l->id);
  save_progress();
  open_curriculum();
}

// ---- Journal ----

void AppViewModel::set_journal_filter(int index) {
  const int count = static_cast<int>(journal_missions(shell_.progress.runs).size());
  const int next = std::clamp(index, 0, count);
  if (next == shell_.journal_filter) return;
  shell_.journal_filter = next;
  touch();
}

// ---- Settings ----

void AppViewModel::set_crt(CrtSettings crt) {
  if (crt == shell_.settings.crt) return;
  shell_.settings.crt = crt;
  save_settings();
  touch();
}

void AppViewModel::set_text_scale(float scale) {
  const float next = clamp_text_scale(scale);
  if (next == shell_.settings.text_scale) return;
  shell_.settings.text_scale = next;
  save_settings();
  touch();
}

void AppViewModel::set_colorblind(bool on) {
  if (on == shell_.settings.colorblind) return;
  shell_.settings.colorblind = on;
  save_settings();
  touch();
}

void AppViewModel::set_reduce_motion(bool on) {
  if (on == shell_.reduce_motion) return;
  shell_.reduce_motion = on;
  touch();
}

void AppViewModel::select_key_row(int row) {
  const int count = static_cast<int>(web_content().shortcut_actions.size());
  if (row < 0 || row >= count || row == shell_.key_row) return;
  shell_.key_row = row;
  touch();
}

void AppViewModel::begin_key_change() {
  shell_.capturing_key = true;
  shell_.key_message.clear();
  touch();
}

namespace {

std::string action_label_key(const std::string& action) {
  return action == "mute" ? "help.sound" : "help." + action;  // ShortcutSettings ACTION_LABEL
}

}  // namespace

bool AppViewModel::capture_key(const std::string& key) {
  if (!shell_.capturing_key) return false;
  if (key.empty()) return true;  // a modifier alone: keep waiting
  shell_.capturing_key = false;
  touch();
  if (key == "Escape") return true;
  const WebContent& content = web_content();
  const std::string& action = content.shortcut_actions.at(static_cast<std::size_t>(shell_.key_row));
  const BindResult r = bind_key(content, shell_.keymap, action, key);
  const std::string label = key_label(key);
  shell_.key_message_ok = r.ok();
  if (r.ok()) {
    shell_.keymap = r.map;
    save_settings();
    shell_.key_message = tr("keys.saved", {{"action", tr(action_label_key(action))}, {"key", label}});
  } else if (r.error == BindResult::Error::Reserved) {
    shell_.key_message = tr("keys.reserved", {{"key", label}});
  } else {
    shell_.key_message = tr("keys.conflict", {{"key", label}, {"action", tr(action_label_key(r.other))}});
  }
  return true;
}

void AppViewModel::reset_keys() {
  shell_.keymap = default_keymap(web_content());
  shell_.capturing_key = false;
  shell_.key_message.clear();
  save_settings();
  touch();
}

}  // namespace tradelab::desktop
