#include "model/practice_json.hpp"

#include <algorithm>
#include <cmath>

#include "model/storage/json_writer.hpp"

namespace tradelab::desktop::practice_json {

namespace {

using storage::JsonWriter;
using Kind = json::Value::Kind;

// storage.ts num(): a finite number.
bool num(const json::Value& v) { return v.kind() == Kind::Number && std::isfinite(v.as_double()); }
bool str(const json::Value& v) { return v.kind() == Kind::String; }
int int_of(const json::Value& v) { return static_cast<int>(v.as_int()); }

bool is_day(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
  }
  return true;
}

// The newest `keep` of a list (oldest first).
template <class T>
std::vector<T> newest(std::vector<T> list, std::size_t keep) {
  if (list.size() > keep) list.erase(list.begin(), list.end() - static_cast<std::ptrdiff_t>(keep));
  return list;
}

std::optional<SkillMap> skills_from(const json::Value& v) {
  if (v.kind() != Kind::Object) return std::nullopt;
  SkillMap out;
  for (const auto& [k, x] : v.fields()) {
    if (num(x["n"]) && num(x["right"])) out[k] = {int_of(x["n"]), int_of(x["right"])};
  }
  return out;
}

std::optional<DailyAttempt> attempt_from(const json::Value& v) {
  if (!str(v["pnl"]) || !str(v["runId"])) return std::nullopt;
  DailyAttempt a{std::nullopt, v["pnl"].as_string(), v["runId"].as_string()};
  if (num(v["score"])) a.score = int_of(v["score"]);
  return a;
}

void write_attempt(JsonWriter& w, std::string_view key, const std::optional<DailyAttempt>& a) {
  w.key(key);
  if (!a) {
    w.null();
    return;
  }
  w.begin_object().key("score");
  if (a->score) w.integer(*a->score); else w.null();
  w.field("pnl", a->pnl).field("runId", a->run_id).end_object();
}

bool is_decision(const std::string& d) { return d == "long" || d == "short" || d == "none"; }

}  // namespace

std::string drill_reviews(const DrillReviews& r) {
  JsonWriter w;
  w.begin_object();
  for (const auto& [skill, e] : r) w.key(skill).begin_object().field("box", e.box).field("due", e.due).end_object();
  return w.end_object().text();
}

DrillReviews drill_reviews_from(const json::Value& v) {
  DrillReviews out;
  for (const auto& [skill, e] : v.fields()) {
    if (num(e["box"]) && num(e["due"])) out[skill] = {int_of(e["box"]), e["due"].as_int()};
  }
  return out;
}

std::string drill_history(const std::vector<DrillSession>& h) {
  JsonWriter w;
  w.begin_array();
  for (const DrillSession& s : h) {
    w.begin_object().field("date", s.date).field("items", s.items).field("firstTry", s.first_try);
    if (s.skills) {
      w.key("skills").begin_object();
      for (const auto& [k, x] : *s.skills) w.key(k).begin_object().field("n", x.n).field("right", x.right).end_object();
      w.end_object();
    }
    w.end_object();
  }
  return w.end_array().text();
}

std::vector<DrillSession> drill_history_from(const json::Value& v) {
  std::vector<DrillSession> out;
  for (const json::Value& x : v.items()) {
    if (!str(x["date"]) || !num(x["items"]) || !num(x["firstTry"])) continue;
    out.push_back({x["date"].as_string(), int_of(x["items"]), int_of(x["firstTry"]), skills_from(x["skills"])});
  }
  return newest(std::move(out), kDrillHistoryKeep);
}

std::string daily_challenges(const DailyChallenges& d) {
  JsonWriter w;
  w.begin_object();
  for (const auto& [date, c] : d) {
    w.key(date).begin_object().field("mission", c.mission).field("seed", c.seed);
    write_attempt(w, "first", c.first);
    write_attempt(w, "best", c.best);
    w.field("attempts", c.attempts).end_object();
  }
  return w.end_object().text();
}

DailyChallenges daily_challenges_from(const json::Value& v) {
  std::vector<std::string> days;
  for (const auto& [day, x] : v.fields()) {
    if (is_day(day)) days.push_back(day);
  }
  std::ranges::sort(days);
  days = newest(std::move(days), kDailyKeepDays);
  DailyChallenges out;
  for (const std::string& day : days) {
    const json::Value& x = v[day];
    if (!str(x["mission"]) || !num(x["seed"])) continue;
    out[day] = {x["mission"].as_string(), static_cast<std::uint32_t>(x["seed"].as_int()), attempt_from(x["first"]),
                attempt_from(x["best"]), num(x["attempts"]) ? int_of(x["attempts"]) : 0};
  }
  return out;
}

std::string guess_history(const std::vector<GuessRound>& h) {
  JsonWriter w;
  w.begin_array();
  for (const GuessRound& r : h) {
    w.begin_object().field("market", r.market).field("seed", r.seed).field("step", r.step).field("decision", r.decision);
    w.field("p", r.p).field("score", r.score);
    if (r.higher) w.field("higher", *r.higher);
    w.end_object();
  }
  return w.end_array().text();
}

std::vector<GuessRound> guess_history_from(const json::Value& v) {
  std::vector<GuessRound> out;
  for (const json::Value& x : v.items()) {
    const std::string d = x["decision"].as_string();
    if (!str(x["market"]) || !num(x["seed"]) || !num(x["step"]) || !is_decision(d) || !num(x["p"]) || !num(x["score"])) continue;
    GuessRound r{x["market"].as_string(), static_cast<std::uint32_t>(x["seed"].as_int()), int_of(x["step"]), d,
                 int_of(x["p"]), int_of(x["score"]), std::nullopt};
    if (x["higher"].kind() == Kind::Bool) r.higher = x["higher"].as_bool();
    out.push_back(std::move(r));
  }
  return newest(std::move(out), kGuessHistoryKeep);
}

}  // namespace tradelab::desktop::practice_json
