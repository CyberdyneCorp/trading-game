// One function per shell screen, building its ScreenModel from the AppViewModel's public state (the
// matching web component is named on each). Plain C++: unit-tested without a window.
#pragma once

#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/journal_stats.hpp"
#include "viewmodel/screen_model.hpp"
#include "viewmodel/run_file_names.hpp"
#include "viewmodel/screen_names.hpp"

namespace tradelab::desktop::screens {

ScreenModel menu(const AppViewModel& vm);        // MissionMenu.svelte + CampaignPanel.svelte
ScreenModel briefing(const AppViewModel& vm);    // Briefing.svelte
ScreenModel debrief(const AppViewModel& vm);     // Debrief.svelte and its panels
ScreenModel curriculum(const AppViewModel& vm);  // CurriculumMap.svelte
ScreenModel journal(const AppViewModel& vm);     // Journal.svelte + JournalEntries.svelte
ScreenModel lesson(const AppViewModel& vm);      // LessonRunner.svelte + LessonChart.svelte (bar table)
ScreenModel settings(const AppViewModel& vm);    // menu settings + ShortcutSettings.svelte (+ desktop CRT, text scale)
ScreenModel tutorial(const AppViewModel& vm);    // Tutorial.svelte (before the briefing)
ScreenModel catalog(const AppViewModel& vm);     // Catalog.svelte + the installed packs (MissionMenu practice panel)
ScreenModel csv_import(const AppViewModel& vm);  // CsvImport.svelte
ScreenModel game(const AppViewModel& vm);        // the terminal's shell parts: assisted tag and AssistPanel.svelte
ScreenModel academy(const AppViewModel& vm);     // Academy.svelte
ScreenModel glossary(const AppViewModel& vm);    // Glossary.svelte + GlossaryList.svelte
ScreenModel stats(const AppViewModel& vm);       // JournalStats.svelte (add-run-sharing-and-journal-stats D7)

// The runs the journal statistics count: own runs with a summary, never assisted, oldest first.
std::vector<StatsRun> stats_runs(const Progress& p);

// A marked glossary term's in-place definition (Term.svelte): "<term>: <short definition>" ("" when
// the glossary has no such term). The view shows it as the tooltip and the accessible description.
std::string term_tip(const AppViewModel& vm, std::string_view id);
// The lesson widgets (LessonTicket / LessonTable / LessonPayoff, the chart's levels) and the question's
// number and set rows (screens/lesson_widgets.cpp).
void lesson_widgets(const AppViewModel& vm, const std::string& base, const LessonStep& step, ScreenModel& m);
// The Academy's parts of other screens (screens/academy.cpp): the menu's Academy and glossary buttons
// and review prompt, the map's Academy link, the briefing's guided tour, the debrief's recommended
// lesson and coach step, and the terminal's coach marks and marked terms.
void academy_menu(const AppViewModel& vm, ScreenModel& m);
void academy_curriculum(const AppViewModel& vm, ScreenModel& m);
void academy_briefing(const AppViewModel& vm, ScreenModel& m);
void academy_debrief(const AppViewModel& vm, ScreenModel& m);
void academy_game(const AppViewModel& vm, ScreenModel& m);
// The guided first trade's callout (CoachMarks.svelte) in the terminal or the debrief.
void coach_callout(const AppViewModel& vm, bool debrief, ScreenModel& m);

// Run files (screens/run_files.cpp): the journal's run-file panel and imports, the statistics'
// recomputation, and the replay viewer's parts of the terminal and the debrief.
void journal_run_files(const AppViewModel& vm, ScreenModel& m);
void stats_run_files(const AppViewModel& vm, ScreenModel& m);
void game_replay(const AppViewModel& vm, ScreenModel& m);
void debrief_replay(const AppViewModel& vm, ScreenModel& m);

// AssistPanel.svelte, shown in the terminal and in the debrief under their own widget names.
void assist_panel(const AppViewModel& vm, const names::AssistNames& n, ScreenModel& m);

// Shared helpers.
std::string mission_title(const AppViewModel& vm, const std::string& id);
inline constexpr std::string_view kSep = " \xC2\xB7 ";  // " · "

}  // namespace tradelab::desktop::screens
