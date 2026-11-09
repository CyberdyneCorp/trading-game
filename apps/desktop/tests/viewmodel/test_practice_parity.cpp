// Web parity of the practice formats (add-practice-and-advanced-strategies W3-D2, W3-D3): every case of
// tests/fixtures/web/drills.json, daily.json, guess.json and coach.json (scripts/web_fixtures_wave3.mjs,
// from the web's prng.ts, drills.ts, daily.ts, guessRound.ts and coachReview.ts) replayed through the
// C++ ports. Numbers with a 1e-9 relative tolerance, texts and integers exactly.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "model/content.hpp"
#include "model/engine_run.hpp"
#include "model/i18n.hpp"
#include "support/web_fixture.hpp"
#include "viewmodel/practice/coach_review.hpp"
#include "viewmodel/practice/daily.hpp"
#include "viewmodel/practice/drills.hpp"
#include "viewmodel/practice/guess_round.hpp"
#include "viewmodel/practice/prng.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::practice;
using namespace tradelab::desktop::test_support;

namespace {

Catalog catalog_in(Locale l) {
  Catalog c = builtin_catalog();
  c.set_locale(l);
  return c;
}

// ---- drills.ts ----

SessionInput session_input(const Value& v) {
  SessionInput in;
  in.date = v["date"].as_string();
  in.now = v["now"].as_int();
  in.session = static_cast<int>(v["session"].as_int());
  for (const auto& [id, e] : v["lessonReviews"].fields()) in.lesson_reviews[id] = {static_cast<int>(e["box"].as_int()), e["due"].as_string()};
  for (const auto& [skill, e] : v["drillReviews"].fields()) in.drill_reviews[skill] = {static_cast<int>(e["box"].as_int()), e["due"].as_int()};
  for (const Value& s : v["history"].items()) {
    SkillMap skills;
    for (const auto& [k, x] : s["skills"].fields()) skills[k] = {static_cast<int>(x["n"].as_int()), static_cast<int>(x["right"].as_int())};
    in.history.push_back({s["date"].as_string(), static_cast<int>(s["items"].as_int()), static_cast<int>(s["firstTry"].as_int()), skills});
  }
  return in;
}

void check_item(const DrillItem& got, const Value& want) {
  CHECK(got.template_id == want["template"].as_string());
  CHECK(got.skill == want["skill"].as_string());
  check_strings(got.lessons, want["lessons"]);
  REQUIRE(got.params.size() == want["params"].fields().size());
  for (const auto& [k, v] : got.params) CHECK_MESSAGE(v == want["params"][k].as_string(), k);
  check_number(got.answer, want["answer"]);
  CHECK(got.digits == want["digits"].as_int());
  check_number(got.tolerance, want["tolerance"]);
  CHECK(got.choice == want["choice"].as_bool());
}

void check_texts(const DrillItem& item, const Value& want) {
  const DrillTemplate* t = drill_template(web_content(), item.template_id);
  REQUIRE(t != nullptr);
  for (const Locale l : {Locale::PtBR, Locale::En}) {
    const Value& w = want[l == Locale::En ? "en" : "pt-BR"];
    const DrillText text = item_text(item, *t, catalog_in(l));
    CHECK(text.prompt == w["prompt"].as_string());
    CHECK(text.explain == w["explain"].as_string());
    check_strings(text.choices, w["choices"]);
    CHECK(text.answer == w["answer"].as_string());
  }
}

std::map<std::string, Case> drill_ports() {
  return {
      {"fnv1a32", [](const Value& a, const Value& out) { CHECK(fnv1a32(a[0].as_string()) == static_cast<std::uint32_t>(out.as_int())); }},
      {"mulberry32",
       [](const Value& a, const Value& out) {
         Mulberry32 next(static_cast<std::uint32_t>(a[0].as_int()));
         for (const Value& v : out.items()) CHECK(next() == static_cast<std::uint32_t>(v.as_int()));
       }},
      {"buildSession",
       [](const Value& a, const Value& out) {
         const auto items = build_session(web_content(), session_input(a[0]));
         REQUIRE(items.size() == out["items"].size());
         for (std::size_t i = 0; i < items.size(); ++i) {
           check_item(items[i], out["items"][i]);
           check_texts(items[i], out["texts"][i]);
         }
       }},
      {"makeItem",
       [](const Value& a, const Value& out) {
         const DrillTemplate* t = drill_template(web_content(), a[0].as_string());
         REQUIRE(t != nullptr);
         Mulberry32 next(static_cast<std::uint32_t>(a[1].as_int()));
         check_item(make_item(*t, next), out);
       }},
      {"parseAnswer", [](const Value& a, const Value& out) { check_number(parse_answer(a[0].as_string()), out); }},
  };
}

// ---- daily.ts ----

std::map<std::string, Case> daily_ports() {
  return {
      {"dailyChallenge",
       [](const Value& a, const Value& out) {
         const DailyLink d = daily_challenge(a[0].as_string());
         CHECK(d.date == out["date"].as_string());
         CHECK(d.mission == out["mission"].as_string());
         CHECK(d.seed == static_cast<std::uint32_t>(out["seed"].as_int()));
       }},
      {"pool",
       [](const Value&, const Value& out) {
         REQUIRE(out.size() == kDailyPoolV1.size());
         for (std::size_t i = 0; i < kDailyPoolV1.size(); ++i) CHECK(out[i].as_string() == kDailyPoolV1[i]);
       }},
  };
}

// ---- guessRound.ts ----

// The markets web_fixtures_wave3.mjs passes (the engine's round markets: checked below).
const std::vector<MissionInfo> kMarkets = {{"r01-trend", 160, 60, 0, true, "round"},
                                           {"r02-range", 160, 60, 0, true, "round"},
                                           {"r03-breakout", 150, 60, 0, true, "round"},
                                           {"r04-event", 160, 60, 0, true, "round"}};

Commitment commitment(const Value& v) {
  return {v["decision"].as_string(), static_cast<int>(v["p"].as_int()), v["invalidation"].as_string(), v["target"].as_string()};
}

void check_flag(const std::optional<bool>& got, const Value& want) {
  if (want.is_null()) {
    CHECK_FALSE(got.has_value());
  } else {
    REQUIRE(got.has_value());
    CHECK(*got == want.as_bool());
  }
}

std::map<std::string, Case> guess_ports() {
  return {
      {"pickRound",
       [](const Value& a, const Value& out) {
         const RoundPick p = pick_round(kMarkets, a[0].as_string(), static_cast<int>(a[1].as_int()));
         CHECK(p.market == out["market"].as_string());
         CHECK(p.seed == static_cast<std::uint32_t>(out["seed"].as_int()));
         CHECK(p.step == out["step"].as_int());
       }},
      {"scoreRound",
       [](const Value& a, const Value& out) {
         const GuessScore s = score_round(commitment(a[0]), {a[1]["close"].as_string(), a[1]["atr"].as_string(), a[1]["higher"].as_bool()});
         CHECK(s.calibration == out["calibration"].as_int());
         CHECK(s.plan == out["plan"].as_int());
         CHECK(s.consistency == out["consistency"].as_int());
         CHECK(s.total == out["total"].as_int());
         check_flag(s.side_ok, out["sideOk"]);
         check_flag(s.distance_ok, out["distanceOk"]);
         check_flag(s.reward_ok, out["rewardOk"]);
       }},
      {"firstHit",
       [](const Value& a, const Value& out) {
         std::vector<HighLow> bars;
         for (const Value& b : a[1].items()) bars.push_back({b["high"].as_string(), b["low"].as_string()});
         CHECK(first_hit(commitment(a[0]), bars) == out.as_string());
       }},
      {"calibrationTable",
       [](const Value& a, const Value& out) {
         std::vector<StatedOutcome> rounds;
         for (const Value& r : a[0].items()) rounds.push_back({static_cast<int>(r["p"].as_int()), r["higher"].as_bool()});
         const auto rows = calibration_table(rounds);
         REQUIRE(rows.size() == out.size());
         for (std::size_t i = 0; i < rows.size(); ++i) {
           CHECK(rows[i].from == out[i]["from"].as_int());
           CHECK(rows[i].rounds == out[i]["rounds"].as_int());
           check_number(rows[i].stated, out[i]["stated"]);
           check_number(rows[i].observed, out[i]["observed"]);
         }
       }},
  };
}

// ---- coachReview.ts ----

std::vector<FindingRef> finding_refs(const Value& list) {
  std::vector<FindingRef> out;
  for (const Value& f : list.items()) out.push_back({f["code"].as_string(), static_cast<int>(f["step"].as_int())});
  return out;
}

std::map<std::string, Case> coach_ports() {
  return {
      {"momentText",
       [](const Value& a, const Value& out) {
         const CoachMoment m = coach_moment_from(a[0]);
         CHECK(moment_key(m) == out["key"].as_string());
         CHECK(moment_text(m, catalog_in(locale(a[1]))) == out["text"].as_string());
       }},
      {"mistakeOutcome",
       [](const Value& a, const Value& out) {
         std::vector<CoachMoment> moments;
         for (const Value& m : a[2]["moments"].items()) moments.push_back(coach_moment_from(m));
         CHECK(mistake_outcome(a[0].as_string(), static_cast<int>(a[1].as_int()), moments, finding_refs(a[2]["findings"])) == out.as_string());
       }},
      {"mistakeName", [](const Value& a, const Value& out) { CHECK(mistake_name(a[0].as_string()) == out.as_string()); }},
  };
}

}  // namespace

TEST_CASE("web parity: drills.ts") { replay("drills", drill_ports()); }
TEST_CASE("web parity: daily.ts") { replay("daily", daily_ports()); }
TEST_CASE("web parity: guessRound.ts") { replay("guess", guess_ports()); }
TEST_CASE("web parity: coachReview.ts") { replay("coach", coach_ports()); }

// The fixture's round markets are the engine's, and the daily pool is every built-in mission but the
// capstone (design D16: a unit test against the engine's catalog).
TEST_CASE("practice: the round markets and the daily pool match the engine's catalog") {
  const auto all = engine_missions();
  const auto rounds = round_markets(all);
  REQUIRE(rounds.size() == kMarkets.size());
  for (std::size_t i = 0; i < rounds.size(); ++i) {
    CHECK(rounds[i].id == kMarkets[i].id);
    CHECK(rounds[i].total_bars == kMarkets[i].total_bars);
    CHECK(rounds[i].warmup_bars == kMarkets[i].warmup_bars);
  }
  std::vector<std::string> pool;
  for (const auto& m : missions_only(all)) {
    if (m.id != "m12-capstone") pool.push_back(m.id);
  }
  REQUIRE(pool.size() == kDailyPoolV1.size());
  for (std::size_t i = 0; i < pool.size(); ++i) CHECK(pool[i] == kDailyPoolV1[i]);
}

// Every template of the content has its texts, and every one of its numbers comes from the lesson checks.
TEST_CASE("practice: every drill template has texts in both locales and a finite answer") {
  const WebContent& content = web_content();
  REQUIRE(content.drill_skills.size() == 11);
  REQUIRE(content.drills.size() >= content.drill_skills.size());
  const Catalog& c = builtin_catalog();
  for (const DrillTemplate& t : content.drills) {
    INFO(t.id);
    CHECK(c.has("drill." + t.id + ".prompt"));
    CHECK(c.has("drill." + t.id + ".explain"));
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
      Mulberry32 next(seed);
      CHECK(std::isfinite(make_item(t, next).answer));
    }
  }
}
