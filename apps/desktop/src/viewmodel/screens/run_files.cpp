// Run files on the shell screens (web RunExport/RunImport, the journal's imported runs, ReplayScreen's
// tags and the debrief in viewing mode; add-run-sharing-and-journal-stats D2, D6, D7).
#include "viewmodel/run_file_names.hpp"
#include "viewmodel/screens/screens.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::screens {

namespace {

std::string run_item(const AppViewModel& vm, const RunRecord& r) {
  return mission_title(vm, r.mission_id) + std::string(kSep) + "seed " + std::to_string(r.seed) + std::string(kSep) +
         vm.tr("journal.status." + std::string(r.status == RunStatus::Finished   ? "finished"
                                               : r.status == RunStatus::Bankrupt ? "bankrupt"
                                               : r.status == RunStatus::Left     ? "left"
                                                                                 : "active"));
}

void files_panel(const AppViewModel& vm, ScreenModel& m) {
  const RunFileState& f = vm.run_files();
  m.text(names::kFilesTitle, vm.tr("desktop.runfile.title"));
  m.text(names::kFilesNote, vm.tr("desktop.runfile.note"));
  m.text(names::kFilesPathLabel, vm.tr("desktop.runfile.path"));
  m.text(names::kFilesPath, f.path);
  m.name(names::kFilesPath, vm.tr("desktop.runfile.path"));
  m.text(names::kFilesRunLabel, vm.tr("desktop.runfile.run"));
  const auto runs = vm.exportable_runs();
  WidgetItems items{names::kFilesRun, {}, f.run};
  for (const RunRecord* r : runs) items.items.push_back(run_item(vm, *r));
  m.items.push_back(std::move(items));
  m.name(names::kFilesRun, vm.tr("desktop.runfile.run"));
  m.text(names::kFilesNotes, vm.tr("runfile.includeNotes"));
  m.check(names::kFilesNotes, f.notes);
  m.text(names::kFilesNotesHint, vm.tr("runfile.notesHint"));
  m.text(names::kFilesExport, "\xE2\x87\xAA " + vm.tr("runfile.export"));
  m.text(names::kFilesWatch, "\xE2\x96\xB6 " + vm.tr("journal.watchOwn"));
  m.text(names::kFilesImport, "\xE2\x87\xA9 " + vm.tr("desktop.runfile.import"));
  const bool ready = vm.run_files_available() && !vm.waiting();
  m.enable(names::kFilesExport, ready && !runs.empty());
  m.enable(names::kFilesWatch, ready && !runs.empty());
  m.enable(names::kFilesImport, ready && !f.path.empty());
  m.text(names::kFilesMessage, f.message);
  m.show(names::kFilesMessage, !f.message.empty());
}

void imports_panel(const AppViewModel& vm, ScreenModel& m) {
  const RunFileState& f = vm.run_files();
  m.text(names::kImportsTitle, vm.tr("journal.imports"));
  m.text(names::kImportsNote, vm.tr("journal.importsNote"));
  m.text(names::kImportsEmpty, vm.tr("journal.importsEmpty"));
  m.show(names::kImportsEmpty, f.imports.empty());
  m.show(names::kImportsRow, !f.imports.empty());
  WidgetItems items{names::kImports, {}, f.imported};
  for (const storage::ImportRecord& r : f.imports) {
    items.items.push_back("[" + vm.tr("runfile.tagImported") + "] " + mission_title(vm, r.mission) + std::string(kSep) + "seed " +
                          std::to_string(r.seed) + std::string(kSep) + vm.tr("journal.status." + r.status));
  }
  m.items.push_back(std::move(items));
  m.name(names::kImports, vm.tr("journal.imports"));
  m.text(names::kImportWatch, "\xE2\x96\xB6 " + vm.tr("runfile.watch"));
  m.text(names::kImportRemove, vm.tr("runfile.remove"));
  m.enable(names::kImportWatch, !vm.waiting());
}

}  // namespace

void journal_run_files(const AppViewModel& vm, ScreenModel& m) {
  files_panel(vm, m);
  imports_panel(vm, m);
}

void stats_run_files(const AppViewModel& vm, ScreenModel& m) {
  int missing = 0;
  for (const RunRecord& r : vm.progress().runs) {
    const bool ended = r.status == RunStatus::Finished || r.status == RunStatus::Bankrupt;
    missing += ended && !r.assisted && r.summary.empty() ? 1 : 0;
  }
  const std::string key = missing == 1 ? "stats.missingOne" : "stats.missing";
  m.text(names::kStatsMissing, vm.tr(key, {{"n", std::to_string(missing)}}));
  m.show(names::kStatsMissing, missing > 0);
  m.show(names::kStatsRecompute, missing > 0 && vm.run_files_available());
  m.text(names::kStatsRecompute, vm.tr("stats.recompute"));
  m.enable(names::kStatsRecompute, !vm.run_files().recomputing);
  m.text(names::kStatsMessage, vm.run_files().stats_message);
  m.show(names::kStatsMessage, !vm.run_files().stats_message.empty());
}

// The terminal while the replay viewer plays a run: its tag with the bars played, and the way out.
void game_replay(const AppViewModel& vm, ScreenModel& m) {
  const bool on = vm.viewing();
  m.show(names::kGameReplayTag, on);
  m.show(names::kGameReplayLeave, on);
  if (!on) return;
  const Snapshot* s = vm.snapshot();
  const bool imported = vm.run_files().viewing->imported;
  std::string tag = "[" + vm.tr("replay.title") + "] ";
  if (s != nullptr) tag += vm.tr("replay.bar", {{"bar", std::to_string(s->watch_bar)}, {"bars", std::to_string(s->watch_bars)}});
  if (imported) tag += std::string(kSep) + vm.tr("runfile.tagImported");
  if (s != nullptr && s->watch_done && s->active()) tag += std::string(kSep) + vm.tr("replay.endActive");
  m.text(names::kGameReplayTag, tag);
  m.name(names::kGameReplayTag, imported ? vm.tr("replay.imported") : vm.tr("replay.own"));
  m.text(names::kGameReplayLeave, vm.tr("replay.back"));
}

// The debrief of a watched run: labeled, without the player's own controls (play again, campaign,
// rewind: the assist panel is hidden by the empty run id), and "Leave replay" in place of the menu.
void debrief_replay(const AppViewModel& vm, ScreenModel& m) {
  const bool on = vm.viewing();
  m.show(names::kDebriefViewing, on);
  if (!on) return;
  m.text(names::kDebriefViewing, (vm.run_files().viewing->imported ? "[" + vm.tr("runfile.tagImported") + "] " : std::string()) +
                                     vm.tr("debrief.viewing"));
  m.show(names::kDebriefAgain, false);
  m.show(names::kDebriefCampaignPanel, false);
  m.text(names::kDebriefMenu, vm.tr("replay.back"));
}

}  // namespace tradelab::desktop::screens
