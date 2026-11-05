// Curriculum map (web CurriculumMap.svelte): every stage in dependency order with its status,
// assessment missions, lessons and unlocks, plus the player's progress.
#include "model/content.hpp"
#include "viewmodel/campaign.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

std::string join(const std::vector<std::string>& parts) {
  std::string out;
  for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
  return out;
}

std::string stage_content(const AppViewModel& vm, const Stage& st) {
  const Progress& p = vm.progress();
  if (!st.missions.empty()) {
    std::vector<std::string> titles;
    for (const auto& m : st.missions) titles.push_back((contains(p.assessed, m) ? "\xE2\x9C\x93 " : "") + mission_title(vm, m));
    return vm.tr("curriculum.assessment", {{"missions", join(titles)}});
  }
  return vm.tr(st.lessons.empty() ? "curriculum.soonNote" : "curriculum.lessonNote");
}

std::string stage_unlocks(const AppViewModel& vm, const Stage& st) {
  if (st.unlocks.empty()) return {};
  std::vector<std::string> what;
  const std::string cap = std::to_string(web_content().locked_leverage_cap);
  for (const auto& u : st.unlocks) what.push_back(vm.tr("unlock." + u, {{"cap", cap}}));
  return vm.tr("curriculum.unlocks", {{"what", join(what)}});
}

void progress_lines(const AppViewModel& vm, ScreenModel& m) {
  const WebContent& c = web_content();
  const Progress& p = vm.progress();
  const CurriculumSummary sum = curriculum_summary(c, p.assessed, p.lessons_done);
  std::string played = vm.tr("campaign.played", {{"n", std::to_string(p.campaign_history.size())}});
  const std::string best = best_completed_capital(p.campaign_history);
  if (!best.empty()) played += std::string(kSep) + vm.tr("campaign.best", {{"capital", fmt::fmt_money(best, vm.locale())}});
  m.lines.push_back({names::kCurriculumProgress,
                     {{vm.tr("curriculum.progress"), Tone::Title},
                      {vm.tr("curriculum.summary", {{"passed", std::to_string(sum.passed)}, {"total", std::to_string(sum.with_content)}})},
                      {vm.tr("curriculum.completed", {{"n", std::to_string(p.completed.size())}, {"total", std::to_string(vm.missions().size())}})},
                      {played}}});
}

}  // namespace

ScreenModel curriculum(const AppViewModel& vm) {
  ScreenModel m;
  const WebContent& c = web_content();
  const Progress& p = vm.progress();
  m.text(names::kCurriculumTitle, vm.tr("curriculum.title"));
  m.text(names::kCurriculumBack, vm.tr("debrief.menu"));
  m.text(names::kCurriculumNote, vm.tr("curriculum.note"));
  progress_lines(vm, m);
  // The web shows stage cards; the desktop shows them as one table (a screen reader reads it by row).
  WidgetTable stages{names::kCurriculumStages,
                     {"#", vm.tr("desktop.curriculum.colStage"), vm.tr("desktop.curriculum.colStatus"),
                      vm.tr("desktop.curriculum.colContent"), vm.tr("desktop.curriculum.colUnlocks")},
                     {}};
  int i = 0;
  for (const auto& st : c.stages) {
    const StageStatus status = stage_status(c, st.id, p.assessed, p.lessons_done);
    stages.rows.push_back({std::to_string(++i), vm.tr("stage." + st.id),
                           vm.tr("curriculum.status." + std::string(stage_status_name(status))), stage_content(vm, st),
                           stage_unlocks(vm, st)});
  }
  m.tables.push_back(std::move(stages));
  m.name(names::kCurriculumStages, vm.tr("curriculum.title"));
  WidgetItems lessons{names::kCurriculumLessons, {}, vm.shell().lesson_selected};
  bool selected_done = false;
  for (const auto& st : c.stages) {
    for (const auto& id : st.lessons) {
      const bool done = contains(p.lessons_done, id);
      if (static_cast<int>(lessons.items.size()) == vm.shell().lesson_selected) selected_done = done;
      lessons.items.push_back(vm.tr("curriculum.lesson", {{"title", vm.tr("academyLesson." + id + ".title")}}) +
                              (done ? " \xE2\x9C\x93 " + vm.tr("curriculum.lessonDone") : ""));
    }
  }
  m.items.push_back(std::move(lessons));
  m.name(names::kCurriculumLessons, vm.tr("curriculum.lessonNote"));
  m.text(names::kCurriculumLessonStart, vm.tr(selected_done ? "curriculum.lessonRepeat" : "curriculum.lessonStart"));
  return m;
}

}  // namespace tradelab::desktop::screens
