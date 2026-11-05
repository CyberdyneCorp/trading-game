// Port of apps/web/src/lib/game/campaign.ts and campaignReport.ts (names kept). A campaign run plays
// the curriculum's missions in order, each starting from the capital the previous one ended with (the
// engine's final equity, passed back as the startingBalance start option). Bankruptcy, or less than
// the campaign minimum left, ends it; finishing a mission passes its stages for good.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/content.hpp"
#include "model/json.hpp"
#include "model/progress.hpp"

namespace tradelab::desktop {

CampaignRun new_campaign(const WebContent& c, std::string id, std::int64_t now);
// The mission the active campaign run plays next, or "" when there is none.
std::string next_campaign_mission(const WebContent& c, const std::optional<CampaignRun>& run);
// Starts a new campaign run; an active one is ended as abandoned (kept in the history).
Progress start_campaign(const WebContent& c, Progress p, std::string id, std::int64_t now);
// Ends the active campaign run by the player's choice; assessed stages stay.
Progress abandon_campaign(const WebContent& c, Progress p);

struct RunOutcome {
  std::string run_id;
  std::string mission_id;
  LegResult result = LegResult::Finished;
  std::string equity;  // final equity, or the last known one of a left run
  std::optional<LegReport> report;
};

// Applies a campaign mission's outcome once (idempotent by run id); unchanged when the run is not the
// active campaign run's current mission.
Progress settle_campaign_run(const WebContent& c, Progress p, const CampaignLink& link, const RunOutcome& out);
bool below_campaign_minimum(const WebContent& c, const std::string& equity);
// Best ending capital among completed campaign runs, or "".
std::string best_completed_capital(const std::vector<CampaignRun>& history);

// legReport: the trades and costs of one mission's engine debrief (tl_debrief + tl_state totals).
LegReport leg_report(const json::Value& debrief, const json::Value& totals);

struct RegimeResult {
  std::string regime;
  int trades = 0;
  int wins = 0;
  double net = 0;
};

struct CampaignReport {
  int missions = 0;
  int trades = 0;
  int wins = 0;
  std::optional<double> win_rate;
  std::optional<double> avg_win_r;
  std::optional<double> avg_loss_r;
  std::optional<double> expectancy_r;
  int without_r = 0;
  double costs = 0;
  double gross = 0;
  std::optional<double> cost_share;  // none when the gross P&L is not positive
  std::vector<RegimeResult> by_regime;
  bool too_few = true;
};

CampaignReport campaign_report(const WebContent& c, const std::vector<CampaignLeg>& legs);

}  // namespace tradelab::desktop
