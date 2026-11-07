// Port of apps/web/src/lib/game/runSummary.ts and journalStats.ts (names kept; openspec
// add-run-sharing-and-journal-stats design D7): the per-run summary copied from the engine's debrief
// and the statistics over the player's own runs. Checked against the web parity fixtures.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/content.hpp"
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

struct StatsRun {
  std::string run_id;
  std::string mission_id;
  std::int64_t created_at = 0;
  RunSummary summary;
};

constexpr int kRBuckets = 6;  // <= -2, (-2,-1], (-1,0], (0,1], (1,2], > 2
int r_bucket(double r);

struct CoreStats {
  int runs = 0;
  int trades = 0;
  int wins = 0;
  std::optional<double> win_rate;
  int with_r = 0;
  int without_r = 0;
  std::optional<double> expectancy_r;
  std::optional<double> avg_win_r;
  std::optional<double> avg_loss_r;
  std::vector<int> r_buckets = std::vector<int>(kRBuckets, 0);
  int stops_judged = 0;
  int stops_respected = 0;
  std::optional<double> respected_rate;
  int stops_reported = 0;
  int moved_away = 0;
  std::optional<double> moved_away_rate;
  int risk_trades = 0;
  std::optional<double> avg_risk_pct;
  int risk_below1 = 0;
  int risk_1to2 = 0;
  int risk_above2 = 0;
  int oversized_runs = 0;
  int liquidations = 0;
  int margin_calls = 0;
};

CoreStats core_stats(const std::vector<StatsRun>& runs);

struct MistakeRow {
  std::string code;
  std::string tone;
  int runs = 0;
  std::int64_t last_at = 0;
  bool repeats = false;
  std::optional<std::string> lesson;
};
std::vector<MistakeRow> mistakes(const std::vector<StatsRun>& runs);
std::optional<std::string> finding_lesson(const std::string& code);

struct GroupRow {
  std::string key;
  int runs = 0;
  int trades = 0;
  std::optional<double> win_rate;
  std::optional<double> expectancy_r;
};
inline constexpr const char* kPracticeStage = "practice";
std::vector<GroupRow> by_mission(const std::vector<StatsRun>& runs);
std::vector<GroupRow> by_stage(const WebContent& c, const std::vector<StatsRun>& runs);

inline constexpr int kTrendWindow = 10;
struct TrendPoint {
  std::string run_id;
  std::string mission_id;
  std::int64_t created_at = 0;
  int trades = 0;
  std::optional<double> expectancy_r;
  std::optional<double> respected_rate;
  std::optional<std::int64_t> score;
};
struct Trend {
  std::optional<CoreStats> recent;
  std::optional<CoreStats> previous;
  std::vector<TrendPoint> points;
};
Trend trend(const std::vector<StatsRun>& runs);

struct JournalStats {
  CoreStats core;
  std::vector<MistakeRow> mistakes;
  std::vector<GroupRow> by_mission;
  std::vector<GroupRow> by_stage;
  Trend trend;
  bool too_few = true;
};
JournalStats journal_stats(const WebContent& c, const std::vector<StatsRun>& all, const std::optional<std::string>& mission = std::nullopt);

}  // namespace tradelab::desktop
