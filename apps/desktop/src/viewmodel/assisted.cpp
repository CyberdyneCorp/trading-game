// AppViewModel: the tutorial shown before a mission's briefing (web Tutorial.svelte and
// session.needsTutorial / finishTutorial), assisted practice (web assisted.ts, AssistPanel.svelte and
// session.rewind / practiceNewSeed) and the debrief's chart (web Debrief.svelte <Chart>).
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "model/progress_json.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop {

namespace {

RunRecord* find_record(Progress& p, const std::string& id) {
  const auto it = std::ranges::find_if(p.runs, [&](const RunRecord& r) { return r.run_id == id; });
  return it == p.runs.end() ? nullptr : &*it;
}

}  // namespace

// ---- Tutorial ----

// A mission's tutorial opens by itself only the first time it starts on this device (never for packs).
void AppViewModel::enter_briefing() {
  const bool cards = shell_.run_pack.empty() && !game::tutorial_cards(game_->texts(), run_mission_).empty();
  shell_.tutorial_index = 0;
  screen_ = cards && !game_->tutorial_seen(run_mission_) ? Screen::Tutorial : Screen::Briefing;
}

void AppViewModel::tutorial_step(int delta) {
  if (screen_ != Screen::Tutorial) return;
  const int count = static_cast<int>(game::tutorial_cards(game_->texts(), run_mission_).size());
  const int next = std::max(0, shell_.tutorial_index + delta);
  if (next >= count) return tutorial_finish();
  shell_.tutorial_index = next;
  touch();
}

void AppViewModel::tutorial_finish() {
  if (screen_ != Screen::Tutorial) return;
  game_->mark_tutorial_seen(run_mission_);
  shell_.tutorial_index = 0;
  screen_ = Screen::Briefing;
  touch();
}

void AppViewModel::show_tutorial() {
  if (screen_ != Screen::Briefing || game::tutorial_cards(game_->texts(), run_mission_).empty()) return;
  shell_.tutorial_index = 0;
  screen_ = Screen::Tutorial;
  touch();
}

// ---- Assisted practice ----

// Outside a campaign mission in progress (assisted.ts assistAllowed), on a run this session knows.
bool AppViewModel::assist_allowed() const {
  if (!snap_ || !snap_->game || shell_.run_id.empty()) return false;
  return !(shell_.run_campaign && snap_->active());
}

std::optional<std::pair<int, int>> AppViewModel::rewind_range() const {
  if (!snap_ || snap_->step <= snap_->warmup_bars) return std::nullopt;
  return std::pair<int, int>{snap_->warmup_bars, snap_->step - 1};
}

void AppViewModel::set_assist_turn(std::string text) {
  if (text == shell_.assist_turn) return;
  shell_.assist_turn = std::move(text);
  touch();
}

// assisted.ts clampRewindTurn: the typed turn brought into the range; anything unreadable = the first.
int AppViewModel::assist_target_turn() const {
  const auto range = rewind_range();
  if (!range) return 1;
  const int last = range->second - range->first + 1;
  const double typed = fmt::parse_decimal(shell_.assist_turn);  // either decimal mark
  if (!std::isfinite(typed)) return 1;
  return static_cast<int>(std::clamp(std::round(typed), 1.0, static_cast<double>(last)));
}

// A new attempt flagged assisted, rebuilt from this run's actions before the chosen decision; the
// original attempt keeps its record (left, if it was still in play) and its journal entries before
// that decision come along.
void AppViewModel::rewind() {
  const auto range = rewind_range();
  if (waiting() || !assist_allowed() || !range) return;
  const int to = range->first + assist_target_turn() - 1;
  const AssistedLink link{shell_.run_id, "rewind", to};
  std::vector<PlanEntry> journal;
  if (const RunRecord* r = find_record(shell_.progress, shell_.run_id)) {
    std::ranges::copy_if(r->journal, std::back_inserter(journal), [to](const PlanEntry& e) { return e.step < to; });
  }
  leave_active_run();
  if (send([&] { engine_.rewind(to, RunLinks{{}, progress_json::assisted(link)}); })) {
    shell_.rewind_pending = true;
    shell_.pending_assisted = link;
    shell_.pending_journal = std::move(journal);
    notice_.clear();
  }
}

void AppViewModel::apply_rewind(const CommandResult& r) {
  if (!shell_.rewind_pending) return;
  shell_.rewind_pending = false;
  if (!r.accepted) {
    notice_ = reason_text(r.reason);
    shell_.pending_assisted.reset();
    shell_.pending_journal.clear();
    return;
  }
  shell_.run_campaign.reset();
  shell_.run_assisted = std::exchange(shell_.pending_assisted, std::nullopt);
  saved_run_.reset();
  record_started_run();
  screen_ = Screen::Game;
}

// The same mission on another market, without the advisor (packs have one market only).
void AppViewModel::practice_new_seed() {
  if (waiting() || !assist_allowed() || !shell_.run_pack.empty()) return;
  const AssistedLink link{shell_.run_id, "seed", std::nullopt};
  leave_active_run();
  RunOptions options = game_->options_for_new_run();
  options.advice_allowance = 0;
  const std::string mission = run_mission_;
  const RunLinks links{{}, progress_json::assisted(link)};
  if (send([&] { engine_.start_linked(mission, seeds_(), options, links); })) {
    saved_run_.reset();
    start_pending_ = true;
    shell_.run_campaign.reset();
    shell_.pending_assisted = link;
    shell_.pending_pack.clear();
    notice_.clear();
  }
}

// ---- Debrief chart ----

namespace {

// The trades' entries (info ring) and exits (attention ring) on the chart, joined by a dashed line in
// the trade's result colour.
void mark_trades(game::ChartScene& scene, const game::ChartViewModel& chart, const json::Value& trades) {
  const game::ChartViewModel::Frame& f = chart.frame();
  const auto x_of = [&f](long step) {
    const long i = step - static_cast<long>(f.first);
    return i < 0 ? -1.0f : std::round(static_cast<float>(i) * f.slot_w + f.slot_w / 2) + 0.5f;
  };
  const auto y_of = [&chart](double price) { return chart.y_of(price); };
  for (const json::Value& t : trades.items()) {
    const float x0 = x_of(static_cast<long>(t["entryStep"].as_int(-1)));
    const float y0 = y_of(t["entryAvg"].as_double());
    if (x0 >= 0) scene.rects.push_back({x0 - 5, y0 - 5, 10, 10, game::Ink::Info, 1, true});
    if (t["exitStep"].is_null()) continue;
    const float x1 = x_of(static_cast<long>(t["exitStep"].as_int(-1)));
    const float y1 = y_of(t["exitAvg"].as_double());
    if (x1 < 0) continue;
    scene.rects.push_back({x1 - 5, y1 - 5, 10, 10, game::Ink::Attention, 1, true});
    const game::Ink ink = t["net"].as_double() >= 0 ? game::Ink::Up : game::Ink::Down;
    if (x0 >= 0) scene.lines.push_back({x0, y0, x1, y1, ink, 0.8f, 1, 4, 3});
  }
}

}  // namespace

game::ChartScene AppViewModel::debrief_scene(float width, float height) {
  if (!snap_ || !snap_->game || !snap_->debrief) return {};
  game::ChartScene scene = debrief_chart_.build(game_->texts(), *snap_->game, std::nullopt, false, width, height);
  if (scene.empty) return scene;
  mark_trades(scene, debrief_chart_, snap_->debrief->raw["trades"]);
  if (debrief_moment_) game::mark_moment(scene, debrief_chart_, *debrief_moment_);
  return scene;
}

void AppViewModel::debrief_show_moment(int index) {
  const auto moments = screens::debrief_moments(*this);
  if (index < 0 || index >= static_cast<int>(moments.size())) {
    debrief_moment_.reset();
  } else {
    debrief_moment_ = moments[static_cast<std::size_t>(index)].step;
    // A moment before the visible window widens it to show the bar, with a few bars before it.
    if (snap_ && snap_->game && *debrief_moment_ < static_cast<int>(debrief_chart_.frame().first)) {
      debrief_chart_.show_from(*snap_->game, std::max(0, *debrief_moment_ - 5));
    }
  }
  touch();
}

void AppViewModel::debrief_page(int delta) {
  if (!snap_ || !snap_->game) return;
  debrief_chart_.page_by(*snap_->game, delta);
  touch();
}

game::BarPage AppViewModel::debrief_bar_page() const {
  if (!snap_ || !snap_->game) return {};
  return debrief_chart_.page(*snap_->game);
}

}  // namespace tradelab::desktop
