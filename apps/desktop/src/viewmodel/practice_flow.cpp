// AppViewModel: the practice formats of add-practice-and-advanced-strategies (web DrillScreen.svelte,
// DailyScreen.svelte, GuessScreen.svelte, CoachReview.svelte, MistakeReport.svelte and the matching parts
// of session.svelte.ts). Daily drills (D15), the daily challenge (D16), coach mode (D17), mistake replay
// (D18) and guess rounds (D19). The rules are the ports in viewmodel/practice (web parity fixtures).
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <utility>

#include "model/content.hpp"
#include "model/progress_json.hpp"
#include "model/run_summary.hpp"
#include "viewmodel/academy/review.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop {

namespace {

RunRecord* find_record(Progress& p, const std::string& id) {
  const auto it = std::ranges::find_if(p.runs, [&](const RunRecord& r) { return r.run_id == id; });
  return it == p.runs.end() ? nullptr : &*it;
}

}  // namespace

void AppViewModel::open_drills() {
  if (shell_.practice.drills.phase == DrillsState::Phase::Done) shell_.practice.drills = DrillsState{};
  screen_ = Screen::Drills;
  touch();
}

void AppViewModel::open_daily() {
  screen_ = Screen::Daily;
  touch();
}

void AppViewModel::open_guess() {
  GuessState& g = shell_.practice.guess;
  if (g.phase != GuessState::Phase::Commit) g.phase = GuessState::Phase::Intro;
  screen_ = Screen::Guess;
  touch();
}

// ---- Daily drills ----

void AppViewModel::drills_set_timer(bool on) {
  if (shell_.settings.drill_timer == on) return;
  shell_.settings.drill_timer = on;
  save_settings();
  touch();
}

void AppViewModel::drills_start() {
  DrillsState& d = shell_.practice.drills;
  const Progress& p = shell_.progress;
  const std::string date = today();
  practice::SessionInput input{date, shell_.now(), practice::session_index(p.drill_history, date), p.lesson_reviews, p.drill_reviews,
                               p.drill_history};
  d = DrillsState{};
  d.items = practice::build_session(web_content(), input);
  d.results.assign(d.items.size(), std::nullopt);
  d.phase = DrillsState::Phase::Asking;
  d.started_ms = shell_.now();
  touch();
}

int AppViewModel::drills_seconds_left() const {
  const auto elapsed = (shell_.now() - shell_.practice.drills.started_ms) / 1000;
  return static_cast<int>(std::max<std::int64_t>(0, practice::kDrillSeconds - elapsed));
}

void AppViewModel::tick_practice() {
  DrillsState& d = shell_.practice.drills;
  if (screen_ != Screen::Drills || d.phase != DrillsState::Phase::Asking || !shell_.settings.drill_timer) return;
  const int left = drills_seconds_left();
  if (left == d.shown_seconds) return;
  d.shown_seconds = left;
  if (left == 0) {
    d.time_up = true;
    drills_finish();
  }
  touch();
}

// Once per item: a typed answer that is not a number asks again; otherwise right or wrong, then Next.
void AppViewModel::drills_check() {
  DrillsState& d = shell_.practice.drills;
  if (d.phase != DrillsState::Phase::Asking || d.index >= static_cast<int>(d.items.size())) return;
  if (d.feedback == DrillsState::Feedback::Right || d.feedback == DrillsState::Feedback::Wrong) return;
  const practice::DrillItem& item = d.items[static_cast<std::size_t>(d.index)];
  if (item.choice) return;
  const double given = practice::parse_answer(d.typed);
  if (!std::isfinite(given)) {
    d.feedback = DrillsState::Feedback::Invalid;
  } else {
    const bool right = practice::is_right(item, given);
    d.results[static_cast<std::size_t>(d.index)] = right ? practice::ItemResult::Right : practice::ItemResult::Wrong;
    d.feedback = right ? DrillsState::Feedback::Right : DrillsState::Feedback::Wrong;
  }
  touch();
}

void AppViewModel::drills_choose(int choice) {
  DrillsState& d = shell_.practice.drills;
  if (d.phase != DrillsState::Phase::Asking || d.index >= static_cast<int>(d.items.size())) return;
  if (d.feedback == DrillsState::Feedback::Right || d.feedback == DrillsState::Feedback::Wrong) return;
  const practice::DrillItem& item = d.items[static_cast<std::size_t>(d.index)];
  if (!item.choice) return;
  const bool right = practice::is_right(item, choice);
  d.results[static_cast<std::size_t>(d.index)] = right ? practice::ItemResult::Right : practice::ItemResult::Wrong;
  d.feedback = right ? DrillsState::Feedback::Right : DrillsState::Feedback::Wrong;
  touch();
}

void AppViewModel::drills_set_typed(std::string text) {
  DrillsState& d = shell_.practice.drills;
  if (text == d.typed) return;
  d.typed = std::move(text);
  if (d.feedback == DrillsState::Feedback::Invalid) d.feedback = DrillsState::Feedback::None;
  touch();
}

void AppViewModel::drills_next() {
  DrillsState& d = shell_.practice.drills;
  if (d.phase != DrillsState::Phase::Asking) return;
  if (d.feedback != DrillsState::Feedback::Right && d.feedback != DrillsState::Feedback::Wrong) return;
  if (d.index + 1 >= static_cast<int>(d.items.size())) return drills_finish();
  ++d.index;
  d.typed.clear();
  d.feedback = DrillsState::Feedback::None;
  touch();
}

// Unanswered items count as skipped; the session joins the drill schedule and history (never a lesson
// review).
void AppViewModel::drills_finish() {
  DrillsState& d = shell_.practice.drills;
  if (d.phase != DrillsState::Phase::Asking) return;
  std::vector<practice::ItemResult> results;
  for (auto& r : d.results) {
    if (!r) r = practice::ItemResult::Skipped;
    results.push_back(*r);
  }
  shell_.progress = practice::finish_session(std::move(shell_.progress), d.items, results, today());
  save_progress();
  d.phase = DrillsState::Phase::Done;
  touch();
}

void AppViewModel::drills_pick_lesson(int index) {
  DrillsState& d = shell_.practice.drills;
  if (index < 0 || index >= static_cast<int>(d.items.size()) || index == d.lesson) return;
  d.lesson = index;
  touch();
}

void AppViewModel::drills_open_lesson() {
  const DrillsState& d = shell_.practice.drills;
  if (d.phase != DrillsState::Phase::Done || d.lesson >= static_cast<int>(d.items.size())) return;
  const auto& lessons = d.items[static_cast<std::size_t>(d.lesson)].lessons;
  if (!lessons.empty() && web_content().lesson(lessons.front()) != nullptr) open_lesson(lessons.front(), Screen::Drills);
}

// ---- Daily challenge ----

practice::DailyLink AppViewModel::daily_today() const { return practice::daily_challenge(today()); }

// The mission's own rules: no advisor, no intel perks, the default balance, so attempts compare.
void AppViewModel::daily_play() {
  if (screen_ != Screen::Daily || in_flight_ > 0) return;
  const practice::DailyLink link = daily_today();
  leave_active_run();
  if (send([&] { engine_.start_linked(link.mission, link.seed, RunOptions{}, RunLinks{}); })) {
    saved_run_.reset();
    start_pending_ = true;
    run_mission_ = link.mission;
    shell_.run_campaign.reset();
    shell_.pending_assisted.reset();
    shell_.pending_pack.clear();
    shell_.practice.pending_daily = link;
    notice_.clear();
  }
}

// ---- The run's life (shell.cpp calls these) ----

// A run started (or resumed, or rewound): the daily challenge it attempts, and no coach review or
// mistake report of an earlier run.
void AppViewModel::practice_run_started() {
  PracticeState& p = shell_.practice;
  p.run_daily = std::exchange(p.pending_daily, std::nullopt);
  if (p.run_daily || !snap_ || shell_.run_assisted || !shell_.run_pack.empty()) {
    // a daily start, or a run that cannot be one
  } else if (const RunRecord* r = find_record(shell_.progress, shell_.run_id)) {
    // A run resumed from an earlier launch: the daily challenge of the day it started, when it is that.
    const practice::DailyLink day = practice::daily_challenge(academy::local_day(r->created_at));
    if (day.mission == r->mission_id && day.seed == r->seed) p.run_daily = day;
  }
  p.coach = CoachReviewState{};
  p.mistake.reset();
  game_->set_daily(p.run_daily.has_value());
}

// The run ended: a daily attempt joins the day's history; a mistake retry is compared with its original.
void AppViewModel::practice_run_ended() {
  const Debrief& d = *snap_->debrief;
  if (shell_.practice.run_daily && !shell_.run_assisted) {
    const DailyAttempt attempt{d.score_total < 0 ? std::nullopt : std::optional<int>(d.score_total), d.net_pnl, shell_.run_id};
    shell_.progress = practice::record_daily_attempt(std::move(shell_.progress), *shell_.practice.run_daily, attempt);
  }
  if (shell_.run_assisted && shell_.run_assisted->kind == "mistake") load_coach_review([this] { compare_mistake(); });
}

// ---- Coach review ----

void AppViewModel::load_coach_review(std::function<void()> then) {
  CoachReviewState& c = shell_.practice.coach;
  if (c.review && c.run_id == shell_.run_id) {
    if (then) then();
    return;
  }
  if (c.loading || !snap_ || snap_->active()) return;
  c.loading = true;
  const std::string run = shell_.run_id;
  engine_.analyze(AnalysisKind::Coach, "", [alive = std::weak_ptr<bool>(alive_), self = this, run, then = std::move(then)](const std::string& text) {
    if (alive.expired() || run != self->shell_.run_id) return;
    CoachReviewState& state = self->shell_.practice.coach;
    state.loading = false;
    const auto v = json::parse(text);
    state.review = practice::coach_review_from(v.value_or(json::Value{}));
    state.run_id = run;
    if (then) then();
    self->touch();
  });
}

void AppViewModel::open_coach_review() {
  if (screen_ != Screen::Debrief || !snap_ || snap_->active() || viewing()) return;
  shell_.practice.coach.index = 0;
  load_coach_review({});
  screen_ = Screen::CoachReview;
  touch();
}

const practice::CoachMoment* AppViewModel::coach_review_moment() const {
  const CoachReviewState& c = shell_.practice.coach;
  if (!c.review || c.index < 0 || c.index >= static_cast<int>(c.review->moments.size())) return nullptr;
  return &c.review->moments[static_cast<std::size_t>(c.index)];
}

void AppViewModel::coach_review_step(int delta) {
  CoachReviewState& c = shell_.practice.coach;
  if (screen_ != Screen::CoachReview || !c.review) return;
  const int last = static_cast<int>(c.review->moments.size());  // last = the end screen
  c.index = std::clamp(c.index + delta, 0, last);
  touch();
}

void AppViewModel::coach_review_close() {
  if (screen_ != Screen::CoachReview) return;
  screen_ = Screen::Debrief;
  touch();
}

// The last screen links the reveal: back to the debrief, whose reveal panel is the first row's third.
void AppViewModel::coach_review_to_reveal() { coach_review_close(); }

void AppViewModel::coach_review_retry() {
  const practice::CoachMoment* m = coach_review_moment();
  if (m == nullptr || !practice::retryable(*m)) return;
  const auto [step, code] = std::pair{m->step, m->code};
  retry_mistake(step, code);
}

void AppViewModel::coach_review_lesson() {
  const practice::CoachMoment* m = coach_review_moment();
  if (m != nullptr && web_content().lesson(m->lesson) != nullptr) open_lesson(m->lesson, Screen::CoachReview);
}

void AppViewModel::coach_review_term() {
  const practice::CoachMoment* m = coach_review_moment();
  if (m != nullptr && web_content().term(m->glossary) != nullptr) open_glossary(m->glossary);
}

// The run as of the moment's bar (practice::state_at): nothing after it is drawn.
game::ChartScene AppViewModel::coach_review_scene(float width, float height) {
  const practice::CoachMoment* m = coach_review_moment();
  if (m == nullptr || !snap_ || !snap_->game) return {};
  const GameState cut = practice::state_at(*snap_->game, m->step);
  game::ChartViewModel& chart = shell_.practice.coach.chart;
  game::ChartScene scene = chart.build(game_->texts(), cut, std::nullopt, false, width, height);
  if (!scene.empty) game::mark_moment(scene, chart, m->step);
  return scene;
}

// ---- Mistake replay ----

// Finished runs outside the replay viewer, never on imported runs or a campaign mission in progress.
bool AppViewModel::mistake_retry_allowed() const {
  return snap_ && snap_->game && !snap_->active() && !viewing() && !snap_->game->known_scenario && assist_allowed();
}

// An assisted attempt rebuilt from this run's actions before the decision at `step` (the host's rewind):
// back at that decision, before the mistaken action, without the advisor.
void AppViewModel::retry_mistake(int step, const std::string& code) {
  if (waiting() || !mistake_retry_allowed() || step < snap_->warmup_bars || step > snap_->step) return;
  const AssistedLink link{shell_.run_id, "mistake", step, code};
  std::vector<PlanEntry> journal;
  if (const RunRecord* r = find_record(shell_.progress, shell_.run_id)) {
    std::ranges::copy_if(r->journal, std::back_inserter(journal), [step](const PlanEntry& e) { return e.step < step; });
  }
  if (send([&] { engine_.rewind(step, RunLinks{{}, progress_json::assisted(link)}); })) {
    shell_.rewind_pending = true;
    shell_.pending_assisted = link;
    shell_.pending_journal = std::move(journal);
    notice_.clear();
  }
}

std::optional<std::pair<int, std::string>> AppViewModel::debrief_retry_target() const {
  if (!mistake_retry_allowed() || !snap_->debrief) return std::nullopt;
  int k = 0;
  for (const json::Value& f : snap_->debrief->raw["findings"].items()) {
    const int step = static_cast<int>(f["step"].as_int(-1));
    if (!practice::finding_retryable(f["tone"].as_string(), step, snap_->warmup_bars)) continue;
    if (k++ == shell_.practice.retry_pick) return std::pair{step, f["code"].as_string()};
  }
  return std::nullopt;
}

void AppViewModel::debrief_retry_pick(int index) {
  if (index < 0 || index == shell_.practice.retry_pick) return;
  shell_.practice.retry_pick = index;
  touch();
}

void AppViewModel::debrief_retry() {
  if (const auto target = debrief_retry_target()) retry_mistake(target->first, target->second);
}

// Repeated or avoided (the same code at a step in [step, step + 5]), and both results side by side.
void AppViewModel::compare_mistake() {
  if (!snap_ || !snap_->debrief || !shell_.run_assisted || !shell_.run_assisted->step) return;
  const AssistedLink& link = *shell_.run_assisted;
  const Debrief& d = *snap_->debrief;
  std::vector<practice::FindingRef> findings;
  for (const json::Value& f : d.raw["findings"].items()) findings.push_back({f["code"].as_string(), static_cast<int>(f["step"].as_int(-1))});
  const auto& c = shell_.practice.coach;
  const std::vector<practice::CoachMoment> none;
  const auto& moments = c.review ? c.review->moments : none;
  MistakeReport report{link.code, *link.step, practice::mistake_outcome(link.code, *link.step, moments, findings), std::nullopt,
                       MistakeResult{d.score_total < 0 ? std::nullopt : std::optional<std::int64_t>(d.score_total), d.net_pnl}};
  if (const RunRecord* original = find_record(shell_.progress, link.from); original != nullptr && !original->summary.empty()) {
    const auto v = json::parse(original->summary);
    if (const auto s = v ? summary_from_json(*v) : std::nullopt) report.original = MistakeResult{s->score, fmt::to_fixed_text(s->net, 2)};
  }
  shell_.practice.mistake = std::move(report);
  touch();
}

// ---- Guess rounds ----

namespace {

std::string round_request(const practice::RoundPick& p, int advance) {
  return R"({"mission":")" + p.market + R"(","seed":)" + std::to_string(p.seed) + R"(,"advance":)" + std::to_string(advance) + "}";
}

const MissionInfo* market_of(const std::vector<MissionInfo>& rounds, const std::string& id) {
  const auto it = std::ranges::find_if(rounds, [&](const MissionInfo& m) { return m.id == id; });
  return it == rounds.end() ? nullptr : &*it;
}

using practice::typed_decimal;

constexpr std::array<std::string_view, 3> kDecisions = {"long", "short", "none"};

}  // namespace

void AppViewModel::guess_start() {
  GuessState& g = shell_.practice.guess;
  if (rounds_.empty() || g.phase == GuessState::Phase::Loading) return;
  const practice::RoundPick pick = practice::pick_round(rounds_, today(), static_cast<int>(shell_.progress.guess_history.size()));
  const MissionInfo* m = market_of(rounds_, pick.market);
  g.pick = pick;
  g.phase = GuessState::Phase::Loading;
  g.outcome.reset();
  g.revealed.reset();
  g.invalidation.clear();
  g.target.clear();
  g.problem = false;
  g.chart = game::ChartViewModel{};
  const std::uint64_t request = ++g.request;
  engine_.analyze(AnalysisKind::Round, round_request(pick, pick.step - m->warmup_bars), [alive = std::weak_ptr<bool>(alive_), self = this, request](const std::string& text) {
    if (!alive.expired()) self->guess_reply(request, false, text);
  });
  touch();
}

void AppViewModel::guess_reply(std::uint64_t request, bool reveal, const std::string& text) {
  GuessState& g = shell_.practice.guess;
  if (request != g.request) return;
  const auto v = json::parse(text);
  if (!v || v->has("error") || !g.pick) {
    g.phase = GuessState::Phase::Error;
  } else if (reveal) {
    guess_revealed(*v);
  } else {
    g.at_decision_raw = *v;
    g.at_decision = std::make_shared<const GameState>(game_state_from_json(*v));
    g.phase = GuessState::Phase::Commit;
  }
  touch();
}

void AppViewModel::guess_set_decision(int index) {
  GuessState& g = shell_.practice.guess;
  if (index < 0 || index >= static_cast<int>(kDecisions.size()) || index == g.decision) return;
  g.decision = index;
  touch();
}

void AppViewModel::guess_set_p(int index) {
  const auto ps = practice::probabilities();
  if (index < 0 || index >= static_cast<int>(ps.size()) || ps[static_cast<std::size_t>(index)] == shell_.practice.guess.p) return;
  shell_.practice.guess.p = ps[static_cast<std::size_t>(index)];
  touch();
}

void AppViewModel::guess_set_level(bool target, std::string text) {
  std::string& field = target ? shell_.practice.guess.target : shell_.practice.guess.invalidation;
  if (field == text) return;
  field = std::move(text);
  touch();
}

// A trade needs both levels; then the next 12 bars are asked for (the same run, replayed further).
void AppViewModel::guess_commit() {
  GuessState& g = shell_.practice.guess;
  if (g.phase != GuessState::Phase::Commit || !g.pick) return;
  const bool trade = g.decision != 2;
  g.problem = trade && (!practice::fixed(typed_decimal(g.invalidation)) || !practice::fixed(typed_decimal(g.target)));
  if (g.problem) return touch();
  const MissionInfo* m = market_of(rounds_, g.pick->market);
  g.phase = GuessState::Phase::Loading;
  const std::uint64_t request = ++g.request;
  const int advance = g.pick->step - m->warmup_bars + practice::kGuessHorizon;
  engine_.analyze(AnalysisKind::Round, round_request(*g.pick, advance), [alive = std::weak_ptr<bool>(alive_), self = this, request](const std::string& text) {
    if (!alive.expired()) self->guess_reply(request, true, text);
  });
  touch();
}

// The reveal: the close 12 bars later against the decision close, the score (never the P&L), which level
// the next bars reached first, and the round kept in the history.
void AppViewModel::guess_revealed(const json::Value& state) {
  GuessState& g = shell_.practice.guess;
  const auto step = static_cast<std::size_t>(g.pick->step);
  const std::string close = g.at_decision_raw["bars"][step][3].as_string();
  const std::string atr = g.at_decision_raw["indicators"]["atr14"][step].as_string("0");
  const std::string after = state["bars"][step + practice::kGuessHorizon][3].as_string();
  const bool higher = practice::fixed(after).value_or(0) > practice::fixed(close).value_or(0);
  const bool trade = g.decision != 2;
  const practice::Commitment c{std::string(kDecisions[static_cast<std::size_t>(g.decision)]), g.p,
                               trade ? typed_decimal(g.invalidation) : std::string(), trade ? typed_decimal(g.target) : std::string()};
  std::vector<practice::HighLow> next;
  for (std::size_t i = step + 1; i <= step + practice::kGuessHorizon; ++i) {
    next.push_back({state["bars"][i][1].as_string(), state["bars"][i][2].as_string()});
  }
  const practice::GuessScore score = practice::score_round(c, {close, atr, higher});
  g.outcome = GuessOutcome{score, practice::first_hit(c, next), higher, close, after};
  g.revealed = std::make_shared<const GameState>(game_state_from_json(state));
  g.phase = GuessState::Phase::Reveal;
  auto& history = shell_.progress.guess_history;
  history.push_back({g.pick->market, g.pick->seed, g.pick->step, c.decision, g.p, score.total, higher});
  if (history.size() > kGuessHistoryKeep) history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(kGuessHistoryKeep));
  save_progress();
}

// The chart up to the decision; after the reveal, the next 12 bars with the decision bar marked.
game::ChartScene AppViewModel::guess_scene(float width, float height) {
  GuessState& g = shell_.practice.guess;
  const bool reveal = g.phase == GuessState::Phase::Reveal && g.revealed;
  const GameState* s = reveal ? g.revealed.get() : g.at_decision.get();
  if (s == nullptr || !g.pick || (g.phase != GuessState::Phase::Commit && !reveal)) return {};
  game::ChartScene scene = g.chart.build(game_->texts(), *s, std::nullopt, false, width, height);
  if (reveal && !scene.empty) game::mark_moment(scene, g.chart, g.pick->step);
  return scene;
}

}  // namespace tradelab::desktop
