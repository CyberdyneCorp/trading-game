#include "viewmodel/practice/daily.hpp"

#include <iterator>

#include "viewmodel/practice/prng.hpp"

namespace tradelab::desktop::practice {

namespace {

// A higher mission score; a scored attempt beats an unscored one.
bool better(const DailyAttempt& a, const std::optional<DailyAttempt>& b) {
  return !b || a.score.value_or(-1) > b->score.value_or(-1);
}

bool same_run(const std::optional<DailyAttempt>& a, const DailyAttempt& b) { return a && a->run_id == b.run_id; }

}  // namespace

std::uint32_t daily_seed(std::string_view date) {
  const std::uint32_t h = fnv1a32("tradelab-daily-v1:" + std::string(date));
  return h == 0 ? 1 : h;
}

DailyLink daily_challenge(const std::string& date) {
  const std::int64_t day = days_between(kDailyEpoch, date).value_or(0);
  const auto n = static_cast<std::int64_t>(kDailyPoolV1.size());
  const auto index = static_cast<std::size_t>(((day % n) + n) % n);
  return {date, std::string(kDailyPoolV1[index]), daily_seed(date)};
}

Progress record_daily_attempt(Progress p, const DailyLink& link, const DailyAttempt& attempt) {
  DailyChallenge day{link.mission, link.seed, std::nullopt, std::nullopt, 0};
  if (const auto it = p.daily_challenges.find(link.date); it != p.daily_challenges.end()) day = it->second;
  if (same_run(day.first, attempt) || same_run(day.best, attempt)) return p;
  if (!day.first) day.first = attempt;
  if (better(attempt, day.best)) day.best = attempt;
  ++day.attempts;
  p.daily_challenges[link.date] = std::move(day);
  while (p.daily_challenges.size() > kDailyKeepDays) p.daily_challenges.erase(p.daily_challenges.begin());
  return p;
}

std::vector<DailyRow> daily_history(const Progress& p) {
  std::vector<DailyRow> out;
  for (auto it = p.daily_challenges.rbegin(); it != p.daily_challenges.rend(); ++it) out.push_back({it->first, it->second});
  return out;
}

}  // namespace tradelab::desktop::practice
