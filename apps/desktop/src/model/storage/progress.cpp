#include "model/storage/progress.hpp"

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
  return p;
}

std::string progress_json(const Progress& p) {
  JsonWriter w;
  w.begin_object().field("version", 3);
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
  return w.end_object().text();
}

}  // namespace tradelab::desktop::storage
