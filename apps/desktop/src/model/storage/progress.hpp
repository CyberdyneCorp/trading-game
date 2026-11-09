// Per-device player progress, version 4 (port of `Progress` / `migrateProgress` in
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
// storage.ts DRILL_HISTORY_KEEP, DAILY_KEEP_DAYS, GUESS_HISTORY_KEEP (progress v4).
constexpr std::size_t kDrillHistoryKeep = 30;
constexpr std::size_t kDailyKeepDays = 90;
constexpr std::size_t kGuessHistoryKeep = 200;

struct Progress {
  int version = 4;
  std::vector<std::string> tutorials_seen;
  std::int64_t advisor_points = kStartPoints;
  std::vector<std::string> settled_runs;
  std::vector<std::string> perks_introduced;
  std::vector<std::string> lessons_done;
  std::optional<std::string> campaign;        // CampaignRun JSON, or none
  std::vector<std::string> campaign_history;  // ended CampaignRun JSON objects, oldest first
  std::vector<std::string> assessed;
  // Version 3 (add-academy-and-strategies): the spaced-review schedule as the web's JSON object
  // ({"<lesson>": {"box", "due"}}), the guided tours done and the glossary terms opened.
  std::string lesson_reviews = "{}";
  std::vector<std::string> coach_done;
  std::vector<std::string> glossary_seen;
  // Version 4 (add-practice-and-advanced-strategies D20): the practice formats' keys as the web's JSON
  // (drill skills in the spaced review, the last drill sessions, daily challenges by local date and the
  // last guess rounds), read defensively; campaign runs snapshot their `order` inside their own JSON.
  std::string drill_reviews = "{}";
  std::string drill_history = "[]";
  std::string daily_challenges = "{}";
  std::string guess_history = "[]";

  bool operator==(const Progress&) const = default;
};

// Reads any stored progress shape: version 1 gets the version 2 fields empty, version 2 the version 3,
// version 3 the version 4 ones; unknown or damaged
// fields fall back to their defaults instead of failing.
Progress migrate_progress(const json::Value& row);
std::string progress_json(const Progress& p);

}  // namespace tradelab::desktop::storage
