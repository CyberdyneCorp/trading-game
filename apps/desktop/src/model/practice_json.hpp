// Progress v4 practice keys (model/progress.hpp) as the JSON the web stores (storage.ts readDrillReviews,
// readDrillHistory, readDaily, readGuesses): damaged entries are dropped, the histories keep their newest
// entries, and the wave 3 fields (a session's `skills`, a round's `higher`) survive a round trip.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "model/json.hpp"
#include "model/progress.hpp"

namespace tradelab::desktop::practice_json {

using DrillReviews = std::map<std::string, DrillReview, std::less<>>;

std::string drill_reviews(const DrillReviews& r);
DrillReviews drill_reviews_from(const json::Value& v);

std::string drill_history(const std::vector<DrillSession>& h);
std::vector<DrillSession> drill_history_from(const json::Value& v);

std::string daily_challenges(const DailyChallenges& d);
DailyChallenges daily_challenges_from(const json::Value& v);

std::string guess_history(const std::vector<GuessRound>& h);
std::vector<GuessRound> guess_history_from(const json::Value& v);

}  // namespace tradelab::desktop::practice_json
