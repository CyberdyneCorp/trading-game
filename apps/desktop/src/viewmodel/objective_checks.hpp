// Port of apps/web/src/lib/game/objectiveChecks.ts (names kept): the debrief reads the objectives of
// m22-m25 against the engine's report-only facts (scoring v14 episodes and bias findings;
// add-practice-and-advanced-strategies D12, D13). The engine computes every fact; this only says which
// objective each fact meets or misses. An objective without a reading here is not judged.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/json.hpp"

namespace tradelab::desktop {

struct ObjectiveReading {
  std::string code;         // engine ObjectiveCode
  bool met = false;         // web verdict 'met' | 'missed'
  std::optional<int> step;  // the bar the reading points to (a finding's step), when it has one
  bool operator==(const ObjectiveReading&) const = default;
};

// The mission's objectives this debrief can judge, in the briefing's order (tl_debrief JSON: its
// `missionRules.objectives`, `episodes` and `findings`).
std::vector<ObjectiveReading> objective_readings(const json::Value& debrief, double initial_balance);

// The bar a scoring v14 finding points to on the chart (its evidence's entry or add, else its step);
// none without evidence.
std::optional<int> finding_moment(const json::Value& finding);

}  // namespace tradelab::desktop
