// One function per shell screen, building its ScreenModel from the AppViewModel's public state (the
// matching web component is named on each). Plain C++: unit-tested without a window.
#pragma once

#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_model.hpp"

namespace tradelab::desktop::screens {

ScreenModel menu(const AppViewModel& vm);        // MissionMenu.svelte + CampaignPanel.svelte
ScreenModel briefing(const AppViewModel& vm);    // Briefing.svelte
ScreenModel debrief(const AppViewModel& vm);     // Debrief.svelte and its panels
ScreenModel curriculum(const AppViewModel& vm);  // CurriculumMap.svelte
ScreenModel journal(const AppViewModel& vm);     // Journal.svelte + JournalEntries.svelte
ScreenModel lesson(const AppViewModel& vm);      // LessonRunner.svelte + LessonChart.svelte (bar table)
ScreenModel settings(const AppViewModel& vm);    // menu settings + ShortcutSettings.svelte (+ desktop CRT, text scale)

// Shared helpers.
std::string mission_title(const AppViewModel& vm, const std::string& id);
inline constexpr std::string_view kSep = " \xC2\xB7 ";  // " · "

}  // namespace tradelab::desktop::screens
