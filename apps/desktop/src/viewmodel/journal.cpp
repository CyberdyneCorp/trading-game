#include "viewmodel/journal.hpp"

#include <algorithm>

namespace tradelab::desktop {

std::string_view run_mode_name(RunMode m) {
  switch (m) {
    case RunMode::Campaign: return "campaign";
    case RunMode::Free: return "free";
    case RunMode::Practice: return "practice";
  }
  return "free";
}

std::string_view run_status_name(RunStatus s) {
  switch (s) {
    case RunStatus::Active: return "active";
    case RunStatus::Finished: return "finished";
    case RunStatus::Bankrupt: return "bankrupt";
    case RunStatus::Left: return "left";
  }
  return "active";
}

RunMode run_mode(const RunRecord& r) {
  if (r.campaign) return RunMode::Campaign;
  return r.pack_digest.empty() ? RunMode::Free : RunMode::Practice;
}

std::vector<JournalRun> journal_runs(const std::vector<RunRecord>& records, const std::string& mission) {
  std::vector<JournalRun> out;
  for (const auto& r : records) {
    if (r.journal.empty() || (!mission.empty() && r.mission_id != mission)) continue;
    JournalRun run{r.run_id, r.mission_id, r.seed,     r.created_at,  run_mode(r),
                   r.status, r.intel,      r.assisted, r.pack_digest, r.journal};
    std::ranges::stable_sort(run.entries, [](const PlanEntry& a, const PlanEntry& b) { return a.step < b.step; });
    out.push_back(std::move(run));
  }
  std::ranges::stable_sort(out, [](const JournalRun& a, const JournalRun& b) { return a.created_at > b.created_at; });
  return out;
}

std::vector<std::string> journal_missions(const std::vector<RunRecord>& records) {
  std::vector<std::string> out;
  for (const auto& r : records) {
    if (!r.journal.empty() && std::ranges::find(out, r.mission_id) == out.end()) out.push_back(r.mission_id);
  }
  return out;
}

}  // namespace tradelab::desktop
