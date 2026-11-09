#include "model/storage/progress.hpp"

#include <algorithm>
#include <array>

#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::storage {

namespace {

std::vector<std::string> strings(const json::Value& v) {
  std::vector<std::string> out;
  for (const json::Value& item : v.items()) {
    if (item.kind() == json::Value::Kind::String) out.push_back(item.as_string());
  }
  return out;
}

// campaign.ts isCampaign: an object with a string id and a legs array.
bool is_campaign(const json::Value& v) {
  return v["id"].kind() == json::Value::Kind::String && v["legs"].kind() == json::Value::Kind::Array;
}

void write_strings(JsonWriter& w, std::string_view key, const std::vector<std::string>& list) {
  w.key(key).begin_array();
  for (const std::string& s : list) w.string(s);
  w.end_array();
}

// ---- Progress v4 practice keys (storage.ts readDrillReviews, readDrillHistory, readDaily, readGuesses):
// damaged entries are dropped and the histories keep their newest entries, as the web reads them.

using Kind = json::Value::Kind;

bool num(const json::Value& v) { return v.kind() == Kind::Number; }
bool str(const json::Value& v) { return v.kind() == Kind::String; }

std::string drill_reviews(const json::Value& v) {
  JsonWriter w;
  w.begin_object();
  for (const auto& [skill, r] : v.fields()) {
    if (num(r["box"]) && num(r["due"])) w.key(skill).raw(dump(r));
  }
  return w.end_object().text();
}

template <class Ok>
std::string newest(const json::Value& v, std::size_t keep, Ok ok) {
  std::vector<const json::Value*> valid;
  for (const json::Value& x : v.items()) {
    if (x.kind() == Kind::Object && ok(x)) valid.push_back(&x);
  }
  const std::size_t from = valid.size() > keep ? valid.size() - keep : 0;
  JsonWriter w;
  w.begin_array();
  for (std::size_t i = from; i < valid.size(); ++i) w.raw(dump(*valid[i]));
  return w.end_array().text();
}

bool is_day(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  return std::ranges::all_of(std::array{0, 1, 2, 3, 5, 6, 8, 9}, [&s](int i) { return s[static_cast<std::size_t>(i)] >= '0' && s[static_cast<std::size_t>(i)] <= '9'; });
}

std::string daily_challenges(const json::Value& v) {
  std::vector<std::string> days;
  for (const auto& [day, x] : v.fields()) {
    if (is_day(day) && str(x["mission"]) && num(x["seed"])) days.push_back(day);
  }
  std::ranges::sort(days);
  const std::size_t from = days.size() > kDailyKeepDays ? days.size() - kDailyKeepDays : 0;
  JsonWriter w;
  w.begin_object();
  for (std::size_t i = from; i < days.size(); ++i) w.key(days[i]).raw(dump(v[days[i]]));
  return w.end_object().text();
}

bool drill_session(const json::Value& x) { return str(x["date"]) && num(x["items"]) && num(x["firstTry"]); }

bool guess_round(const json::Value& x) {
  const std::string d = x["decision"].as_string();
  return str(x["market"]) && num(x["seed"]) && num(x["step"]) && (d == "long" || d == "short" || d == "none") && num(x["p"]) &&
         num(x["score"]);
}

}  // namespace

Progress migrate_progress(const json::Value& row) {
  Progress p;
  p.tutorials_seen = strings(row["tutorialsSeen"]);
  if (row["advisorPoints"].kind() == json::Value::Kind::Number) p.advisor_points = row["advisorPoints"].as_int(kStartPoints);
  p.settled_runs = strings(row["settledRuns"]);
  p.perks_introduced = strings(row["perksIntroduced"]);
  p.lessons_done = strings(row["lessonsDone"]);
  if (is_campaign(row["campaign"])) p.campaign = dump(row["campaign"]);
  for (const json::Value& c : row["campaignHistory"].items()) {
    if (is_campaign(c)) p.campaign_history.push_back(dump(c));
  }
  p.assessed = strings(row["assessed"]);
  if (row["lessonReviews"].kind() == json::Value::Kind::Object) p.lesson_reviews = dump(row["lessonReviews"]);
  p.coach_done = strings(row["coachDone"]);
  p.glossary_seen = strings(row["glossarySeen"]);
  p.drill_reviews = drill_reviews(row["drillReviews"]);
  p.drill_history = newest(row["drillHistory"], kDrillHistoryKeep, drill_session);
  p.daily_challenges = daily_challenges(row["dailyChallenges"]);
  p.guess_history = newest(row["guessHistory"], kGuessHistoryKeep, guess_round);
  return p;
}

std::string progress_json(const Progress& p) {
  JsonWriter w;
  w.begin_object().field("version", 4);
  write_strings(w, "tutorialsSeen", p.tutorials_seen);
  w.field("advisorPoints", p.advisor_points);
  write_strings(w, "settledRuns", p.settled_runs);
  write_strings(w, "perksIntroduced", p.perks_introduced);
  write_strings(w, "lessonsDone", p.lessons_done);
  w.key("campaign");
  if (p.campaign) w.raw(*p.campaign); else w.null();
  w.key("campaignHistory").begin_array();
  for (const std::string& c : p.campaign_history) w.raw(c);
  w.end_array();
  write_strings(w, "assessed", p.assessed);
  w.key("lessonReviews").raw(p.lesson_reviews.empty() ? "{}" : p.lesson_reviews);
  write_strings(w, "coachDone", p.coach_done);
  write_strings(w, "glossarySeen", p.glossary_seen);
  const auto or_empty = [](const std::string& raw, std::string_view empty) { return raw.empty() ? empty : std::string_view(raw); };
  w.key("drillReviews").raw(or_empty(p.drill_reviews, "{}"));
  w.key("drillHistory").raw(or_empty(p.drill_history, "[]"));
  w.key("dailyChallenges").raw(or_empty(p.daily_challenges, "{}"));
  w.key("guessHistory").raw(or_empty(p.guess_history, "[]"));
  return w.end_object().text();
}

}  // namespace tradelab::desktop::storage
