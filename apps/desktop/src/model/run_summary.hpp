// Port of apps/web/src/lib/game/runSummary.ts (names kept; openspec add-run-sharing-and-journal-stats
// design D7): the compact copy of a run's engine debrief that the journal statistics read, stored as
// the web's RunRecord.summary JSON in runs.summary.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/json.hpp"

namespace tradelab::desktop {

struct TradeSummary {
  double net = 0;
  std::optional<std::int64_t> r_bp;
  std::optional<double> risk_pct;
  std::string reason;
  std::optional<bool> respected;
  std::optional<std::int64_t> moved_away;
  bool option = false;
};

struct FindingSummary {
  std::string code;
  std::string tone;
};

struct RunSummary {
  std::string mission;
  std::string status;  // finished | bankrupt
  double initial = 0;
  double net = 0;
  std::optional<std::int64_t> score;
  std::vector<TradeSummary> trades;
  std::vector<FindingSummary> findings;
};

// From a tl_debrief object; nullopt while the run is active.
std::optional<RunSummary> summarize(const json::Value& debrief);
// The summary as the web stores it (RunRecord.summary JSON, "v":1), and back.
std::string summary_json(const RunSummary& s);
std::optional<RunSummary> summary_from_json(const json::Value& v);

}  // namespace tradelab::desktop
