// The Academy on the desktop (add-academy-and-strategies tasks D3, D6, D7, D8), without a window:
// every lesson played through AppViewModel with its widgets and question types, the retake and the
// spaced review on a fake clock, the review prompt, the glossary and its in-place definitions, and the
// guided first trade on a real campaign terminal (start, typed stop, advance, debrief, skip, replay).
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "model/engine_run.hpp"
#include "model/storage/actions.hpp"
#include "support/academy_helpers.hpp"
#include "support/shell_helpers.hpp"
#include "viewmodel/academy/review.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/screen_names.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::test_support;
namespace names = tradelab::desktop::names;

namespace {

struct FakeEngine final : EngineCommands {
  std::vector<std::string> log;
  RunOptions last_options;
  void start(const std::string& mission, std::uint32_t seed) override { log.push_back("start " + mission + " " + std::to_string(seed)); }
  void start_with(const std::string& mission, std::uint32_t seed, const RunOptions& o) override {
    last_options = o;
    start(mission, seed);
  }
  void advance(int steps) override { log.push_back("advance " + std::to_string(steps)); }
  void market(Side side, const std::string& qty) override { log.push_back(side == Side::Buy ? "buy " + qty : "sell " + qty); }
};

std::shared_ptr<const Snapshot> with_result(Snapshot s, CommandKind kind) {
  s.results.push_back(CommandResult{kind, true, "none"});
  return std::make_shared<Snapshot>(std::move(s));
}

constexpr std::int64_t kDay = 24LL * 3600 * 1000;

struct Fixture {
  FakeEngine engine;
  AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 1u; }};
  std::int64_t now = 1'767'268'800'000;  // 2026-01-01 12:00 UTC
  Fixture() {
    vm.set_clock([this] { return now; });
    testing::tutorials_seen(vm);
  }
  [[nodiscard]] const LessonStep& step() const { return *vm.shell().lesson.step(); }
  [[nodiscard]] Locale L() const { return vm.locale(); }

  void edit(const SandboxEdit& e) {
    using F = AppViewModel::SandboxField;
    const F field = e.field == "qty" ? F::Qty : e.field == "leverage" ? F::Leverage : e.field == "stop" ? F::Stop : F::Target;
    vm.sandbox_set_text(field, e.text);
  }

  // Answers the current question: wrong first when asked, then right; checks the feedback each time.
  void answer(bool wrong_first) {
    const LessonStep& s = step();
    if (!s.is_question()) return;
    if (wrong_first) {
      give(s, false);
      CHECK(vm.shell().lesson.status() == AnswerStatus::Wrong);
      CHECK(vm.screen_model().text_of(names::kLessonFeedback).starts_with("Tente de novo. "));
      CHECK_FALSE(vm.shell().lesson.solved());
    }
    give(s, true);
    CHECK(vm.shell().lesson.status() == AnswerStatus::Right);
    CHECK(vm.screen_model().text_of(names::kLessonFeedback).starts_with("Correto. "));
  }

  void give(const LessonStep& s, bool right) {
    switch (s.question->type) {
      case StepQuestion::Type::Choice: vm.lesson_pick(right ? s.question->answer : choice_wrong(s)); break;
      case StepQuestion::Type::Number:
        vm.lesson_set_typed(right ? number_answer(s, L()) : number_wrong(s, L()));
        vm.lesson_submit_number();
        break;
      case StepQuestion::Type::Set: {
        const auto e = right ? set_answer(s, L()) : set_wrong(s, L());
        REQUIRE(e.has_value());
        edit(*e);
        vm.lesson_check_set();
        break;
      }
    }
  }

  // Plays the open lesson to its end screen.
  void finish_lesson(bool wrong_first) {
    for (int guard = 0; guard < 60 && !vm.shell().lesson.finished(); ++guard) {
      answer(wrong_first);
      const ScreenModel m = vm.screen_model();
      CHECK(m.is_enabled(names::kLessonNext));
      vm.lesson_next();
    }
    REQUIRE(vm.shell().lesson.finished());
  }
};

}  // namespace

TEST_CASE("academy: every lesson is played from the Academy screen, wrong then right, and completed") {
  Fixture f;
  f.vm.open_academy();
  REQUIRE(f.vm.screen() == Screen::Academy);
  ScreenModel m = f.vm.screen_model();
  const WidgetItems* list = m.items_of(names::kAcademyLessons);
  REQUIRE(list != nullptr);
  REQUIRE(list->items.size() == web_content().lessons.size());
  CHECK(list->items[0] == "Fundamentos · Ler uma barra [NOVA]");
  // A stage's subtitle stays in the detail: the full name pushed the title and status out of the list.
  CHECK(list->items[4] == "Futuros II · Margem, alavancagem e liquidação [NOVA]");
  f.vm.academy_select(4);
  CHECK(f.vm.screen_model().text_of(names::kAcademyDetail).starts_with("Futuros II · margem, alavancagem e liquidação\nMargem"));
  for (const auto& row : list->items) CHECK(row.size() <= 64);
  CHECK_FALSE(m.is_visible(names::kAcademyDueBox));
  const int count = static_cast<int>(list->items.size());
  for (int i = 0; i < count; ++i) {
    f.vm.academy_select(i);
    f.vm.academy_start_selected();
    REQUIRE(f.vm.screen() == Screen::Lesson);
    INFO("lesson " << f.vm.shell().lesson.lesson()->id);
    f.finish_lesson(true);
    m = f.vm.screen_model();
    CHECK(m.text_of(names::kLessonScore).starts_with("0 de "));
    CHECK(m.text_of(names::kLessonFinish) == "Voltar à Academia");
    f.vm.lesson_finish();
    CHECK(f.vm.screen() == Screen::Academy);
  }
  CHECK(f.vm.progress().lessons_done.size() == web_content().lessons.size());
  CHECK(f.vm.progress().lesson_reviews.size() == web_content().lessons.size());
  m = f.vm.screen_model();
  CHECK(m.items_of(names::kAcademyLessons)->items[0].ends_with("[CONCLUÍDA]"));
  CHECK(m.text_of(names::kAcademyStart) == "Refazer lição");
}

TEST_CASE("academy: the ticket sandbox computes the web's numbers and a set question reads it") {
  Fixture f;
  f.vm.open_lesson("risk-sizing", Screen::Academy);
  while (f.step().id != "set-size") {
    f.answer(false);
    f.vm.lesson_next();
  }
  ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kLessonTicketBox, false));
  CHECK(m.is_visible(names::kLessonSetRow, false));
  CHECK_FALSE(m.is_visible(names::kLessonNumberRow));
  CHECK(m.is_enabled(names::kSandboxQty));
  CHECK_FALSE(m.is_enabled(names::kSandboxLeverage));  // only the quantity is the player's here
  CHECK(m.tip_of(names::kSandboxLevLabel).starts_with("Alavancagem: "));
  // Typing with a comma: 12,5 contracts at entry 100 with the stop at 96 risk 50.
  f.vm.sandbox_set_text(AppViewModel::SandboxField::Qty, "12,5");
  m = f.vm.screen_model();
  const WidgetLines* out = m.lines_of(names::kSandboxOut);
  REQUIRE(out != nullptr);
  bool risk = false;
  for (const auto& l : out->lines) risk = risk || l.text == "Risco planejado até o stop: 50,00 cr (0,50%)";
  CHECK(risk);
  f.vm.sandbox_set_text(AppViewModel::SandboxField::Leverage, "50");  // not editable: ignored
  CHECK(f.vm.shell().academy.sandbox->leverage == 3);
  f.vm.lesson_check_set();
  CHECK(f.vm.shell().lesson.status() == AnswerStatus::Wrong);
  f.vm.sandbox_set_text(AppViewModel::SandboxField::Qty, "25");
  f.vm.lesson_check_set();
  CHECK(f.vm.shell().lesson.solved());
  // A step keeps its answer when the player comes back to it; a fresh sandbox each time.
  f.vm.lesson_next();
  f.vm.lesson_prev();
  CHECK(f.vm.shell().lesson.solved());
  CHECK(f.vm.shell().academy.sandbox_text[0] == "10");
}

TEST_CASE("academy: a number question accepts either decimal mark and the units, and grades the first try") {
  Fixture f;
  f.vm.open_lesson("events-surprise", Screen::Academy);
  while (f.step().id != "gapFill") {
    f.answer(false);
    f.vm.lesson_next();
  }
  ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kLessonNumberRow, false));
  CHECK_FALSE(m.is_enabled(names::kLessonCheck));  // nothing typed
  f.vm.lesson_set_typed("abc");
  f.vm.lesson_submit_number();
  CHECK(f.vm.shell().lesson.status() == AnswerStatus::Idle);  // not a number: not graded
  f.vm.lesson_set_typed("2.6");
  f.vm.lesson_submit_number();
  CHECK(f.vm.shell().lesson.solved());
}

TEST_CASE("academy: payoff, table and chart levels are shown with their texts and marked terms") {
  Fixture f;
  f.vm.open_lesson("options-payoff", Screen::Academy);
  bool payoff = false;
  bool hidden = false;
  for (int guard = 0; guard < 40 && !f.vm.shell().lesson.finished(); ++guard) {
    ScreenModel m = f.vm.screen_model();
    if (const auto* p = std::get_if<PayoffWidget>(&f.step().widget)) {
      payoff = true;
      CHECK(m.is_visible(names::kLessonPayoffBox, false));
      CHECK_FALSE(m.text_of(names::kLessonPayoffLegs).empty());
      CHECK(m.is_visible(names::kLessonPayoffAnalytics) == !p->hide_analytics);
      hidden = hidden || p->hide_analytics;
      if (!p->hide_analytics) {
        const WidgetLines* a = m.lines_of(names::kLessonPayoffAnalytics);
        REQUIRE(a != nullptr);
        CHECK(a->lines[0].tip.starts_with(f.vm.tr("glossary.breakeven.term") + ": "));
      }
    }
    f.answer(false);
    f.vm.lesson_next();
  }
  CHECK(payoff);
  CHECK(hidden);  // a step that asks for the breakeven hides it

  f.vm.open_lesson("orders-execution", Screen::Curriculum);
  bool table = false;
  for (int guard = 0; guard < 40 && !f.vm.shell().lesson.finished(); ++guard) {
    ScreenModel m = f.vm.screen_model();
    if (std::holds_alternative<TableWidget>(f.step().widget)) {
      table = true;
      const WidgetTable* t = m.table_of(names::kLessonTable);
      REQUIRE(t != nullptr);
      CHECK_FALSE(t->rows.empty());
      CHECK(t->columns.size() == std::get<TableWidget>(f.step().widget).columns.size());
    }
    f.answer(false);
    f.vm.lesson_next();
  }
  CHECK(table);
}

TEST_CASE("academy: a step's linked terms are listed with their definitions and open the glossary") {
  Fixture f;
  f.vm.open_lesson("futures-margin", Screen::Academy);
  while (f.step().terms.empty()) {
    f.answer(false);
    f.vm.lesson_next();
  }
  const std::string term = f.step().terms[0];
  const ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kLessonTermsRow, false));
  CHECK(m.tip_of(names::kLessonTerms[0]).starts_with(f.vm.tr("glossary." + term + ".term") + ": "));
  const int at = f.vm.shell().lesson.index();
  f.vm.open_glossary(term);
  REQUIRE(f.vm.screen() == Screen::Glossary);
  CHECK(f.vm.screen_model().text_of(names::kGlossaryTerm) == f.vm.tr("glossary." + term + ".term"));
  f.vm.leave_glossary();
  CHECK(f.vm.screen() == Screen::Lesson);
  CHECK(f.vm.shell().lesson.index() == at);  // the lesson goes on where it was
}

TEST_CASE("academy: completing schedules a review a day later; reviews move up, reset and retire") {
  Fixture f;
  f.vm.open_lesson("risk-expectancy", Screen::Academy);
  f.finish_lesson(false);
  f.vm.lesson_finish();
  const std::string day0 = f.vm.today();
  CHECK(f.vm.progress().lesson_reviews.at("risk-expectancy") == ReviewEntry{1, academy::add_days(day0, 1)});
  CHECK(f.vm.reviews_due().empty());
  ScreenModel m = f.vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kAcademyDueBox));

  // A retake leaves the schedule alone.
  f.vm.open_lesson("risk-expectancy", Screen::Academy);
  f.finish_lesson(true);
  f.vm.lesson_finish();
  CHECK(f.vm.progress().lesson_reviews.at("risk-expectancy").box == 1);

  f.now += kDay;
  REQUIRE(f.vm.reviews_due() == std::vector<std::string>{"risk-expectancy"});
  m = f.vm.screen_model();
  CHECK(m.is_visible(names::kAcademyDueBox, false));
  CHECK(m.text_of(names::kAcademyDueTitle) == "Revisões de hoje (1)");
  CHECK(m.items_of(names::kAcademyLessons)->items[7].ends_with("[REVISAR]"));
  f.vm.academy_review_selected();
  REQUIRE(f.vm.screen() == Screen::Lesson);
  REQUIRE(f.vm.shell().lesson.mode() == LessonMode::Review);
  // A review asks only the decision and transfer questions.
  CHECK(f.vm.shell().lesson.count() == static_cast<int>(review_steps(*f.vm.shell().lesson.lesson()).size()));
  f.finish_lesson(false);
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kLessonDoneTitle) == "Revisão concluída");
  CHECK(m.text_of(names::kLessonDoneReview) == f.vm.tr("academy.reviewUp"));
  f.vm.lesson_finish();
  const std::string day1 = f.vm.today();
  CHECK(f.vm.progress().lesson_reviews.at("risk-expectancy") == ReviewEntry{2, academy::add_days(day1, 3)});

  // A wrong first answer sends it back to box 1.
  f.now += 3 * kDay;
  f.vm.academy_review_selected();
  f.finish_lesson(true);
  CHECK(f.vm.screen_model().text_of(names::kLessonDoneReview) == f.vm.tr("academy.reviewReset"));
  f.vm.lesson_finish();
  CHECK(f.vm.progress().lesson_reviews.at("risk-expectancy").box == 1);
}

TEST_CASE("academy: the menu's review prompt counts the reviews due and stays dismissed for the day") {
  Fixture f;
  ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kMenuAcademy) == "Academia");
  CHECK_FALSE(m.is_visible(names::kMenuReviewRow));
  f.vm.open_lesson("mind-plan", Screen::Menu);
  f.finish_lesson(false);
  f.vm.lesson_finish();
  REQUIRE(f.vm.screen() == Screen::Menu);
  f.now += kDay;
  m = f.vm.screen_model();
  CHECK(m.is_visible(names::kMenuReviewRow, false));
  CHECK(m.text_of(names::kMenuReviewText) == "1 lição para revisar hoje");
  f.vm.dismiss_review_prompt();
  CHECK_FALSE(f.vm.screen_model().is_visible(names::kMenuReviewRow));
  CHECK(f.vm.settings().review_dismissed_on == f.vm.today());
  f.now += kDay;  // the next day it is back
  CHECK(f.vm.screen_model().is_visible(names::kMenuReviewRow, false));
}

TEST_CASE("academy: the glossary searches without accents or case and marks terms seen") {
  Fixture f;
  f.vm.open_glossary();
  REQUIRE(f.vm.screen() == Screen::Glossary);
  ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kGlossaryCount) == std::to_string(web_content().glossary.size()) + " termos");
  CHECK(m.is_visible(names::kGlossaryPick, false));
  f.vm.glossary_search("LIQUIDACAO");
  const auto hits = f.vm.glossary_hits();
  REQUIRE_FALSE(hits.empty());
  CHECK(std::ranges::find(hits, "liquidation-price") != hits.end());
  m = f.vm.screen_model();
  CHECK(m.items_of(names::kGlossaryList)->items[0].ends_with(" · novo"));
  f.vm.glossary_select(0);
  m = f.vm.screen_model();
  CHECK(m.is_visible(names::kGlossaryEntry, false));
  CHECK(contains(f.vm.progress().glossary_seen, hits[0]));
  CHECK_FALSE(m.items_of(names::kGlossaryList)->items[0].ends_with(" · novo"));
  CHECK(m.items_of(names::kGlossaryList)->selected == 0);
  f.vm.glossary_search("um termo que não existe");
  CHECK(f.vm.screen_model().text_of(names::kGlossaryCount) == "0 termos");
  f.vm.leave_glossary();
  CHECK(f.vm.screen() == Screen::Menu);
}

TEST_CASE("academy: debrief findings carry their glossary term and the recommended lesson opens") {
  Fixture f;
  f.vm.start_selected();
  auto run = EngineRun::create("m01-false-breakout", 1, {});
  f.vm.on_snapshot(with_result(run->snapshot(), CommandKind::Start));
  f.vm.open_terminal();
  run->place_market(Side::Buy, "1.5");
  run->advance(500);
  f.vm.on_snapshot(with_result(run->snapshot(), CommandKind::Advance));
  REQUIRE(f.vm.screen() == Screen::Debrief);
  const ScreenModel m = f.vm.screen_model();
  const WidgetLines* findings = m.lines_of(names::kDebriefFindings);
  REQUIRE(findings != nullptr);
  bool tipped = false;
  for (const auto& l : findings->lines) tipped = tipped || !l.tip.empty();
  CHECK(tipped);  // no stop: the stop-loss definition
  const std::string lesson = f.vm.debrief_lesson();
  REQUIRE_FALSE(lesson.empty());
  CHECK(m.is_visible(names::kDebriefOpenLesson, false));
  f.vm.open_debrief_lesson();
  CHECK(f.vm.screen() == Screen::Lesson);
  CHECK(f.vm.shell().lesson.lesson()->id == lesson);
  f.vm.lesson_leave();
  CHECK(f.vm.screen() == Screen::Debrief);
}

// ---- guided first trade ----

namespace {

struct CampaignTerminal {
  Fixture f;
  std::unique_ptr<EngineRun> run;
  void open() {
    f.vm.campaign_start();
    run = EngineRun::create("m06-thin-book", 1, f.engine.last_options);
    f.vm.on_snapshot(with_result(run->snapshot(), CommandKind::Start));
    REQUIRE(f.vm.screen() == Screen::Briefing);
    f.vm.open_terminal();
    f.vm.tick_coach();
    // A calibration question is modal: the callout waits for it (CoachMarks.svelte `visible`).
    if (f.vm.game().modal() == game::Modal::Calibration) {
      CHECK_FALSE(f.vm.screen_model().is_visible(names::kGameCoach));
      storage::apply_action(*run, storage::actions::answer(0, true));
      f.vm.on_snapshot(with_result(run->snapshot(), CommandKind::Advance));
      REQUIRE(f.vm.game().modal() == game::Modal::None);
    }
  }
  [[nodiscard]] std::string step() const { return f.vm.coach().step() ? f.vm.coach().step()->id : ""; }
  void advance() {
    run->advance(1);
    f.vm.on_snapshot(with_result(run->snapshot(), CommandKind::Advance));
    f.vm.tick_coach();
  }
};

}  // namespace

TEST_CASE("coach: the first campaign terminal starts the tour; a typed stop and an advance complete steps") {
  CampaignTerminal t;
  t.open();
  REQUIRE(t.step() == "chart");
  ScreenModel m = t.f.vm.screen_model();
  CHECK(m.is_visible(names::kGameCoach, false));
  CHECK(m.text_of(names::kGameCoachStep) == "Primeiro trade guiado · Passo 1 de 6");
  CHECK(m.text_of(names::kGameCoachTitle) == "O gráfico");
  CHECK(m.text_of(names::kGameCoachNext) == "Próximo");
  bool described = false;
  for (const auto& d : m.descriptions) described = described || (d.widget == "game-panel-chart" && d.text == t.f.vm.tr("coach.chart.body"));
  CHECK(described);
  t.f.vm.coach_next();
  CHECK(t.step() == "ticket");
  t.f.vm.coach_next();
  REQUIRE(t.step() == "stop");
  t.f.vm.tick_coach();  // the stop step saw the field as it was
  t.f.vm.game().ticket.stop = "95";
  t.f.vm.tick_coach();
  CHECK(t.step() == "risk");
  t.f.vm.coach_next();
  REQUIRE(t.step() == "advance");
  t.f.vm.tick_coach();
  CHECK_FALSE(contains(t.f.vm.progress().coach_done, "first-trade"));
  t.advance();
  CHECK(t.step() == "debrief");  // the terminal part is over: waits for this run's debrief
  CHECK(contains(t.f.vm.progress().coach_done, "first-trade"));
  m = t.f.vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kGameCoach));
}

TEST_CASE("coach: skipping marks the tour done; the briefing and the help replay it") {
  CampaignTerminal t;
  t.open();
  REQUIRE(t.step() == "chart");
  t.f.vm.coach_skip();
  CHECK(t.step().empty());
  CHECK(contains(t.f.vm.progress().coach_done, "first-trade"));
  // Help -> Guided tour: starts on this terminal now.
  t.f.vm.coach_replay();
  CHECK(t.step() == "chart");
  t.f.vm.coach_skip();
  // A later terminal of the same device does not start it by itself.
  t.f.vm.back_to_menu();
  t.f.vm.tick_coach();
  t.f.vm.campaign_continue();
  t.f.vm.tick_coach();
  CHECK(t.step().empty());
  // The briefing's Guided tour starts it when the terminal opens.
  t.f.vm.back_to_menu();
  t.f.vm.tick_coach();
  t.f.vm.coach_replay();
  CHECK(t.f.vm.coach().requested);
  t.f.vm.campaign_continue();
  t.f.vm.tick_coach();
  CHECK(t.step() == "chart");
  // Leaving the terminal for the menu ends it without marking anything new.
  t.f.vm.back_to_menu();
  t.f.vm.tick_coach();
  CHECK(t.step().empty());
}

TEST_CASE("coach: the debrief step shows in that run's debrief, then the tour ends") {
  CampaignTerminal t;
  t.open();
  for (int i = 0; i < 4; ++i) t.f.vm.coach_next();
  REQUIRE(t.step() == "advance");
  t.run->place_market(Side::Buy, "1");
  t.run->advance(500);
  t.f.vm.on_snapshot(with_result(t.run->snapshot(), CommandKind::Advance));
  REQUIRE(t.f.vm.screen() == Screen::Debrief);
  t.f.vm.tick_coach();
  REQUIRE(t.step() == "debrief");
  const ScreenModel m = t.f.vm.screen_model();
  CHECK(m.is_visible(names::kDebriefCoach, false));
  CHECK(m.text_of(names::kDebriefCoachNext) == "Concluir");
  t.f.vm.coach_next();
  CHECK(t.step().empty());
  CHECK_FALSE(t.f.vm.screen_model().is_visible(names::kDebriefCoach));
}
