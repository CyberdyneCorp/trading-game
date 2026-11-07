#include "viewmodel/academy/review.hpp"

#include <cstdio>
#include <ctime>

namespace tradelab::desktop::academy {

namespace {

std::string day_of(const std::tm& t) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return buf;
}

ReviewEntry entry(int box, const std::string& today) {
  return {box, add_days(today, kReviewDays[static_cast<std::size_t>(box - 1)])};
}

}  // namespace

std::string local_day(std::int64_t ms) {
  const std::time_t secs = static_cast<std::time_t>(ms / 1000);
  std::tm t{};
  localtime_r(&secs, &t);
  return day_of(t);
}

std::string add_days(const std::string& day, int n) {
  int y = 0;
  int m = 0;
  int d = 0;
  if (std::sscanf(day.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return day;
  // Noon: a daylight-saving change never moves the result to another day.
  std::tm t{};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d + n;
  t.tm_hour = 12;
  t.tm_isdst = -1;
  std::mktime(&t);
  return day_of(t);
}

LessonReviews schedule_on_complete(LessonReviews reviews, const std::string& id, const std::string& today) {
  if (!reviews.contains(id)) reviews.emplace(id, entry(1, today));
  return reviews;
}

LessonReviews apply_review(LessonReviews reviews, const std::string& id, bool all_right, const std::string& today) {
  if (!all_right) {
    reviews[id] = entry(1, today);
    return reviews;
  }
  const auto it = reviews.find(id);
  const int next = (it == reviews.end() ? 1 : it->second.box) + 1;
  reviews[id] = next >= kRetired ? ReviewEntry{kRetired, today} : entry(next, today);
  return reviews;
}

bool is_retired(const LessonReviews& reviews, const std::string& id) {
  const auto it = reviews.find(id);
  return it != reviews.end() && it->second.box >= kRetired;
}

std::vector<std::string> due_reviews(const LessonReviews& reviews, const std::string& today, const std::vector<std::string>& order) {
  std::vector<std::string> out;
  for (const auto& id : order) {
    const auto it = reviews.find(id);
    if (it != reviews.end() && it->second.box < kRetired && it->second.due <= today) out.push_back(id);
  }
  return out;
}

}  // namespace tradelab::desktop::academy
