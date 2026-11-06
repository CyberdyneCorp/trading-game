// Port of apps/web/src/lib/game/journal.ts (names kept): the plans the player wrote, run by run. The
// free text stays on this device and is never scored.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/progress.hpp"

namespace tradelab::desktop {

enum class RunMode : std::uint8_t { Campaign, Free, Practice };

std::string_view run_mode_name(RunMode m);      // "campaign", "free", "practice"
std::string_view run_status_name(RunStatus s);  // "active", "finished", "bankrupt", "left"

struct JournalRun {
  std::string run_id;
  std::string mission_id;
  std::uint32_t seed = 0;
  std::int64_t created_at = 0;
  RunMode mode = RunMode::Free;
  RunStatus status = RunStatus::Active;
  bool intel = false;
  bool assisted = false;
  std::string pack_digest;
  std::vector<PlanEntry> entries;  // by bar
};

RunMode run_mode(const RunRecord& r);
// Runs with at least one journal entry, newest first, optionally of one mission only ("" = all).
std::vector<JournalRun> journal_runs(const std::vector<RunRecord>& records, const std::string& mission = {});
// Missions that have journal entries, in first-seen order (for the filter).
std::vector<std::string> journal_missions(const std::vector<RunRecord>& records);

}  // namespace tradelab::desktop
