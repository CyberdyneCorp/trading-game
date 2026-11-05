// Academy lesson (web LessonRunner.svelte): step header, illustrative chart (drawn by the view from
// lesson_chart()) with its screen-reader table of closed bars, the question's choices and feedback,
// and the end screen with the first-try score.
#include "viewmodel/format.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

std::string choice_text(const AppViewModel& vm, const std::string& base, const LessonChoice& c) {
  switch (c.kind) {
    case LessonChoice::Kind::Bar: return vm.tr("academy.bar", {{"n", std::to_string(c.bar)}});
    case LessonChoice::Kind::Pct: return fmt::fmt_pct(c.value / 100.0, vm.locale(), 2, true);
    case LessonChoice::Kind::Text: {
      const std::string key = base + ".choice." + c.id;
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
  m.text(names::kLessonChartNote, vm.tr("academy.illustrative") + std::string(kSep) +
                                      vm.tr("academy.market", {{"market", chart.market}, {"interval", chart.interval}}) +
                                      (chart.known > 0 ? std::string(kSep) + vm.tr("academy.future") : ""));
}

void question(const AppViewModel& vm, const std::string& base, const LessonStep& step, ScreenModel& m) {
  const LessonRunner& r = vm.shell().lesson;
  for (std::size_t k = 0; k < names::kLessonChoices.size(); ++k) {
    const bool shown = k < step.choices.size();
    m.show(names::kLessonChoices[k], shown);
    if (!shown) continue;
    const bool right = static_cast<int>(k) == step.answer;
    const bool picked = r.picked() == static_cast<int>(k);
    m.text(names::kLessonChoices[k], (picked ? (right ? "\xE2\x9C\x94 " : "\xE2\x9C\x96 ") : "") + choice_text(vm, base, step.choices[k]));
    m.enable(names::kLessonChoices[k], !r.solved() || right);
  }
  m.show(names::kLessonFeedback, r.picked().has_value());
  if (!r.picked()) return;
  const bool solved = r.solved();
  m.text(names::kLessonFeedback, vm.tr(solved ? "academy.right" : "academy.wrong") + " " +
                                     vm.tr(base + "." + step.id + (solved ? ".right" : ".wrong")));
}

void done_screen(const AppViewModel& vm, const std::string& base, ScreenModel& m) {
  const LessonRunner::Score s = vm.shell().lesson.score();
  m.text(names::kLessonDoneTitle, vm.tr("academy.doneTitle"));
  m.text(names::kLessonDoneName, vm.tr(base + ".title"));
  m.text(names::kLessonScore, vm.tr("academy.doneScore", {{"right", std::to_string(s.right)}, {"total", std::to_string(s.total)}}));
  m.text(names::kLessonDoneNote, vm.tr("academy.doneNote"));
  m.text(names::kLessonFinish, vm.tr("academy.finish"));
}

}  // namespace

ScreenModel lesson(const AppViewModel& vm) {
  ScreenModel m;
  const LessonRunner& r = vm.shell().lesson;
  const AcademyLesson* l = r.lesson();
  m.text(names::kLessonLeave, vm.tr("academy.leave"));
  m.show(names::kLessonDonePanel, l != nullptr && r.finished());
  m.show(names::kLessonRunPanel, l == nullptr || !r.finished());
  if (l == nullptr) {
    m.text(names::kLessonTitle, vm.tr("academy.missing"));
    return m;
  }
  const std::string base = "academyLesson." + l->id;
  if (r.finished()) {
    done_screen(vm, base, m);
    return m;
  }
  const LessonStep& step = *r.step();
  m.text(names::kLessonHead, vm.tr(base + ".title") + std::string(kSep) + vm.tr("academy.phase." + step.phase));
  m.text(names::kLessonStep, vm.tr("tut.step", {{"n", std::to_string(r.index() + 1)}, {"total", std::to_string(l->steps.size())}}));
  m.text(names::kLessonTitle, vm.tr(base + "." + step.id + ".title"));
  m.text(names::kLessonBody, vm.tr(base + "." + step.id + ".body"));
  m.show(names::kLessonChart, step.chart.has_value());
  m.show(names::kLessonBars, step.chart.has_value());
  m.show(names::kLessonChartNote, step.chart.has_value());
  if (step.chart) bar_table(vm, *step.chart, m);
  if (step.chart) m.name(names::kLessonChart, vm.tr(base + "." + step.id + ".title") + ": " + vm.tr("academy.illustrative"));
  question(vm, base, step, m);
  if (!step.question()) {
    for (auto w : names::kLessonChoices) m.show(w, false);
    m.show(names::kLessonFeedback, false);
  }
  m.text(names::kLessonPrev, vm.tr("tut.prev"));
  m.enable(names::kLessonPrev, r.index() > 0);
  m.text(names::kLessonNext, vm.tr(r.last() ? "academy.complete" : "tut.next"));
  m.enable(names::kLessonNext, r.solved());
  return m;
}

}  // namespace tradelab::desktop::screens
