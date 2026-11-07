// Names of the run-file widgets and actions (add-run-sharing-and-journal-stats): the journal's run-file
// panel and imported runs, the statistics' recomputation, and the replay viewer's parts of the terminal
// and the debrief. The headless test checks that every one exists in the loaded views.
#pragma once

#include <array>
#include <string_view>

namespace tradelab::desktop::names {

inline constexpr std::string_view kFilesTitle = "journal-files-title";
inline constexpr std::string_view kFilesNote = "journal-files-note";
inline constexpr std::string_view kFilesPathLabel = "journal-files-path-label";
inline constexpr std::string_view kFilesPath = "journal-files-path";
inline constexpr std::string_view kFilesRunLabel = "journal-files-run-label";
inline constexpr std::string_view kFilesRun = "journal-files-run";
inline constexpr std::string_view kFilesNotes = "journal-files-notes";
inline constexpr std::string_view kFilesNotesHint = "journal-files-notes-hint";
inline constexpr std::string_view kFilesExport = "journal-files-export";
inline constexpr std::string_view kFilesWatch = "journal-files-watch";
inline constexpr std::string_view kFilesImport = "journal-files-import";
inline constexpr std::string_view kFilesMessage = "journal-files-message";
inline constexpr std::string_view kImportsTitle = "journal-imports-title";
inline constexpr std::string_view kImportsNote = "journal-imports-note";
inline constexpr std::string_view kImportsEmpty = "journal-imports-empty";
inline constexpr std::string_view kImportsRow = "journal-imports-row";
inline constexpr std::string_view kImports = "journal-imports";
inline constexpr std::string_view kImportWatch = "journal-import-watch";
inline constexpr std::string_view kImportRemove = "journal-import-remove";
inline constexpr std::string_view kStatsMissing = "stats-missing";
inline constexpr std::string_view kStatsRecompute = "stats-recompute";
inline constexpr std::string_view kStatsMessage = "stats-message";
inline constexpr std::string_view kGameReplayTag = "game-replay-tag";
inline constexpr std::string_view kGameReplayLeave = "game-replay-leave";
inline constexpr std::string_view kDebriefViewing = "debrief-viewing";

inline constexpr auto kRunFileWidgets = std::to_array<std::string_view>(
    {kFilesTitle, kFilesNote, kFilesPathLabel, kFilesPath, kFilesRunLabel, kFilesRun, kFilesNotes, kFilesNotesHint, kFilesExport,
     kFilesWatch, kFilesImport, kFilesMessage, kImportsTitle, kImportsNote, kImportsEmpty, kImportsRow, kImports, kImportWatch,
     kImportRemove, kStatsMissing, kStatsRecompute, kStatsMessage, kGameReplayTag, kGameReplayLeave, kDebriefViewing});

inline constexpr std::string_view kActFilesExport = "tl.files.export";
inline constexpr std::string_view kActFilesWatch = "tl.files.watch";
inline constexpr std::string_view kActFilesImport = "tl.files.import";
inline constexpr std::string_view kActImportWatch = "tl.files.watchImport";
inline constexpr std::string_view kActImportRemove = "tl.files.removeImport";
inline constexpr std::string_view kActStatsRecompute = "tl.stats.recompute";
inline constexpr std::string_view kActReplayLeave = "tl.replay.leave";

inline constexpr auto kRunFileActions = std::to_array<std::string_view>(
    {kActFilesExport, kActFilesWatch, kActFilesImport, kActImportWatch, kActImportRemove, kActStatsRecompute, kActReplayLeave});

}  // namespace tradelab::desktop::names
