// Port of apps/web/src/lib/game/daily.ts (names kept; web parity fixture daily.json): the daily
// challenge (add-practice-and-advanced-strategies D16). The same mission and seed for everyone on the
// same local date, picked without a server; the history keeps the first finished attempt of each day
// (the record) and the best beside it, ranked by the mission score, never by P&L. No streak, no
// leaderboard.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "model/progress.hpp"

namespace tradelab::desktop::practice {

// Day 0 of the pool.
inline constexpr std::string_view kDailyEpoch = "2026-01-01";

// Version 1 of the pool: every built-in mission except the capstone (m12) and the round markets. Fixed
// and versioned: a new list is DAILY_POOL_V2 with its own seed prefix.
inline constexpr std::array<std::string_view, 24> kDailyPoolV1 = {
    "m01-false-breakout", "m02-funding-squeeze", "m03-volatility-shock", "m04-options-buyer", "m05-options-writer",
    "m06-thin-book",      "m07-trend-trailing",  "m08-regime-switch",    "m09-event-straddle", "m10-hedged-book",
    "m11-news-calendar",  "m13-call-spread",     "m14-covered-call",     "m15-protective-put", "m16-range-condor",
    "m17-before-expiry",  "m18-gamma-scalp",     "m19-funding-carry",    "m20-event-vol",      "m21-calendar",
    "m22-scale-manage",   "m23-second-chance",   "m24-fast-market",      "m25-the-dip"};

// Links a run to the daily challenge it attempts (storage.ts DailyLink).
struct DailyLink {
  std::string date;
  std::string mission;
  std::uint32_t seed = 0;
  bool operator==(const DailyLink&) const = default;
};

// FNV-1a 32 of "tradelab-daily-v1:" + date (0 becomes 1).
std::uint32_t daily_seed(std::string_view date);
// The challenge of a local date ("YYYY-MM-DD"): the pool's mission at dayIndex mod n, and the seed.
DailyLink daily_challenge(const std::string& date);
// A finished attempt: the first one is the day's record and stays; the best is replaced only by a
// higher score. Days beyond the last kDailyKeepDays are dropped. The same run twice changes nothing.
Progress record_daily_attempt(Progress p, const DailyLink& link, const DailyAttempt& attempt);

struct DailyRow {
  std::string date;
  DailyChallenge day;
};
// The history rows, newest day first.
std::vector<DailyRow> daily_history(const Progress& p);

}  // namespace tradelab::desktop::practice
