// Per-device player progress, version 2 (port of `Progress` / `migrateProgress` in
// apps/web/src/lib/game/storage.ts). The campaign run and its history are kept as the web's JSON objects
// (CampaignRun) so the campaign logic ported later reads exactly what the web stores.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/json.hpp"

namespace tradelab::desktop::storage {

constexpr std::int64_t kStartPoints = 3;  // points.ts START_POINTS

struct Progress {
  int version = 2;
  std::vector<std::string> tutorials_seen;
  std::int64_t advisor_points = kStartPoints;
  std::vector<std::string> settled_runs;
  std::vector<std::string> perks_introduced;
  std::vector<std::string> lessons_done;
  std::optional<std::string> campaign;        // CampaignRun JSON, or none
  std::vector<std::string> campaign_history;  // ended CampaignRun JSON objects, oldest first
  std::vector<std::string> assessed;

  bool operator==(const Progress&) const = default;
};

// Reads any stored progress shape: version 1 gets the version 2 fields empty; unknown or damaged
// fields fall back to their defaults instead of failing.
Progress migrate_progress(const json::Value& row);
std::string progress_json(const Progress& p);

}  // namespace tradelab::desktop::storage
