// The Academy screen (web Academy.svelte), the glossary screen (Glossary.svelte + GlossaryList.svelte)
// and the Academy's parts of other screens: the menu's buttons and review prompt (ReviewPrompt.svelte),
// the map's Academy link, the briefing's and help's guided tour, the debrief's recommended lesson, the
// coach marks of the guided first trade (CoachMarks.svelte) and the terminal's marked terms (Term.svelte).
#include <algorithm>

#include "viewmodel/academy/glossary.hpp"
#include "viewmodel/academy/review.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

// tCount: the "One" text for 1 (web i18n tCount).
std::string tr_count(const AppViewModel& vm, const std::string& key, std::size_t n) {
  return vm.tr(n == 1 && vm.catalog().has(key + "One") ? key + "One" : key, {{"n", std::to_string(n)}});
}

std::string lesson_title(const AppViewModel& vm, const std::string& id) { return vm.tr("academyLesson." + id + ".title"); }

// A stage's name without its subtitle ("Futuros II · margem, alavancagem e liquidação" -> "Futuros II"):
// the full name pushed the lesson title and its status past the list's right edge.
std::string short_stage(const AppViewModel& vm, const std::string& stage) {
  std::string name = vm.tr("stage." + stage);
  if (const auto cut = name.find(kSep); cut != std::string::npos) name.resize(cut);
  return name;
}

std::vector<std::pair<std::string, std::string>> academy_rows() {  // stage, lesson
  std::vector<std::pair<std::string, std::string>> out;
  for (const auto& s : web_content().stages) {
    for (const auto& l : s.lessons) out.emplace_back(s.id, l);
  }
  return out;
}

std::string_view lesson_status(const AppViewModel& vm, const std::vector<std::string>& due, const std::string& id) {
  if (contains(due, id)) return "due";
  return contains(vm.progress().lessons_done, id) ? "done" : "new";
}

void due_box(const AppViewModel& vm, const std::vector<std::string>& due, ScreenModel& m) {
  m.show(names::kAcademyDueBox, !due.empty());
  m.text(names::kAcademyDueTitle, vm.tr("academy.dueTitle", {{"n", std::to_string(due.size())}}));
  WidgetItems items{names::kAcademyDue, {}, vm.shell().academy.due_selected};
  for (const auto& id : due) items.items.push_back(lesson_title(vm, id));
  m.items.push_back(std::move(items));
  m.name(names::kAcademyDue, vm.tr("academy.dueTitle", {{"n", std::to_string(due.size())}}));
  m.text(names::kAcademyReviewStart, vm.tr("academy.reviewStart"));
}

std::string related_name(const AppViewModel& vm, const std::string& id) { return vm.tr("glossary." + id + ".term"); }

void glossary_entry(const AppViewModel& vm, const std::string& id, ScreenModel& m) {
  const GlossaryEntry* g = web_content().term(id);
  m.show(names::kGlossaryEntry, g != nullptr);
  m.show(names::kGlossaryPick, g == nullptr);
  m.text(names::kGlossaryPick, vm.tr("glossary.pick"));
  if (g == nullptr) return;
  const academy::TermText t = academy::term_text(vm.catalog(), id);
  m.text(names::kGlossaryTerm, t.term);
  m.text(names::kGlossaryStage, vm.tr("stage." + g->stage));
  m.text(names::kGlossaryShort, t.short_text);
  m.text(names::kGlossaryLong, t.long_text);
  m.show(names::kGlossaryExample, !t.example.empty());
  m.text(names::kGlossaryExample, vm.tr("glossary.example") + ": " + t.example);
  std::string aliases;
  for (const auto& a : t.aliases) aliases += (aliases.empty() ? "" : ", ") + a;
  m.show(names::kGlossaryAliases, !aliases.empty());
  m.text(names::kGlossaryAliases, vm.tr("glossary.aliases") + ": " + aliases);
  m.show(names::kGlossaryRelatedRow, !g->related.empty());
  m.text(names::kGlossaryRelatedLabel, vm.tr("glossary.related") + ":");
  for (std::size_t i = 0; i < names::kGlossaryRelated.size(); ++i) {
    const bool shown = i < g->related.size();
    m.show(names::kGlossaryRelated[i], shown);
    if (!shown) continue;
    m.text(names::kGlossaryRelated[i], related_name(vm, g->related[i]));
    m.tip(names::kGlossaryRelated[i], term_tip(vm, g->related[i]));
  }
}

// Marked terms of the terminal (Term.svelte in OrderTicket, RightRail and OptionChain).
struct MarkedTerm {
  std::string_view widget;
  std::array<std::string_view, 3> terms;
};
constexpr std::array<MarkedTerm, 11> kTerminalTerms = {{
    {names::game::kLevLabel, {"leverage"}},
    {names::game::kLeverage, {"leverage"}},
    {names::game::kStop, {"stop-loss"}},
    {names::game::kTp, {"take-profit"}},
    {names::game::kEstimate, {"initial-margin", "liquidation-price", "risk-budget"}},
    {names::game::kRailEquity, {"equity"}},
    {names::game::kRailLiq, {"liquidation-price"}},
    {names::game::kRailFunding, {"funding-rate"}},
    {names::game::kOptSeries, {"option-chain", "strike", "expiry"}},
    {names::game::kOptEstimate, {"premium"}},
    {names::game::kChain, {"option-chain", "moneyness"}},
}};

std::string terms_tip(const AppViewModel& vm, const MarkedTerm& mt) {
  std::string tip;
  for (std::string_view id : mt.terms) {
    if (!id.empty()) tip += (tip.empty() ? "" : "\n") + term_tip(vm, id);
  }
  return tip;
}

void terminal_terms(const AppViewModel& vm, ScreenModel& m) {
  for (const auto& mt : kTerminalTerms) m.tip(mt.widget, terms_tip(vm, mt));
}

// The coach targets' descriptions: the step's text while it shows, else their terms (or nothing).
void coach_targets(const AppViewModel& vm, ScreenModel& m) {
  for (std::string_view target : {"chart", "ticket", "stop", "risk", "advance"}) {
    const std::string_view w = names::coach_anchor(target);
    const auto* mt = std::ranges::find_if(kTerminalTerms, [w](const MarkedTerm& t) { return t.widget == w; });
    m.describe(w, mt != kTerminalTerms.end() ? terms_tip(vm, *mt) : std::string());
  }
}

}  // namespace

std::string term_tip(const AppViewModel& vm, std::string_view id) {
  const academy::TermText t = academy::term_text(vm.catalog(), id);
  return t.term.empty() ? std::string() : t.term + ": " + t.short_text;
}

// ---- Academy screen ----

ScreenModel academy(const AppViewModel& vm) {
  ScreenModel m;
  const auto due = vm.reviews_due();
  m.text(names::kAcademyTitle, vm.tr("academy.title"));
  m.text(names::kAcademyGlossary, vm.tr("glossary.open"));
  m.text(names::kAcademyBack, vm.tr("debrief.menu"));
  m.text(names::kAcademyIntro, vm.tr("academy.intro"));
  due_box(vm, due, m);
  const auto rows = academy_rows();
  const int selected = vm.shell().academy.academy_selected;
  WidgetItems list{names::kAcademyLessons, {}, selected};
  std::string_view selected_status = "new";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& [stage, id] = rows[i];
    const std::string_view status = lesson_status(vm, due, id);
    if (static_cast<int>(i) == selected) selected_status = status;
    const bool retired = academy::is_retired(vm.progress().lesson_reviews, id);
    list.items.push_back(short_stage(vm, stage) + std::string(kSep) + lesson_title(vm, id) + " [" +
                         vm.tr("academy.status." + std::string(status)) + "]" + (retired ? " " + vm.tr("academy.retired") : ""));
  }
  m.items.push_back(std::move(list));
  m.name(names::kAcademyLessons, vm.tr("academy.title"));
  if (selected >= 0 && selected < static_cast<int>(rows.size())) {
    const auto& [stage, id] = rows[static_cast<std::size_t>(selected)];
    m.text(names::kAcademyDetail,
           vm.tr("stage." + stage) + "\n" + lesson_title(vm, id) + ": " + vm.tr("academyLesson." + id + ".summary"));
  }
  m.text(names::kAcademyStart, vm.tr(selected_status == "new" ? "curriculum.lessonStart" : "curriculum.lessonRepeat"));
  return m;
}

// ---- Glossary screen ----

ScreenModel glossary(const AppViewModel& vm) {
  ScreenModel m;
  const AcademyState& a = vm.shell().academy;
  m.text(names::kGlossaryTitle, vm.tr("glossary.title"));
  m.text(names::kGlossaryBack, vm.tr("glossary.back"));
  m.text(names::kGlossaryNote, vm.tr("glossary.note"));
  m.text(names::kGlossarySearchLabel, vm.tr("glossary.search"));
  m.text(names::kGlossarySearch, a.glossary_query);
  m.name(names::kGlossarySearch, vm.tr("glossary.search"));
  const auto hits = vm.glossary_hits();
  m.text(names::kGlossaryCount, tr_count(vm, "glossary.count", hits.size()));
  WidgetItems list{names::kGlossaryList, {}, -1};
  for (std::size_t i = 0; i < hits.size(); ++i) {
    const bool seen = contains(vm.progress().glossary_seen, hits[i]);
    list.items.push_back(related_name(vm, hits[i]) + (seen ? "" : " \xC2\xB7 " + vm.tr("glossary.new")));
    if (hits[i] == a.glossary_term) list.selected = static_cast<int>(i);
  }
  m.items.push_back(std::move(list));
  m.name(names::kGlossaryList, vm.tr("glossary.title"));
  glossary_entry(vm, a.glossary_term, m);
  return m;
}

// ---- Parts of other screens ----

void academy_menu(const AppViewModel& vm, ScreenModel& m) {
  m.text(names::kMenuAcademy, vm.tr("academy.open")); 
  m.text(names::kMenuGlossary, vm.tr("glossary.open"));
  const bool prompt = vm.review_prompt_shown();
  m.show(names::kMenuReviewRow, prompt);
  if (!prompt) return;
  m.text(names::kMenuReviewText, tr_count(vm, "academy.reviewPrompt", vm.reviews_due().size()));
  m.text(names::kMenuReviewOpen, vm.tr("academy.reviewOpen"));
  m.text(names::kMenuReviewDismiss, vm.tr("academy.reviewDismiss"));
}

void academy_curriculum(const AppViewModel& vm, ScreenModel& m) {
  m.text(names::kCurriculumAcademy, vm.tr("academy.open"));
}

void academy_briefing(const AppViewModel& vm, ScreenModel& m) {
  m.text(names::kBriefTour, vm.tr("coach.replay"));
}

void academy_debrief(const AppViewModel& vm, ScreenModel& m) {
  const std::string lesson = vm.debrief_lesson();
  m.show(names::kDebriefOpenLesson, !lesson.empty());
  if (!lesson.empty()) m.text(names::kDebriefOpenLesson, vm.tr("debrief.openLesson", {{"title", lesson_title(vm, lesson)}}));
  m.describe(names::coach_anchor("debrief"), "");
  coach_callout(vm, true, m);
}

void academy_game(const AppViewModel& vm, ScreenModel& m) {
  m.text(names::kGameHelpTour, vm.tr("coach.replay"));
  terminal_terms(vm, m);
  coach_targets(vm, m);
  coach_callout(vm, false, m);
}

// The callout shows the terminal's steps in the terminal (not while one of its dialogs is open: a
// calibration question or the plan come first) and the debrief's step in that run's debrief.
void coach_callout(const AppViewModel& vm, bool debrief, ScreenModel& m) {
  const academy::CoachModel& coach = vm.coach();
  const CoachStepDef* step = coach.step();
  const bool here = step != nullptr && (step->target == "debrief") == debrief;
  const bool visible = here && (debrief || vm.game().modal() == game::Modal::None);
  const std::string_view box = debrief ? names::kDebriefCoach : names::kGameCoach;
  m.show(box, visible);
  if (!debrief) {
    m.show(names::kGameCoachLayer, visible);
    m.show(names::kGameCoachOutline, visible);
  }
  if (!visible) return;
  const std::string n = std::to_string(*coach.index() + 1);
  const std::string total = std::to_string(coach.count());
  const std::string title = vm.tr("coach." + step->id + ".title");
  m.text(debrief ? names::kDebriefCoachStep : names::kGameCoachStep,
         vm.tr("coach.title") + std::string(kSep) + vm.tr("coach.stepOf", {{"n", n}, {"total", total}}));
  m.text(debrief ? names::kDebriefCoachTitle : names::kGameCoachTitle, title);
  m.text(debrief ? names::kDebriefCoachBody : names::kGameCoachBody, vm.tr("coach." + step->id + ".body"));
  m.text(debrief ? names::kDebriefCoachSkip : names::kGameCoachSkip, vm.tr("coach.skip"));
  m.text(debrief ? names::kDebriefCoachNext : names::kGameCoachNext, vm.tr(coach.last() ? "coach.done" : "coach.next"));
  m.name(box, title);
  // The target is described by the callout's text (web aria-describedby="coach-body").
  m.describe(names::coach_anchor(step->target), vm.tr("coach." + step->id + ".body"));
}

}  // namespace tradelab::desktop::screens
