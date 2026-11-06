// Academy lesson (web LessonRunner.svelte + StepQuestion.svelte): step header, illustrative chart
// (drawn by the view from the step's chart) with its screen-reader table of closed bars, the step's
// widget (screens/lesson_widgets.cpp), the question (choices, a typed number or a sandbox target) and
// its feedback, the glossary terms the step's text links, and the end screen with the first-try score
// (and, for a spaced review, how the schedule moves).
#include "viewmodel/academy_names.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

std::string choice_text(const AppViewModel& vm, const std::string& step_base, const LessonChoice& c) {
  switch (c.kind) {
    case LessonChoice::Kind::Bar: return vm.tr("academy.bar", {{"n", std::to_string(c.bar)}});
    case LessonChoice::Kind::Pct: return fmt::fmt_pct(c.value / 100.0, vm.locale(), 2, true);
    case LessonChoice::Kind::Text: {
      const std::string key = step_base + ".choice." + c.id;
      return vm.catalog().has(key) ? vm.tr(key) : c.id;
    }
  }
  return c.id;
}

// LessonChart.svelte's table: only the closed bars (never the hatched future).
void bar_table(const AppViewModel& vm, const LessonChart& chart, ScreenModel& m) {
  const Locale L = vm.locale();
  WidgetTable t{names::kLessonBars,
                {vm.tr("academy.col.bar"), vm.tr("academy.art.open"), vm.tr("academy.art.high"), vm.tr("academy.art.low"),
                 vm.tr("academy.art.close"), vm.tr("academy.col.volume")},
                {}};
  const int known = chart.known > 0 ? chart.known : static_cast<int>(chart.candles.size());
  for (int i = 0; i < known && i < static_cast<int>(chart.candles.size()); ++i) {
    const LessonCandle& k = chart.candles[static_cast<std::size_t>(i)];
    t.rows.push_back({std::to_string(i + 1), fmt::fmt_num(k.o, L, 2), fmt::fmt_num(k.h, L, 2), fmt::fmt_num(k.l, L, 2),
                      fmt::fmt_num(k.c, L, 2), fmt::fmt_num(k.v, L, 0)});
  }
  m.tables.push_back(std::move(t));
  m.name(names::kLessonBars, vm.tr("academy.market", {{"market", chart.market}, {"interval", chart.interval}}) +
                                 std::string(kSep) + vm.tr("academy.illustrative"));
  m.text(names::kLessonChartNote, vm.tr("academy.illustrative") + std::string(kSep) +
                                      vm.tr("academy.market", {{"market", chart.market}, {"interval", chart.interval}}) +
                                      (chart.known > 0 ? std::string(kSep) + vm.tr("academy.future") : ""));
}

void choices(const AppViewModel& vm, const std::string& step_base, const StepQuestion* q, ScreenModel& m) {
  const LessonRunner& r = vm.shell().lesson;
  const bool choice = q != nullptr && q->type == StepQuestion::Type::Choice;
  for (std::size_t k = 0; k < names::kLessonChoices.size(); ++k) {
    const bool shown = choice && k < q->choices.size();
    m.show(names::kLessonChoices[k], shown);
    if (!shown) continue;
    const bool right = static_cast<int>(k) == q->answer;
    const bool picked = r.picked() == static_cast<int>(k);
    m.text(names::kLessonChoices[k], (picked ? (right ? "\xE2\x9C\x94 " : "\xE2\x9C\x96 ") : "") + choice_text(vm, step_base, q->choices[k]));
    m.enable(names::kLessonChoices[k], !r.solved() || right);
  }
}

// Feedback after each answer: a wrong text choice names its misconception when the step has one.
void feedback(const AppViewModel& vm, const std::string& step_base, const StepQuestion* q, ScreenModel& m) {
  const LessonRunner& r = vm.shell().lesson;
  m.show(names::kLessonFeedback, r.status() != AnswerStatus::Idle);
  if (r.status() == AnswerStatus::Idle) return;
  const bool right = r.status() == AnswerStatus::Right;
  std::string key = step_base + (right ? ".right" : ".wrong");
  if (!right && q != nullptr && r.picked() && q->type == StepQuestion::Type::Choice) {
    const LessonChoice& c = q->choices[static_cast<std::size_t>(*r.picked())];
    const std::string own = step_base + ".feedback." + c.id;
    if (c.kind == LessonChoice::Kind::Text && vm.catalog().has(own)) key = own;
  }
  m.text(names::kLessonFeedback, vm.tr(right ? "academy.right" : "academy.wrong") + " " + vm.tr(key));
}

// The glossary terms the step's text links ([[id|text]] on the web): one button each, its short
// definition as tooltip and description; activating it opens the glossary at the term.
void terms(const AppViewModel& vm, const LessonStep& step, ScreenModel& m) {
  m.show(names::kLessonTermsRow, !step.terms.empty());
  m.text(names::kLessonTermsLabel, vm.tr("desktop.academy.terms"));
  for (std::size_t i = 0; i < names::kLessonTerms.size(); ++i) {
    const bool shown = i < step.terms.size();
    m.show(names::kLessonTerms[i], shown);
    if (!shown) continue;
    const std::string tip = term_tip(vm, step.terms[i]);
    m.text(names::kLessonTerms[i], "\xF0\x9F\x93\x96 " + vm.tr("glossary." + step.terms[i] + ".term"));  // 📖
    m.tip(names::kLessonTerms[i], tip + " " + vm.tr("glossary.more"));
  }
}

void done_screen(const AppViewModel& vm, const std::string& base, ScreenModel& m) {
  const LessonRunner& r = vm.shell().lesson;
  const LessonRunner::Score s = r.score();
  const bool review = r.mode() == LessonMode::Review;
  m.text(names::kLessonDoneTitle, vm.tr(review ? "academy.reviewDoneTitle" : "academy.doneTitle"));
  m.text(names::kLessonDoneName, vm.tr(base + ".title"));
  m.text(names::kLessonScore, vm.tr("academy.doneScore", {{"right", std::to_string(s.right)}, {"total", std::to_string(s.total)}}));
  m.text(names::kLessonDoneReview,
         vm.tr(review ? (r.all_right_first_try() ? "academy.reviewUp" : "academy.reviewReset") : "academy.reviewNext"));
  m.text(names::kLessonDoneNote, vm.tr("academy.doneNote"));
  const bool to_academy = vm.shell().academy.lesson_return == static_cast<std::uint8_t>(Screen::Academy);
  m.text(names::kLessonFinish, vm.tr(to_academy ? "academy.backToAcademy" : "academy.finish"));
}

void step_header(const AppViewModel& vm, const std::string& base, const LessonStep& step, ScreenModel& m) {
  const LessonRunner& r = vm.shell().lesson;
  const std::string review = r.mode() == LessonMode::Review ? std::string(kSep) + vm.tr("academy.review") : "";
  m.text(names::kLessonHead, vm.tr(base + ".title") + review + std::string(kSep) + vm.tr("academy.phase." + step.phase));
  m.text(names::kLessonStep, vm.tr("tut.step", {{"n", std::to_string(r.index() + 1)}, {"total", std::to_string(r.count())}}));
  m.text(names::kLessonTitle, vm.tr(base + "." + step.id + ".title"));
  m.text(names::kLessonBody, vm.tr(base + "." + step.id + ".body"));
}

}  // namespace

ScreenModel lesson(const AppViewModel& vm) {
  ScreenModel m;
  const LessonRunner& r = vm.shell().lesson;
  const AcademyLesson* l = r.lesson();
  m.text(names::kLessonLeave, vm.tr("academy.leave"));
  m.show(names::kLessonDonePanel, l != nullptr && r.finished());
  m.show(names::kLessonRunPanel, l == nullptr || !r.finished());
  if (l == nullptr || r.step() == nullptr) {
    m.text(names::kLessonTitle, vm.tr("academy.missing"));
    return m;
  }
  const std::string base = "academyLesson." + l->id;
  if (r.finished()) {
    done_screen(vm, base, m);
    return m;
  }
  const LessonStep& step = *r.step();
  const std::string step_base = base + "." + step.id;
  step_header(vm, base, step, m);
  m.show(names::kLessonChart, step.chart.has_value());
  m.show(names::kLessonBars, step.chart.has_value());
  m.show(names::kLessonChartNote, step.chart.has_value());
  if (step.chart) bar_table(vm, *step.chart, m);
  if (step.chart) m.name(names::kLessonChart, vm.tr(step_base + ".title") + ": " + vm.tr("academy.illustrative"));
  const StepQuestion* q = step.question ? &*step.question : nullptr;
  choices(vm, step_base, q, m);
  feedback(vm, step_base, q, m);
  lesson_widgets(vm, base, step, m);
  terms(vm, step, m);
  m.text(names::kLessonPrev, vm.tr("tut.prev"));
  m.enable(names::kLessonPrev, r.index() > 0);
  m.text(names::kLessonNext, vm.tr(r.last() ? "academy.complete" : "tut.next"));
  m.enable(names::kLessonNext, r.solved());
  return m;
}

}  // namespace tradelab::desktop::screens
