// Port of apps/web/src/lib/game/review.ts (names kept; web parity fixture review.json): the light
// spaced review of Academy lessons (add-academy-and-strategies design D9). Completing a lesson
// schedules a review of its decision and transfer questions 1 day later (box 1); a review answered all
// right on the first try moves it up a box (3, 7, then 21 days) and retires it after the last; any
// wrong first answer sends it back to box 1. Days are the device's local calendar days ("YYYY-MM-DD").
// The schedule itself is progress data (model/progress.hpp LessonReviews).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "model/progress.hpp"

namespace tradelab::desktop::academy {

inline constexpr std::array<int, 4> kReviewDays = {1, 3, 7, 21};
inline constexpr int kRetired = static_cast<int>(kReviewDays.size()) + 1;
static_assert(kRetired == kReviewRetired);

// Local calendar day of a time in ms since the epoch.
std::string local_day(std::int64_t ms);
// `day` plus `n` calendar days.
std::string add_days(const std::string& day, int n);

LessonReviews schedule_on_complete(LessonReviews reviews, const std::string& id, const std::string& today);
LessonReviews apply_review(LessonReviews reviews, const std::string& id, bool all_right, const std::string& today);
bool is_retired(const LessonReviews& reviews, const std::string& id);
// Lesson ids whose review is due on `today` or earlier, in `order`.
std::vector<std::string> due_reviews(const LessonReviews& reviews, const std::string& today, const std::vector<std::string>& order);

}  // namespace tradelab::desktop::academy
