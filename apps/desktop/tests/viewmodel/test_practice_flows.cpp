// The practice formats in the shell (add-practice-and-advanced-strategies W3-D2, W3-D3), with a fake
// engine answered by the real one: a drill session (the web's items for the same date, a wrong answer's
// worked explanation, the schedule and history), the soft clock, the daily challenge (the day's mission
// and seed, no advisor points, the day's history), a guess round on its own run, the coach review cut
// at each moment's bar, and a mistake retried from the coach as an assisted attempt, avoided and
// repeated.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "model/engine_run.hpp"
#include "model/progress_json.hpp"
#include "model/storage/actions.hpp"
#include "model/storage/json_writer.hpp"
#include "support/fake_engine.hpp"
#include "support/shell_helpers.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/practice_names.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;

namespace {

constexpr std::int64_t kHour = 3'600'000;

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

json::Value golden(const std::string& name) {
  const auto v = json::parse(read_file(std::string(TRADELAB_GOLDEN_DIR) + "/" + name + ".json"));
  REQUIRE(v);
  return *v;
}

struct Practice {
  testing::FakeEngine engine;
  AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 2026u; }};
  std::int64_t now = practice::day_start("2026-11-09") + 12 * kHour;  // local noon
  std::unique_ptr<EngineRun> run;

  Practice() {
    vm.set_clock([this] { return now; });
    testing::tutorials_seen(vm);
  }

  // The fake engine's pending report-only call, answered by the real engine.
  void answer(const std::function<std::string(const testing::FakeEngine::Analysis&)>& reply) {
    REQUIRE_FALSE(engine.analyses.empty());
    const auto a = engine.analyses.front();
    engine.analyses.erase(engine.analyses.begin());
    a.reply(reply(a));
  }
  void answer_round() { answer([](const auto& a) { return EngineRun::round_state(a.request); }); }
  void answer_coach() { answer([this](const auto&) { return run->coach_review(); }); }

  void started(const std::string& run_id, CommandKind kind = CommandKind::Start, bool assisted = false) {
    auto s = std::make_shared<Snapshot>(run->snapshot());
    s->results.push_back(CommandResult{kind, true, "none"});
    s->run_id = run_id;
    s->assisted = assisted;
    vm.on_snapshot(std::move(s));
  }
  void state(const std::string& run_id, bool assisted = false) {
    auto s = std::make_shared<Snapshot>(run->snapshot());
    s->run_id = run_id;
    s->assisted = assisted;
    vm.on_snapshot(std::move(s));
  }
};

const Line* line_with(const WidgetLines* lines, std::string_view text) {
  if (lines == nullptr) return nullptr;
  const auto it = std::ranges::find_if(lines->lines, [&](const Line& l) { return l.text.find(text) != std::string::npos; });
  return it == lines->lines.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("practice: the menu offers the three practice formats") {
  Practice p;
  const ScreenModel m = p.vm.screen_model();
  CHECK(m.text_of(names::kMenuPracticeTitle) == "Prática");
  CHECK(m.text_of(names::kMenuDrills).ends_with("Exercícios do dia"));
  CHECK(m.items_of(names::kMenuList)->items.size() == 25);  // no round market among the missions
  p.vm.open_drills();
  CHECK(p.vm.screen() == Screen::Drills);
  p.vm.back_to_menu();
  p.vm.open_guess();
  CHECK(p.vm.screen() == Screen::Guess);
}

// The first session of 2026-11-09 is the web's (drills.json buildSession), its first item the hand-checked
// 3 × 0,8 = 2,40. A wrong answer shows the worked numbers; the session joins the schedule and history.
TEST_CASE("practice: a drill session asks the web's items and keeps its first-try results") {
  Practice p;
  p.vm.open_drills();
  ScreenModel m = p.vm.screen_model();
  CHECK(m.is_visible(names::kDrillsIntroBox, false));
  CHECK_FALSE(m.is_visible(names::kDrillsAskBox, true));
  p.vm.drills_start();
  const DrillsState& d = p.vm.practice().drills;
  REQUIRE(d.items.size() == 6);
  CHECK(d.items[0].template_id == "greeks-delta-units");
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kDrillsPosition) == "Pergunta 1 de 6 \xC2\xB7 Ler Greeks");
  CHECK(m.text_of(names::kDrillsClock) == "2:00 restantes");
  CHECK(m.text_of(names::kDrillsPrompt).find("3") != std::string::npos);
  p.vm.drills_set_typed("dois");  // not a number: asked again, nothing counted
  p.vm.drills_check();
  CHECK(d.feedback == DrillsState::Feedback::Invalid);
  CHECK_FALSE(d.results[0].has_value());
  p.vm.drills_set_typed("2,5");
  p.vm.drills_check();
  CHECK(d.feedback == DrillsState::Feedback::Wrong);
  m = p.vm.screen_model();
  CHECK(m.is_visible(names::kDrillsExplain, false));
  CHECK(m.text_of(names::kDrillsExplain).find("3 contratos \xC3\x97 0,8 = 2,40 unidades") != std::string::npos);  // 3 × 0,8
  CHECK(line_with(m.lines_of(names::kDrillsFeedback), "2,40") != nullptr);
  CHECK_FALSE(m.is_enabled(names::kDrillsCheck, true));
  for (std::size_t k = 1; k < d.items.size(); ++k) {
    p.vm.drills_next();
    const practice::DrillItem& item = d.items[k];
    if (item.choice) {
      p.vm.drills_choose(static_cast<int>(item.answer));
    } else {
      p.vm.drills_set_typed(fmt::number(item.answer, Locale::PtBR, item.digits));
      p.vm.drills_check();
    }
    CHECK(d.feedback == DrillsState::Feedback::Right);
  }
  CHECK(p.vm.screen_model().text_of(names::kDrillsNext) == "Ver o resultado");
  p.vm.drills_next();
  REQUIRE(d.phase == DrillsState::Phase::Done);
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kDrillsFirstTry).find("5") != std::string::npos);
  REQUIRE(m.items_of(names::kDrillsLessons) != nullptr);
  CHECK(m.items_of(names::kDrillsLessons)->items.size() == 6);
  const Progress& pr = p.vm.progress();
  REQUIRE(pr.drill_history.size() == 1);
  CHECK(pr.drill_history[0].first_try == 5);
  CHECK(pr.drill_reviews.at("greeks-read").box == 1);  // a wrong first answer: box 1
  p.vm.drills_open_lesson();
  CHECK(p.vm.screen() == Screen::Lesson);
  p.vm.lesson_leave();
  CHECK(p.vm.screen() == Screen::Drills);
  // The day's second session draws from "drill-v1:2026-11-09:1".
  p.vm.drills_start();
  CHECK(p.vm.practice().drills.items[0].template_id != "" );
  CHECK(practice::session_index(p.vm.progress().drill_history, "2026-11-09") == 1);
}

TEST_CASE("practice: the soft clock ends the session at two minutes, never without the setting") {
  Practice p;
  p.vm.open_drills();
  p.vm.drills_start();
  p.now += 119'000;
  p.vm.tick_practice();
  CHECK(p.vm.practice().drills.phase == DrillsState::Phase::Asking);
  CHECK(p.vm.screen_model().text_of(names::kDrillsClock) == "0:01 restantes");
  p.now += 1'000;
  p.vm.tick_practice();
  REQUIRE(p.vm.practice().drills.phase == DrillsState::Phase::Done);
  CHECK(p.vm.practice().drills.time_up);
  CHECK(p.vm.screen_model().is_visible(names::kDrillsTimeUp, false));
  REQUIRE(p.vm.progress().drill_history.size() == 1);
  CHECK(p.vm.progress().drill_history[0].first_try == 0);
  CHECK(p.vm.progress().drill_reviews.empty());  // skipped items change no schedule
  p.vm.drills_set_timer(false);
  CHECK_FALSE(p.vm.settings().drill_timer);
  p.vm.drills_start();
  p.now += 600'000;
  p.vm.tick_practice();
  CHECK(p.vm.practice().drills.phase == DrillsState::Phase::Asking);
  CHECK(p.vm.screen_model().text_of(names::kDrillsClock) == "sem relógio");
}

// 2026-11-09 is day 312 of the pool (312 mod 24 = 0): m01, seed FNV-1a of "tradelab-daily-v1:2026-11-09".
TEST_CASE("practice: the daily challenge starts the day's market without the advisor and keeps the day's history") {
  Practice p;
  p.vm.open_daily();
  ScreenModel m = p.vm.screen_model();
  const practice::DailyLink today = p.vm.daily_today();
  CHECK(today.mission == "m01-false-breakout");
  CHECK(today.seed == practice::fnv1a32("tradelab-daily-v1:2026-11-09"));
  CHECK(m.text_of(names::kDailyToday).find("2026-11-09") != std::string::npos);
  CHECK(m.is_visible(names::kDailyEmpty, false));
  p.vm.daily_play();
  CHECK(p.engine.log.back() == "start m01-false-breakout " + std::to_string(today.seed));
  CHECK(p.engine.last_options == RunOptions{});
  p.run = EngineRun::create("m01-false-breakout", today.seed, {});
  p.started("run-d1");
  REQUIRE(p.vm.run_daily());
  CHECK(*p.vm.run_daily() == today);
  p.vm.open_terminal();
  CHECK(p.vm.screen_model().is_visible(names::kGameDailyBanner, false));
  p.run->advance(500);
  p.state("run-d1");
  REQUIRE(p.vm.screen() == Screen::Debrief);
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kDebriefDaily).find("2026-11-09") != std::string::npos);
  const auto& day = p.vm.progress().daily_challenges.at("2026-11-09");
  CHECK(day.attempts == 1);
  REQUIRE(day.first);
  CHECK(day.first->run_id == "run-d1");
  CHECK(day.first->pnl == p.run->snapshot().debrief->net_pnl);
  CHECK(day.best == day.first);
  p.vm.back_to_menu();
  p.vm.open_daily();
  m = p.vm.screen_model();
  CHECK(m.is_visible(names::kDailyRecord, false));
  CHECK(m.text_of(names::kDailyPlay) == "Tentar de novo");
  REQUIRE(m.table_of(names::kDailyHistory) != nullptr);
  CHECK(m.table_of(names::kDailyHistory)->rows.size() == 1);
}

// The first round of 2026-11-09 is the web's (guess.json pickRound): r01-trend, seed 32247478, bar 113.
TEST_CASE("practice: a guess round runs its own market to the decision, then reveals and scores it") {
  Practice p;
  p.vm.open_guess();
  p.vm.guess_start();
  REQUIRE(p.engine.analyses.size() == 1);
  CHECK(p.engine.analyses[0].kind == AnalysisKind::Round);
  CHECK(p.engine.analyses[0].request == R"({"mission":"r01-trend","seed":32247478,"advance":53})");
  CHECK(p.vm.screen_model().text_of(names::kGuessStatus) == "Montando a rodada…");
  p.answer_round();
  const GuessState& g = p.vm.practice().guess;
  REQUIRE(g.phase == GuessState::Phase::Commit);
  CHECK(g.at_decision->step == 113);
  CHECK(static_cast<int>(g.at_decision->bars.size()) == 114);  // nothing after the decision bar
  ScreenModel m = p.vm.screen_model();
  CHECK(m.is_visible(names::kGuessCommitBox, false));
  p.vm.guess_commit();  // a trade without levels
  CHECK(p.vm.screen_model().is_visible(names::kGuessProblem, false));
  const double close = g.at_decision->bars.back().close;
  p.vm.guess_set_p(13);  // 70%
  CHECK(g.p == 70);
  p.vm.guess_set_level(false, fmt::to_fixed_text(close - 2, 2));
  p.vm.guess_set_level(true, fmt::to_fixed_text(close + 4, 2));
  p.vm.guess_commit();
  REQUIRE(p.engine.analyses.size() == 1);
  CHECK(p.engine.analyses[0].request == R"({"mission":"r01-trend","seed":32247478,"advance":65})");
  p.answer_round();
  REQUIRE(g.phase == GuessState::Phase::Reveal);
  REQUIRE(g.outcome);
  CHECK(static_cast<int>(g.revealed->bars.size()) == 126);
  // Calibration: 50 − (70 − o)² / 100; consistency 10 (a long with p ≥ 55); side right (10).
  CHECK(g.outcome->score.calibration == (g.outcome->higher ? 4100 : 100));
  CHECK(g.outcome->score.consistency == 1000);
  CHECK(*g.outcome->score.side_ok);
  REQUIRE(p.vm.progress().guess_history.size() == 1);
  const GuessRound& r = p.vm.progress().guess_history[0];
  CHECK(r.market == "r01-trend");
  CHECK(r.score == g.outcome->score.total);
  CHECK(r.higher == g.outcome->higher);
  m = p.vm.screen_model();
  CHECK(line_with(m.lines_of(names::kGuessReveal), "Uma amostra") != nullptr);
  CHECK(m.text_of(names::kGuessRounds).find("1") != std::string::npos);
  CHECK(m.is_visible(names::kGuessTableLocked, false));
  const game::ChartScene scene = p.vm.guess_scene(900, 300);
  CHECK(std::ranges::any_of(scene.texts, [](const game::SceneText& t) { return t.text == "\xE2\x96\xBC"; }));  // the decision bar
}

namespace {

// m23's golden played into the shell to its debrief (run "run-o").
void m23_debrief(Practice& p) {
  const json::Value fx = golden("coach-m23-second-chance");
  p.vm.select(22);
  p.vm.start_selected();
  p.run = EngineRun::create("m23-second-chance", 2026, {});
  p.started("run-o");
  p.vm.open_terminal();
  for (const auto& a : fx["actions"].items()) storage::apply_action(*p.run, a);
  p.state("run-o");
  REQUIRE(p.vm.screen() == Screen::Debrief);
}

// The retry as the host rewinds it: m23 rebuilt from the original's actions before bar 83 (run "run-r").
void rewound_at_83(Practice& p) {
  const json::Value fx = golden("coach-m23-second-chance");
  std::vector<std::string> log;
  for (const auto& a : fx["actions"].items()) log.push_back(storage::dump(a));
  p.run = EngineRun::create("m23-second-chance", 2026, {});
  for (const auto& a : storage::rewind_prefix(log, p.run->snapshot().warmup_bars, 83)) storage::apply_action(*p.run, *json::parse(a));
  p.started("run-r", CommandKind::Rewind, true);
}

}  // namespace

// The m23 golden's coach moments (coach-m23-second-chance), all at bar 83, each with the chart cut there.
TEST_CASE("practice: the coach review steps through the moments with the chart cut at each bar") {
  Practice p;
  m23_debrief(p);
  ScreenModel m = p.vm.screen_model();
  CHECK(m.text_of(names::kDebriefOpenReview) == "Revisar com o coach");
  p.vm.open_coach_review();
  REQUIRE(p.vm.screen() == Screen::CoachReview);
  CHECK(p.vm.screen_model().text_of(names::kReviewStatus) == "O coach está revendo a partida…");
  p.answer_coach();
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kReviewPosition) == "Momento 1 de 3 \xC2\xB7 barra 83");
  CHECK(m.text_of(names::kReviewText).find("112,81") != std::string::npos);  // the loss cut at invalidation
  CHECK_FALSE(m.is_visible(names::kReviewRetry, true));  // a good moment
  CHECK_FALSE(m.is_enabled(names::kReviewPrev, true));
  const GameState cut = practice::state_at(*p.vm.snapshot()->game, 83);
  CHECK(cut.bars.size() == 84);
  CHECK(cut.orders.empty());
  CHECK_FALSE(cut.account.liquidation);
  CHECK_FALSE(p.vm.coach_review_scene(900, 300).empty);
  p.vm.coach_review_step(1);
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kReviewText).find("298,07") != std::string::npos);  // risk above the 200 budget
  CHECK(m.is_visible(names::kReviewRetry, false));
  p.vm.coach_review_step(2);
  CHECK(p.vm.screen_model().text_of(names::kReviewStatus).starts_with("Fim da revisão"));
  CHECK(p.vm.screen_model().is_visible(names::kReviewReveal, false));
  p.vm.coach_review_to_reveal();
  CHECK(p.vm.screen() == Screen::Debrief);
  p.vm.open_coach_review();  // loaded once per run
  CHECK(p.engine.analyses.empty());
  CHECK(p.vm.screen_model().text_of(names::kReviewPosition) == "Momento 1 de 3 \xC2\xB7 barra 83");
}

TEST_CASE("practice: a mistake retried from the coach is assisted, named in a banner and avoided") {
  Practice p;
  m23_debrief(p);
  p.vm.open_coach_review();
  p.answer_coach();
  p.vm.coach_review_step(1);  // risk_above_budget at 83
  p.vm.coach_review_retry();
  CHECK(p.engine.log.back() == "rewind 83");
  const auto link = progress_json::assisted_from(*json::parse(p.engine.last_links.assisted));
  REQUIRE(link);
  CHECK(*link == AssistedLink{"run-o", "mistake", 83, "risk_above_budget"});
  rewound_at_83(p);
  REQUIRE(p.vm.screen() == Screen::Game);
  p.run->advance(1000);  // no entry this time
  p.state("run-r", true);
  REQUIRE(p.vm.screen() == Screen::Debrief);
  CHECK(p.vm.run_assisted() == link);
  p.answer_coach();  // the retry's own moments
  ScreenModel m = p.vm.screen_model();
  REQUIRE(p.vm.practice().mistake);
  CHECK(p.vm.practice().mistake->outcome == "avoided");
  CHECK(m.is_visible(names::kDebriefMistakePanel, false));
  CHECK(line_with(m.lines_of(names::kDebriefMistake), "Evitado: risco acima do orçamento não apareceu entre as barras 83 e 88.") != nullptr);
  const WidgetTable* t = m.table_of(names::kDebriefMistakeTable);
  REQUIRE(t != nullptr);
  REQUIRE(t->rows.size() == 2);
  CHECK(t->rows[0][0] == "Original");
  CHECK(t->rows[0][2].ends_with(" cr"));
  CHECK_FALSE(p.vm.progress().runs.back().summary.empty());
  CHECK(std::ranges::find(p.vm.progress().completed, "m23-second-chance") != p.vm.progress().completed.end());  // the original's
}

TEST_CASE("practice: a retry that makes the same entry again repeats the mistake; the banner names the moment") {
  Practice p;
  m23_debrief(p);
  const auto target = p.vm.debrief_retry_target();  // the debrief's own "Retry from here"
  REQUIRE(target);
  CHECK(target->first >= p.vm.snapshot()->warmup_bars);
  ScreenModel m = p.vm.screen_model();
  CHECK(m.is_visible(names::kDebriefRetryBox, false));
  p.vm.retry_mistake(83, "revenge_trade");
  // Back at bar 83 in the terminal: the banner names the moment, never what happened after it.
  rewound_at_83(p);
  REQUIRE(p.vm.screen() == Screen::Game);
  const json::Value fx = golden("coach-m23-second-chance");
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kGameMistakeBanner).find("Nova tentativa a partir da barra 83: trade de vingança.") != std::string::npos);
  const auto& acts = fx["actions"].items();
  for (std::size_t i = 5; i < acts.size(); ++i) storage::apply_action(*p.run, acts[i]);
  p.state("run-r", true);
  REQUIRE(p.vm.screen() == Screen::Debrief);
  p.answer_coach();
  REQUIRE(p.vm.practice().mistake);
  CHECK(p.vm.practice().mistake->outcome == "repeated");
  CHECK(p.vm.progress().runs.back().assisted);
}
