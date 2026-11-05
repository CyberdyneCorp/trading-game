// Intel perks in the state view (spec progression-rewards "Fast wire", "Economic calendar";
// design D4): early headlines and the calendar never change the market or reveal a later bar.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "tradelab/c_api.h"
#include "tradelab/generator.hpp"

using namespace tradelab;

namespace {

bool has(const std::string& json, const std::string& needle) { return json.find(needle) != std::string::npos; }

std::string between(const std::string& s, const std::string& from, const std::string& to) {
  const auto a = s.find(from);
  const auto b = s.find(to, a);
  REQUIRE(a != std::string::npos);
  REQUIRE(b != std::string::npos);
  return s.substr(a, b - a);
}

std::uint32_t state_step(const std::string& s) {
  const auto at = s.find("\"step\":") + 7;
  return static_cast<std::uint32_t>(std::stoul(s.substr(at)));
}

// Advances `run` until its decision step is `step` and returns that state.
std::string state_at(tl_run* run, std::uint32_t step) {
  const std::string now = tl_state(run);
  REQUIRE(state_step(now) <= step);
  return step == state_step(now) ? now : std::string(tl_advance(run, static_cast<std::int32_t>(step - state_step(now))));
}

std::uint32_t release_of(const char* mission, std::uint32_t seed, NewsKind kind) {
  Scenario sc;
  REQUIRE(build_mission(mission, seed, sc));
  for (const NewsItem& n : sc.news) {
    if (n.kind == kind) return n.release_step;
  }
  FAIL("mission has no item of that kind");
  return 0;
}

}  // namespace

TEST_CASE("Fast wire I: the m01 denial is visible one bar early, flagged early until its release") {
  const std::uint32_t seed = 11;
  const std::uint32_t r = release_of("m01-false-breakout", seed, NewsKind::RumorDenied);
  const std::string item = "\"step\":" + std::to_string(r) + ",\"kind\":\"rumor_denied\"";
  tl_run* wire = tl_run_create_ex("m01-false-breakout", seed, 0, 1, 0, nullptr, 0);
  tl_run* plain = tl_run_create("m01-false-breakout", seed);

  CHECK_FALSE(has(state_at(wire, r - 2), item));
  const std::string early = state_at(wire, r - 1);
  CHECK(has(early, item));
  CHECK(has(between(early, item, "}"), "\"early\":true"));
  CHECK_FALSE(has(state_at(plain, r - 1), item));  // without the perk: not before its release

  const std::string released = state_at(wire, r);
  CHECK(has(between(released, item, "}"), "\"early\":false"));
  CHECK(has(between(state_at(plain, r), item, "}"), "\"early\":false"));
  tl_run_destroy(wire);
  tl_run_destroy(plain);
}

TEST_CASE("Fast wire II: two bars early; the state never shows a bar after the decision step") {
  const std::uint32_t seed = 3;
  const std::uint32_t r = release_of("m03-volatility-shock", seed, NewsKind::ExchangeIncident);
  tl_run* wire = tl_run_create_ex("m03-volatility-shock", seed, 0, 2, 0, nullptr, 0);
  const std::string s = state_at(wire, r - 2);
  CHECK(has(s, "\"step\":" + std::to_string(r) + ",\"kind\":\"exchange_incident\""));
  CHECK(has(s, "\"early\":true"));
  // bars[0..step]: one array per bar, so step + 1 of them.
  const std::string bars = between(s, "\"bars\":", "\"account\"");
  std::size_t count = 0;
  for (std::size_t at = bars.find("[\""); at != std::string::npos; at = bars.find("[\"", at + 1)) ++count;
  CHECK(count == r - 1);
  tl_run_destroy(wire);
}

TEST_CASE("Economic calendar: m02 lists the rate decision's bar only, until the headline is visible") {
  const std::uint32_t seed = 2026;
  const std::uint32_t r = release_of("m02-funding-squeeze", seed, NewsKind::RateDecision);
  tl_run* cal = tl_run_create_ex("m02-funding-squeeze", seed, 0, 0, 1, nullptr, 0);
  tl_run* plain = tl_run_create("m02-funding-squeeze", seed);

  const std::string start = tl_state(cal);
  CHECK(has(start, "\"calendar\":[" + std::to_string(r) + "]"));
  CHECK_FALSE(has(start, "\"rate_decision\""));  // no kind, tone, expectation or value before release
  CHECK(has(tl_state(plain), "\"calendar\":[]"));

  const std::string before = state_at(cal, r - 1);
  CHECK(has(before, "\"calendar\":[" + std::to_string(r) + "]"));
  const std::string released = state_at(cal, r);
  CHECK(has(released, "\"calendar\":[]"));
  CHECK(has(released, "\"kind\":\"rate_decision\""));
  tl_run_destroy(cal);
  tl_run_destroy(plain);

  // With a fast wire the calendar entry hands over to the early headline.
  tl_run* both = tl_run_create_ex("m02-funding-squeeze", seed, 0, 1, 1, nullptr, 0);
  const std::string wired = state_at(both, r - 1);
  CHECK(has(wired, "\"calendar\":[]"));
  CHECK(has(wired, "\"kind\":\"rate_decision\""));
  tl_run_destroy(both);
}

TEST_CASE("Perks change nothing in the market: bars and event digests match the run without them") {
  for (const MissionInfo& info : mission_catalog()) {
    const char* mission = info.id.c_str();
    for (std::uint32_t seed : {1U, 7U, 42U}) {
      CAPTURE(mission);
      CAPTURE(seed);
      tl_run* plain = tl_run_create(mission, seed);
      tl_run* intel = tl_run_create_ex(mission, seed, 0, 2, 1, nullptr, 0);
      std::string a = tl_state(plain);
      std::string b = tl_state(intel);
      for (int chunk = 0; chunk < 40 && has(a, "\"status\":\"active\""); ++chunk) {
        CHECK(between(a, "\"bars\":", "\"account\"") == between(b, "\"bars\":", "\"account\""));
        CHECK(between(a, "\"eventsDigest\"", "}") == between(b, "\"eventsDigest\"", "}"));
        a = tl_advance(plain, 7);
        b = tl_advance(intel, 7);
      }
      CHECK(between(a, "\"bars\":", "\"account\"") == between(b, "\"bars\":", "\"account\""));
      CHECK(between(a, "\"eventsDigest\"", "}") == between(b, "\"eventsDigest\"", "}"));
      tl_run_destroy(plain);
      tl_run_destroy(intel);
    }
  }
}

TEST_CASE("m11 stays fair with intel: a fast wire hands an announced release from upcoming to an early headline") {
  const std::uint32_t seed = 9;
  const std::uint32_t r = release_of("m11-news-calendar", seed, NewsKind::RateDecision);
  const std::string upcoming = "{\"step\":" + std::to_string(r) + ",\"kind\":\"rate_decision\",\"expectation\":0}";
  tl_run* wire = tl_run_create_ex("m11-news-calendar", seed, 0, 2, 1, nullptr, 0);
  tl_run* plain = tl_run_create("m11-news-calendar", seed);
  const std::string before = state_at(wire, r - 3);
  CHECK(has(before, upcoming));
  CHECK(has(state_at(plain, r - 3), upcoming));  // the announcement is public: no perk needed
  const std::string early = state_at(wire, r - 2);
  CHECK_FALSE(has(early, upcoming));
  CHECK(has(between(early, "\"step\":" + std::to_string(r) + ",\"kind\":\"rate_decision\"", "}"), "\"early\":true"));
  const std::string public_view = state_at(plain, r - 2);
  CHECK(has(public_view, upcoming));
  CHECK(between(early, "\"bars\":", "\"account\"") == between(public_view, "\"bars\":", "\"account\""));
  tl_run_destroy(wire);
  tl_run_destroy(plain);
}
