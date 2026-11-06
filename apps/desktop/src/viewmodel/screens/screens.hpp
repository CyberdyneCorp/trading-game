// One function per shell screen, building its ScreenModel from the AppViewModel's public state (the
// matching web component is named on each). Plain C++: unit-tested without a window.
#pragma once

#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_model.hpp"
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

// AssistPanel.svelte, shown in the terminal and in the debrief under their own widget names.
void assist_panel(const AppViewModel& vm, const names::AssistNames& n, ScreenModel& m);

// Shared helpers.
std::string mission_title(const AppViewModel& vm, const std::string& id);
inline constexpr std::string_view kSep = " \xC2\xB7 ";  // " · "

}  // namespace tradelab::desktop::screens
