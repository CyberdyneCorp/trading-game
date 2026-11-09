// The practice formats' screens (add-practice-and-advanced-strategies D15-D19): DrillScreen.svelte,
// DailyScreen.svelte, GuessScreen.svelte and CoachReview.svelte, plus their parts of the menu
// (MissionMenu.svelte practice-formats), the debrief (the coach button, the daily tag, "Retry from here"
// and MistakeReport.svelte) and the terminal (the mistake and daily banners). Numbers come from the
// ports in viewmodel/practice, formatted as the web formats them.
#include <algorithm>
#include <cstdio>

#include "model/content.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/practice_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

constexpr std::string_view kDash = "\xE2\x80\x94";  // —

std::string price(const AppViewModel& vm, const std::string& decimal) { return fmt::fmt_price(decimal, vm.locale()); }

std::string lesson_title(const AppViewModel& vm, const std::string& id) { return vm.tr("academyLesson." + id + ".title"); }

// ---- menu ----

void practice_menu_row(const AppViewModel& vm, ScreenModel& m) {
  m.text(names::kMenuPracticeTitle, vm.tr("menu.practiceFormats"));
  m.text(names::kMenuPracticeNote, vm.tr("menu.practiceFormatsNote"));
  m.text(names::kMenuDrills, "\xE2\x9C\x8E " + vm.tr("drills.open"));  // ✎
  m.text(names::kMenuDaily, "\xE2\x98\x80 " + vm.tr("daily.open"));    // ☀
  m.text(names::kMenuGuess, "? " + vm.tr("guess.open"));
}

// ---- drills ----

std::string clock_text(const AppViewModel& vm) {
  if (!vm.settings().drill_timer) return vm.tr("drills.noClock");
  const int left = vm.drills_seconds_left();
  char buf[16];
  std::snprintf(buf, sizeof buf, "%d:%02d", left / 60, left % 60);
  return vm.tr("drills.timeLeft", {{"time", buf}});
}

void drill_feedback(const AppViewModel& vm, const DrillsState& d, const practice::DrillText& text, ScreenModel& m) {
  using F = DrillsState::Feedback;
  const bool answered = d.feedback == F::Right || d.feedback == F::Wrong;
  std::vector<Line> lines;
  if (d.feedback == F::Invalid) lines.push_back({vm.tr("drills.invalid"), Tone::Attention});
  if (d.feedback == F::Right) lines.push_back({vm.tr("drills.right"), Tone::Up});
  if (d.feedback == F::Wrong) lines.push_back({vm.tr("drills.wrong", {{"answer", text.answer}}), Tone::Down});
  m.lines.push_back({names::kDrillsFeedback, std::move(lines)});
  m.show(names::kDrillsExplain, d.feedback == F::Wrong);
  m.text(names::kDrillsExplain, text.explain);
  m.show(names::kDrillsNext, answered);
  m.text(names::kDrillsNext, vm.tr(d.index + 1 >= static_cast<int>(d.items.size()) ? "drills.finish" : "drills.next"));
  m.enable(names::kDrillsCheck, !answered);
  for (const auto w : names::kDrillsChoice) m.enable(w, !answered);
}

void drill_item(const AppViewModel& vm, const DrillsState& d, ScreenModel& m) {
  const practice::DrillItem& item = d.items[static_cast<std::size_t>(d.index)];
  const DrillTemplate* t = practice::drill_template(web_content(), item.template_id);
  if (t == nullptr) return;
  const practice::DrillText text = practice::item_text(item, *t, vm.catalog());
  m.text(names::kDrillsPosition, vm.tr("drills.item", {{"n", std::to_string(d.index + 1)}, {"total", std::to_string(d.items.size())}}) +
                                     std::string(kSep) + vm.tr("drills.skill." + item.skill));
  m.text(names::kDrillsClock, clock_text(vm));
  m.text(names::kDrillsPrompt, text.prompt);
  m.show(names::kDrillsChoices, item.choice);
  for (std::size_t k = 0; k < names::kDrillsChoice.size(); ++k) {
    m.show(names::kDrillsChoice[k], item.choice && k < text.choices.size());
    if (k < text.choices.size()) m.text(names::kDrillsChoice[k], text.choices[k]);
  }
  m.show(names::kDrillsInputRow, !item.choice);
  m.text(names::kDrillsAnswerLabel, vm.tr("drills.answer"));
  m.text(names::kDrillsInput, d.typed);
  m.name(names::kDrillsInput, vm.tr("drills.answer"));
  m.text(names::kDrillsCheck, vm.tr("drills.check"));
  drill_feedback(vm, d, text, m);
}

std::pair<std::string, Tone> result_tag(const AppViewModel& vm, const std::optional<practice::ItemResult>& r) {
  if (r == practice::ItemResult::Right) return {vm.tr("drills.result.right"), Tone::Up};
  if (r == practice::ItemResult::Wrong) return {vm.tr("drills.result.wrong"), Tone::Down};
  return {vm.tr("drills.result.skipped"), Tone::Muted};
}

void drill_end(const AppViewModel& vm, const DrillsState& d, ScreenModel& m) {
  m.text(names::kDrillsEndTitle, vm.tr("drills.endTitle"));
  m.show(names::kDrillsTimeUp, d.time_up);
  m.text(names::kDrillsTimeUp, vm.tr("drills.timeUp"));
  const auto right = std::ranges::count(d.results, std::optional(practice::ItemResult::Right));
  m.text(names::kDrillsFirstTry, vm.tr("drills.firstTry", {{"n", std::to_string(right)}, {"total", std::to_string(d.items.size())}}));
  WidgetLines lines{names::kDrillsResults, {}};
  WidgetItems lessons{names::kDrillsLessons, {}, d.lesson};
  for (std::size_t k = 0; k < d.items.size(); ++k) {
    const practice::DrillItem& it = d.items[k];
    const DrillTemplate* t = practice::drill_template(web_content(), it.template_id);
    const auto [tag, tone] = result_tag(vm, d.results[k]);
    const std::string prompt = t != nullptr ? practice::item_text(it, *t, vm.catalog()).prompt : it.template_id;
    lines.lines.push_back({std::to_string(k + 1) + ". [" + tag + "] " + prompt, tone});
    const std::string lesson = it.lessons.empty() ? std::string() : it.lessons.front();
    lessons.items.push_back(std::to_string(k + 1) + ". " + vm.tr("drills.lesson", {{"title", lesson_title(vm, lesson)}}));
  }
  m.lines.push_back(std::move(lines));
  m.items.push_back(std::move(lessons));
  m.name(names::kDrillsLessons, vm.tr("drills.endTitle"));
  const auto& picked = d.items.empty() ? std::vector<std::string>{} : d.items[static_cast<std::size_t>(std::clamp(d.lesson, 0, static_cast<int>(d.items.size()) - 1))].lessons;
  m.text(names::kDrillsLesson, "\xF0\x9F\x8E\x93 " + vm.tr("drills.lesson", {{"title", picked.empty() ? std::string() : lesson_title(vm, picked.front())}}));
  m.text(names::kDrillsAgain, vm.tr("drills.again"));
}

// ---- daily ----

std::string daily_result(const AppViewModel& vm, const std::optional<DailyAttempt>& a) {
  if (!a) return std::string(kDash);
  if (!a->score) return vm.tr("daily.noScore") + std::string(kSep) + price(vm, a->pnl) + " cr";
  return vm.tr("daily.result", {{"score", std::to_string(*a->score)}, {"pnl", price(vm, a->pnl)}});
}

void daily_history_table(const AppViewModel& vm, ScreenModel& m) {
  const auto rows = practice::daily_history(vm.progress());
  m.text(names::kDailyHistoryTitle, vm.tr("daily.history"));
  m.show(names::kDailyEmpty, rows.empty());
  m.text(names::kDailyEmpty, vm.tr("daily.noHistory"));
  m.show(names::kDailyHistory, !rows.empty());
  WidgetTable t{names::kDailyHistory,
                {vm.tr("daily.colDate"), vm.tr("daily.colMission"), vm.tr("daily.first"), vm.tr("daily.best"), vm.tr("daily.attempts")},
                {},
                {1.1f, 2.4f, 2, 2, 0.8f}};
  for (const auto& r : rows) {
    t.rows.push_back({r.date, mission_title(vm, r.day.mission), daily_result(vm, r.day.first), daily_result(vm, r.day.best),
                      std::to_string(r.day.attempts)});
  }
  m.tables.push_back(std::move(t));
  m.name(names::kDailyHistory, vm.tr("daily.history"));
}

// ---- guess ----

std::string points(const AppViewModel& vm, int hundredths) {
  return fmt::fmt_num(hundredths / 100.0, vm.locale(), hundredths % 100 == 0 ? 0 : 2);
}

std::string decision_name(const AppViewModel& vm, int decision) {
  static constexpr std::array<std::string_view, 3> kKeys = {"guess.long", "guess.short", "guess.none"};
  return vm.tr(kKeys[static_cast<std::size_t>(std::clamp(decision, 0, 2))]);
}

void guess_commit_box(const AppViewModel& vm, const GuessState& g, ScreenModel& m) {
  const std::size_t step = static_cast<std::size_t>(g.pick->step);
  const std::string close = g.at_decision_raw["bars"][step][3].as_string();
  const std::string atr = g.at_decision_raw["indicators"]["atr14"][step].as_string("0");
  m.text(names::kGuessClose, vm.tr("guess.close", {{"close", price(vm, close)}, {"atr", price(vm, atr)}}));
  m.text(names::kGuessDecisionLabel, vm.tr("guess.decision"));
  m.items.push_back({names::kGuessDecision, {decision_name(vm, 0), decision_name(vm, 1), decision_name(vm, 2)}, g.decision});
  m.name(names::kGuessDecision, vm.tr("guess.decision"));
  m.text(names::kGuessPLabel, vm.tr("guess.p", {{"p", std::to_string(g.p)}}));
  WidgetItems ps{names::kGuessP, {}, -1};
  for (const int p : practice::probabilities()) {
    if (p == g.p) ps.selected = static_cast<int>(ps.items.size());
    ps.items.push_back(std::to_string(p) + "%");
  }
  m.items.push_back(std::move(ps));
  m.name(names::kGuessP, vm.tr("guess.p", {{"p", std::to_string(g.p)}}));
  m.show(names::kGuessLevels, g.decision != 2);
  m.text(names::kGuessInvLabel, vm.tr("guess.invalidation"));
  m.text(names::kGuessTargetLabel, vm.tr("guess.target"));
  m.text(names::kGuessInv, g.invalidation);
  m.text(names::kGuessTarget, g.target);
  m.name(names::kGuessInv, vm.tr("guess.invalidation"));
  m.name(names::kGuessTarget, vm.tr("guess.target"));
  m.show(names::kGuessProblem, g.problem);
  m.text(names::kGuessProblem, vm.tr("guess.needLevels"));
  m.text(names::kGuessCommit, vm.tr("guess.commit"));
}

void guess_score_lines(const AppViewModel& vm, const practice::GuessScore& s, std::vector<Line>& out) {
  out.push_back({vm.tr("guess.score", {{"score", points(vm, s.total)}}), Tone::Accent});
  out.push_back({"  " + vm.tr("guess.part.calibration", {{"v", points(vm, s.calibration)}})});
  out.push_back({"  " + vm.tr("guess.part.plan", {{"v", points(vm, s.plan)}})});
  if (s.side_ok) {
    const auto mark = [](bool ok) { return std::string(ok ? "\xE2\x9C\x93 " : "x "); };  // ✓
    out.push_back({"    " + mark(*s.side_ok) + vm.tr("guess.check.side"), *s.side_ok ? Tone::Up : Tone::Down});
    out.push_back({"    " + mark(*s.distance_ok) + vm.tr("guess.check.distance"), *s.distance_ok ? Tone::Up : Tone::Down});
    out.push_back({"    " + mark(*s.reward_ok) + vm.tr("guess.check.reward"), *s.reward_ok ? Tone::Up : Tone::Down});
  } else {
    out.push_back({"    " + vm.tr("guess.noTradePlan"), Tone::Muted});
  }
  out.push_back({"  " + vm.tr("guess.part.consistency", {{"v", points(vm, s.consistency)}})});
}

void guess_reveal_box(const AppViewModel& vm, const GuessState& g, ScreenModel& m) {
  const GuessOutcome& o = *g.outcome;
  const bool trade = g.decision != 2;
  m.text(names::kGuessRevealTitle, vm.tr("guess.revealTitle"));
  WidgetLines lines{names::kGuessReveal, {}};
  lines.lines.push_back({vm.tr("guess.committed", {{"decision", decision_name(vm, g.decision)}, {"p", std::to_string(g.p)}})});
  if (trade) {
    lines.lines.push_back({vm.tr("guess.levels", {{"inv", price(vm, g.invalidation)}, {"target", price(vm, g.target)}}), Tone::Muted});
  }
  lines.lines.push_back({vm.tr(o.higher ? "guess.higher" : "guess.lower", {{"from", price(vm, o.from)}, {"to", price(vm, o.to)}})});
  if (trade) lines.lines.push_back({vm.tr("guess.first." + o.hit), Tone::Muted});
  guess_score_lines(vm, o.score, lines.lines);
  lines.lines.push_back({vm.tr("guess.oneSample"), Tone::Muted});
  m.lines.push_back(std::move(lines));
  m.text(names::kGuessNext, vm.tr("guess.next"));
}

void guess_history(const AppViewModel& vm, ScreenModel& m) {
  const auto& rounds = vm.progress().guess_history;
  m.text(names::kGuessRounds, vm.tr("guess.rounds", {{"n", std::to_string(rounds.size())}}));
  std::vector<practice::StatedOutcome> outcomes;
  for (const GuessRound& r : rounds) {
    if (r.higher) outcomes.push_back({r.p, *r.higher});
  }
  const auto table = practice::calibration_table(outcomes);
  const bool open = static_cast<int>(rounds.size()) >= practice::kGuessTableAfter && !table.empty();
  m.show(names::kGuessTableTitle, open);
  m.show(names::kGuessTable, open);
  m.show(names::kGuessTableLocked, !open);
  m.text(names::kGuessTableLocked, vm.tr("guess.tableLocked", {{"n", std::to_string(practice::kGuessTableAfter)}, {"done", std::to_string(rounds.size())}}));
  m.text(names::kGuessTableTitle, vm.tr("guess.tableTitle"));
  WidgetTable t{names::kGuessTable, {vm.tr("guess.colBucket"), vm.tr("guess.colRounds"), vm.tr("guess.colStated"), vm.tr("guess.colObserved")}, {}, {}};
  for (const auto& row : table) {
    t.rows.push_back({std::to_string(row.from) + "\xE2\x80\x93" + std::to_string(row.from == 90 ? 100 : row.from + 10) + "%",  // –
                      std::to_string(row.rounds), fmt::fmt_num(row.stated, vm.locale(), 0) + "%", fmt::fmt_num(row.observed, vm.locale(), 0) + "%"});
  }
  m.tables.push_back(std::move(t));
  m.name(names::kGuessTable, vm.tr("guess.tableTitle"));
}

// ---- coach review ----

std::string review_status(const AppViewModel& vm, const CoachReviewState& c, bool at_end) {
  if (!c.review) return vm.tr("coachReview.loading");
  if (!c.review->error.empty()) return vm.tr("coachReview.unavailable", {{"reason", c.review->error}});
  if (c.review->moments.empty()) return vm.tr("coachReview.none");
  return at_end ? vm.tr("coachReview.end") : std::string();
}

void review_moment(const AppViewModel& vm, const CoachReviewState& c, const practice::CoachMoment& mo, ScreenModel& m) {
  m.text(names::kReviewPosition, vm.tr("coachReview.position", {{"n", std::to_string(c.index + 1)},
                                                                {"total", std::to_string(c.review->moments.size())},
                                                                {"step", std::to_string(mo.step)}}));
  const bool good = mo.tone == "good";
  m.lines.push_back({names::kReviewTone, {{"[" + vm.tr(good ? "coachReview.good" : "coachReview.warn") + "]", good ? Tone::Up : Tone::Attention}}});
  m.text(names::kReviewText, practice::moment_text(mo, vm.catalog()));
  const bool lesson = web_content().lesson(mo.lesson) != nullptr;
  m.show(names::kReviewLesson, lesson);
  if (lesson) m.text(names::kReviewLesson, "\xF0\x9F\x8E\x93 " + vm.tr("coachReview.lesson", {{"title", lesson_title(vm, mo.lesson)}}));
  const bool term = web_content().term(mo.glossary) != nullptr;
  m.show(names::kReviewTerm, term);
  if (term) m.text(names::kReviewTerm, vm.tr("coachReview.term", {{"term", vm.tr("glossary." + mo.glossary + ".term")}}));
  m.enable(names::kReviewPrev, c.index > 0);
  m.show(names::kReviewRetry, practice::retryable(mo) && vm.mistake_retry_allowed());
  m.enable(names::kReviewRetry, !vm.waiting());
  m.name(names::kReviewChart, vm.tr("desktop.review.chart", {{"step", std::to_string(mo.step)}}));
}

// ---- debrief ----

void debrief_retry_box(const AppViewModel& vm, const json::Value& d, int warmup, ScreenModel& m) {
  WidgetItems items{names::kDebriefRetryPick, {}, vm.practice().retry_pick};
  for (const json::Value& f : d["findings"].items()) {
    const int step = static_cast<int>(f["step"].as_int(-1));
    if (!practice::finding_retryable(f["tone"].as_string(), step, warmup)) continue;
    items.items.push_back(vm.tr("debrief.showMoment", {{"step", std::to_string(step)}}) + std::string(kSep) +
                          vm.tr(practice::mistake_name(f["code"].as_string())));
  }
  const bool shown = vm.mistake_retry_allowed() && !items.items.empty();
  m.show(names::kDebriefRetryBox, shown);
  items.selected = std::clamp(items.selected, 0, std::max(0, static_cast<int>(items.items.size()) - 1));
  m.items.push_back(std::move(items));
  m.text(names::kDebriefRetryLabel, vm.tr("desktop.debrief.retryPick"));
  m.name(names::kDebriefRetryPick, vm.tr("desktop.debrief.retryPick"));
  m.text(names::kDebriefRetry, "\xE2\x86\xBA " + vm.tr("mistake.retry"));  // ↺
  m.enable(names::kDebriefRetry, !vm.waiting());
}

std::vector<std::string> mistake_row(const AppViewModel& vm, const std::string& label, const std::optional<MistakeResult>& r) {
  if (!r) return {label, std::string(kDash), std::string(kDash)};
  return {label, r->score ? std::to_string(*r->score) : std::string(kDash), price(vm, r->pnl) + " cr"};
}

void mistake_panel(const AppViewModel& vm, ScreenModel& m) {
  const auto& report = vm.practice().mistake;
  m.show(names::kDebriefMistakePanel, report.has_value());
  if (!report) return;
  const std::string name = vm.tr(practice::mistake_name(report->code));
  const bool repeated = report->outcome == "repeated";
  m.lines.push_back({names::kDebriefMistake,
                     {{vm.tr("mistake.title"), Tone::Title},
                      {vm.tr(repeated ? "mistake.repeated" : "mistake.avoided",
                             {{"moment", name}, {"from", std::to_string(report->step)}, {"to", std::to_string(report->step + practice::kRepeatWindow)}}),
                       repeated ? Tone::Attention : Tone::Up}}});
  m.tables.push_back({names::kDebriefMistakeTable,
                      {"", vm.tr("mistake.colScore"), vm.tr("mistake.colPnl")},
                      {mistake_row(vm, vm.tr("mistake.original"), report->original), mistake_row(vm, vm.tr("mistake.retryRun"), report->retry)},
                      {}});
  m.name(names::kDebriefMistakeTable, vm.tr("mistake.title"));
  m.text(names::kDebriefMistakeNote, vm.tr("mistake.sideBySide"));
}

}  // namespace

void practice_menu(const AppViewModel& vm, ScreenModel& m) { practice_menu_row(vm, m); }

ScreenModel drills(const AppViewModel& vm) {
  ScreenModel m;
  const DrillsState& d = vm.practice().drills;
  using P = DrillsState::Phase;
  m.text(names::kDrillsTitle, vm.tr("drills.title"));
  m.text(names::kDrillsBack, vm.tr("practice.back"));
  m.show(names::kDrillsIntroBox, d.phase == P::Intro);
  m.text(names::kDrillsIntro, vm.tr("drills.intro"));
  m.text(names::kDrillsTimer, vm.tr("drills.timerSetting"));
  m.check(names::kDrillsTimer, vm.settings().drill_timer);
  m.text(names::kDrillsReviewNote, vm.tr("drills.reviewNote"));
  m.text(names::kDrillsStart, vm.tr("drills.start"));
  const bool asking = d.phase == P::Asking && d.index < static_cast<int>(d.items.size());
  m.show(names::kDrillsAskBox, asking);
  if (asking) drill_item(vm, d, m);
  m.show(names::kDrillsEndBox, d.phase == P::Done);
  if (d.phase == P::Done) drill_end(vm, d, m);
  return m;
}

ScreenModel daily(const AppViewModel& vm) {
  ScreenModel m;
  const practice::DailyLink today = vm.daily_today();
  const auto it = vm.progress().daily_challenges.find(today.date);
  const DailyChallenge* record = it == vm.progress().daily_challenges.end() ? nullptr : &it->second;
  m.text(names::kDailyTitle, vm.tr("daily.title"));
  m.text(names::kDailyBack, vm.tr("practice.back"));
  m.text(names::kDailyIntro, vm.tr("daily.intro"));
  m.text(names::kDailyToday, vm.tr("daily.today", {{"date", today.date}}));
  m.text(names::kDailyMission, mission_title(vm, today.mission) + " \xE2\x80\x94 " + vm.tr("mission." + today.mission + ".tagline"));
  m.text(names::kDailySeed, vm.tr("daily.market", {{"seed", std::to_string(today.seed)}}));
  m.show(names::kDailyRecord, record != nullptr && record->first.has_value());
  if (record != nullptr) m.text(names::kDailyRecord, vm.tr("daily.todayFirst", {{"result", daily_result(vm, record->first)}}));
  m.text(names::kDailyPlay, vm.tr(record != nullptr && record->attempts > 0 ? "daily.replay" : "daily.play"));
  m.enable(names::kDailyPlay, !vm.waiting());
  daily_history_table(vm, m);
  return m;
}

ScreenModel guess(const AppViewModel& vm) {
  ScreenModel m;
  const GuessState& g = vm.practice().guess;
  using P = GuessState::Phase;
  m.text(names::kGuessTitle, vm.tr("guess.title"));
  m.text(names::kGuessBack, vm.tr("practice.back"));
  m.show(names::kGuessIntroBox, g.phase == P::Intro);
  m.text(names::kGuessIntro, vm.tr("guess.intro"));
  m.text(names::kGuessStart, vm.tr("guess.start"));
  m.show(names::kGuessStart, !vm.round_markets().empty());
  const bool unsupported = vm.round_markets().empty() || g.phase == P::Error;
  m.show(names::kGuessStatus, g.phase == P::Loading || unsupported);
  m.text(names::kGuessStatus, vm.tr(g.phase == P::Loading ? "guess.loading" : "guess.unsupported"));
  const bool commit = g.phase == P::Commit && g.at_decision && g.pick;
  const bool reveal = g.phase == P::Reveal && g.outcome && g.pick;
  m.show(names::kGuessChartBox, commit || reveal);
  if (g.at_decision && g.pick) {
    m.text(names::kGuessMarket, vm.tr("guess.market", {{"alias", g.at_decision->alias}, {"step", std::to_string(g.pick->step)}}));
    m.name(names::kGuessChart, vm.tr("guess.market", {{"alias", g.at_decision->alias}, {"step", std::to_string(g.pick->step)}}));
  }
  m.show(names::kGuessCommitBox, commit);
  if (commit) guess_commit_box(vm, g, m);
  m.show(names::kGuessRevealBox, reveal);
  if (reveal) guess_reveal_box(vm, g, m);
  guess_history(vm, m);
  return m;
}

ScreenModel coach_review(const AppViewModel& vm) {
  ScreenModel m;
  const CoachReviewState& c = vm.practice().coach;
  const practice::CoachMoment* mo = vm.coach_review_moment();
  const bool at_end = c.review && c.review->error.empty() && !c.review->moments.empty() && mo == nullptr;
  m.text(names::kReviewTitle, vm.tr("coachReview.title"));
  m.text(names::kReviewClose, vm.tr("coachReview.close"));
  m.text(names::kReviewIntro, vm.tr("coachReview.intro"));
  const std::string status = review_status(vm, c, at_end);
  m.show(names::kReviewStatus, !status.empty());
  m.text(names::kReviewStatus, status);
  m.show(names::kReviewMomentBox, mo != nullptr);
  if (mo != nullptr) review_moment(vm, c, *mo, m);
  m.text(names::kReviewPrev, vm.tr("coachReview.prev"));
  m.text(names::kReviewNext, vm.tr("coachReview.next"));
  m.text(names::kReviewRetry, "\xE2\x86\xBA " + vm.tr("coachReview.retry"));  // ↺
  const bool reveal = c.review && (at_end || c.review->moments.empty());
  m.show(names::kReviewReveal, reveal);
  m.text(names::kReviewReveal, vm.tr("coachReview.toReveal"));
  return m;
}

void practice_debrief(const AppViewModel& vm, ScreenModel& m) {
  const Snapshot* s = vm.snapshot();
  const auto& daily = vm.run_daily();
  m.show(names::kDebriefDaily, daily.has_value());
  if (daily) m.text(names::kDebriefDaily, "[" + vm.tr("daily.tag", {{"date", daily->date}}) + "]");
  m.show(names::kDebriefOpenReview, !vm.viewing());
  m.text(names::kDebriefOpenReview, vm.tr("coachReview.open"));
  if (s != nullptr && s->debrief) debrief_retry_box(vm, s->debrief->raw, s->warmup_bars, m);
  mistake_panel(vm, m);
}

void practice_game(const AppViewModel& vm, ScreenModel& m) {
  const auto& a = vm.run_assisted();
  const bool mistake = a && a->kind == "mistake" && a->step;
  m.show(names::kGameMistakeBanner, mistake);
  if (mistake) {
    m.text(names::kGameMistakeBanner, "\xE2\x86\xBA " + vm.tr("mistake.banner", {{"step", std::to_string(*a->step)},
                                                                                  {"moment", vm.tr(practice::mistake_name(a->code))}}));
  }
  const auto& daily = vm.run_daily();
  m.show(names::kGameDailyBanner, daily.has_value());
  if (daily) m.text(names::kGameDailyBanner, "\xE2\x98\x80 " + vm.tr("daily.tag", {{"date", daily->date}}));
}

}  // namespace tradelab::desktop::screens
