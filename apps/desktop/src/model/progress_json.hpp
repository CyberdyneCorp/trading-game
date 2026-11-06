// The shell's progress and settings types (model/progress.hpp) as the JSON the web stores: CampaignRun
// (campaign.ts), CampaignLink and AssistedLink (storage.ts RunRecord), PlanEntry[] (analysis.ts, the
// runs.journal column), the display settings and the shortcut map. Readers never fail: a missing or
// damaged field falls back to its default, an unreadable object to nullopt.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "model/json.hpp"
#include "model/progress.hpp"

namespace tradelab::desktop::progress_json {

std::string campaign(const CampaignRun& c);
std::optional<CampaignRun> campaign_from(const json::Value& v);

std::string link(const CampaignLink& l);
std::optional<CampaignLink> link_from(const json::Value& v);

std::string assisted(const AssistedLink& a);
std::optional<AssistedLink> assisted_from(const json::Value& v);

std::string journal(const std::vector<PlanEntry>& entries);
std::vector<PlanEntry> journal_from(const json::Value& v);

std::string settings(const Settings& s);  // without the key map (its own row)
Settings settings_from(const json::Value& v);

std::string keymap(const std::map<std::string, std::string>& map);
std::map<std::string, std::string> keymap_from(const json::Value& v);

}  // namespace tradelab::desktop::progress_json
