#include <doctest/doctest.h>

#include <string>

#include "tradelab/c_api.h"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"

using namespace tradelab;

TEST_CASE("every built-in mission builds a valid, reproducible scenario") {
  for (const MissionInfo& m : mission_catalog()) {
    Scenario a;
    Scenario b;
    REQUIRE(build_mission(m.id, 42, a));
    REQUIRE(build_mission(m.id, 42, b));
    CHECK(validate(a) == ScenarioError::Ok);
    CHECK(a.bars.size() == m.total_bars);
    REQUIRE(a.bars.size() == b.bars.size());
    for (std::size_t i = 0; i < a.bars.size(); ++i) {
      CHECK(a.bars[i].close == b.bars[i].close);
      CHECK(a.bars[i].volume == b.bars[i].volume);
    }
    Scenario c;
    REQUIRE(build_mission(m.id, 43, c));
    CHECK(c.bars.back().close != a.bars.back().close);  // new seed, new market
  }
}

TEST_CASE("generator funding for bar i ignores bar i (no lookahead)") {
  Scenario a;
  REQUIRE(build_mission("m01-false-breakout", 7, a));
  GeneratorSpec spec;
  spec.segments = {{10, 0.0, 0.01, 0.0, 0.0, 1.0, 0.0}};
  Scenario s1 = a;
  generate_market(spec, 1, s1);
  spec.segments = {{9, 0.0, 0.01, 0.0, 0.0, 1.0, 0.0}, {1, 0.05, 0.01, 0.0, 0.0, 1.0, 0.0}};
  Scenario s2 = a;
  generate_market(spec, 1, s2);
  CHECK(s1.funding_rate[9] == s2.funding_rate[9]);  // only the last bar's drift differs
  CHECK(s1.bars[9].close != s2.bars[9].close);
}

TEST_CASE("C ABI: full mission loop with JSON and no future bars") {
  tl_run* run = tl_run_create("m01-false-breakout", 5);
  REQUIRE(run != nullptr);
  const std::string state = tl_state(run);
  CHECK(state.find("\"status\":\"active\"") != std::string::npos);
  CHECK(tl_run_create("nope", 1) == nullptr);

  std::string r = tl_set_leverage(run, 1, 3);
  CHECK(r.find("\"accepted\":true") != std::string::npos);
  r = tl_place_order(run, 2, 0, 0, "10", nullptr, 0);
  CHECK(r.find("\"accepted\":true") != std::string::npos);
  r = tl_place_order(run, 3, 1, 0, "abc", "1", 0);
  CHECK(r.find("invalid_quantity") != std::string::npos);
  r = tl_debrief(run);
  CHECK(r.find("run_active") != std::string::npos);

  std::string s = tl_advance(run, 1);
  // Warm-up 60 => decision step 60; one advance => step 61 => exactly bars 0..61 serialized.
  const std::size_t b0 = s.find("\"bars\":[");
  const std::size_t b1 = s.find("],\"account\"");
  REQUIRE(b0 != std::string::npos);
  REQUIRE(b1 != std::string::npos);
  std::size_t count = 0;
  for (std::size_t pos = s.find("[\"", b0); pos != std::string::npos && pos < b1; pos = s.find("[\"", pos + 1)) ++count;
  CHECK(count == 62);
  CHECK(s.find("\"step\":61") != std::string::npos);
  CHECK(s.find("\"type\":\"fill\"") != std::string::npos);

  s = tl_advance(run, 10000);
  CHECK(s.find("\"status\":\"active\"") == std::string::npos);
  r = tl_debrief(run);
  CHECK(r.find("buyAndHoldPnl") != std::string::npos);
  tl_run_destroy(run);
}

TEST_CASE("C ABI: same mission, seed and commands produce identical digests") {
  auto play = [] {
    tl_run* run = tl_run_create("m03-volatility-shock", 11);
    tl_set_leverage(run, 1, 15);
    tl_place_order(run, 2, 0, 0, "100", nullptr, 0);
    tl_advance(run, 5);
    tl_place_order(run, 3, 2, 1, "100", "90", 1);
    const std::string s = tl_advance(run, 1000);
    tl_run_destroy(run);
    return s.substr(s.find("\"digest\""));
  };
  CHECK(play() == play());
}

// Policy v2 changed the state digest (new order fields). Policy v3 adds the instrument (series) field
// to orders and events, so both digests changed again; perpetual behavior tests are unchanged.
// Mission m01 later gained news events, which changed its generated market (content, not policy).
// Policy v4 adds plans and calibration answers to the state digest (event digests unchanged).
TEST_CASE("Golden: scripted mission run is bit-frozen across targets") {
  tl_run* run = tl_run_create("m01-false-breakout", 2026);
  tl_set_leverage(run, 1, 5);
  tl_place_order(run, 2, 0, 0, "120", nullptr, 0);
  tl_advance(run, 3);
  tl_place_order(run, 3, 2, 1, "120", "99", 1);
  tl_place_order(run, 4, 3, 1, "60", "104", 1);
  tl_advance(run, 20);
  tl_place_order(run, 5, 0, 1, "200", nullptr, 0);
  tl_advance(run, 30);
  tl_close_position(run, 6);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  const std::string digests = s.substr(s.find("\"digest\""));
  CHECK(digests ==
        "\"digest\":\"59ac4c176029f431a2665eef4c9d5ffcb2d9fb3a9e2a3f109170cb85089048fe\","
        "\"eventsDigest\":\"f5902b5a8963380652e1c4e718db648e97c314ea1ea089881ad2ab73f9cde0a7\"}");
}

TEST_CASE("Option missions: chain exposed at the decision step, trades settle through the C ABI") {
  tl_run* run = tl_run_create("m04-options-buyer", 9);
  REQUIRE(run != nullptr);
  std::string s = tl_state(run);
  CHECK(s.find("\"enabled\":true") != std::string::npos);
  CHECK(s.find("\"kind\":\"call\"") != std::string::npos);
  CHECK(s.find("\"allowShort\":false") != std::string::npos);
  std::string r = tl_place_option_order(run, 1, 6, 0, 0, "5", nullptr);  // buy the ATM-ish call of expiry 1
  CHECK(r.find("\"accepted\":true") != std::string::npos);
  r = tl_place_option_order(run, 2, 6, 1, 0, "10", nullptr);  // would open a short: not allowed here
  CHECK(r.find("short_options_not_allowed") != std::string::npos);
  s = tl_advance(run, 1);
  CHECK(s.find("\"positions\":[{\"series\":6") != std::string::npos);
  s = tl_advance(run, 200);
  CHECK(s.find("\"type\":\"option_settlement\"") != std::string::npos);
  CHECK(s.find("\"positions\":[]") != std::string::npos);
  tl_run_destroy(run);
}

TEST_CASE("Golden: scripted option-writing run is bit-frozen across targets") {
  tl_run* run = tl_run_create("m05-options-writer", 77);
  tl_place_option_order(run, 1, 7, 1, 0, "20", nullptr);   // write calls
  tl_place_option_order(run, 2, 8, 1, 1, "10", "0.50");    // write puts with a limit
  tl_advance(run, 30);
  tl_place_option_order(run, 3, 7, 0, 0, "10", nullptr);   // buy back half
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(s.substr(s.find("\"digest\"")) ==
        "\"digest\":\"f126b54c351e8704386c0705038a1abf44b5089ac0a527e6a8a465a717d06f93\","
        "\"eventsDigest\":\"acdd778cc1750c8cc3eed026821e947e77be54d2b7ca959330e6bf7a239c2d0e\"}");
}

TEST_CASE("News: same headline, seeded reactions include none and reversals (spec Events)") {
  ScriptedEvent ev;
  ev.item = {10, NewsKind::RateDecision, 1, 100, 0, 1};  // positive surprise, positive tone
  ev.p_none = 0.2;
  ev.p_align = 0.6;
  int none = 0;
  int up = 0;
  int down = 0;
  for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
    const int r = sample_reaction(ev, seed, 0);
    none += r == 0 ? 1 : 0;
    up += r > 0 ? 1 : 0;
    down += r < 0 ? 1 : 0;
  }
  CHECK(none == doctest::Approx(200).epsilon(0.25));
  CHECK(up == doctest::Approx(480).epsilon(0.15));    // 0.8 * 0.6
  CHECK(down == doctest::Approx(320).epsilon(0.2));   // 0.8 * 0.4: good news can still sell off
}

TEST_CASE("News: only released items are serialized; events never depend on the player") {
  tl_run* run = tl_run_create("m01-false-breakout", 4);
  std::string s = tl_state(run);
  CHECK(s.find("\"news\":[]") != std::string::npos);  // nothing released at the first decision
  s = tl_advance(run, 12);                              // step 72: rumor published
  CHECK(s.find("\"kind\":\"institutional_adoption\"") != std::string::npos);
  CHECK(s.find("rumor_denied") == std::string::npos);   // released at 82: still in the future
  tl_run_destroy(run);

  Scenario a;
  Scenario b;
  REQUIRE(build_mission("m03-volatility-shock", 8, a));
  REQUIRE(build_mission("m03-volatility-shock", 8, b));
  RunState trader = start_run(a, 8);
  Command c;
  c.seq = 1;
  c.client_id = 1;
  c.decision_step = trader.step;
  c.qty = parse_decimal<Qty>("100").value();
  c.side = OrderSide::Sell;
  (void)apply_command(a, trader, c);
  for (int i = 0; i < 30; ++i) advance(a, trader);
  for (std::size_t i = 0; i < a.bars.size(); ++i) CHECK(a.bars[i].close == b.bars[i].close);
}

TEST_CASE("C ABI: plans, answers and the score report in the debrief") {
  tl_run* run = tl_run_create("m01-false-breakout", 5);
  std::string s = tl_state(run);
  CHECK(s.find("\"calibration\":{\"due\":true") != std::string::npos);
  std::string r = tl_answer_question(run, 1, 6000, 0);
  CHECK(r.find("\"accepted\":true") != std::string::npos);
  r = tl_record_plan(run, 2, 0, 0, 0, 0, "95.5", "110", 70, "100");
  CHECK(r.find("\"accepted\":true") != std::string::npos);
  r = tl_record_plan(run, 3, 0, 0, 0, 0, "abc", nullptr, 70, "100");
  CHECK(r.find("invalid_plan") != std::string::npos);
  tl_place_order_ex(run, 4, 0, 0, "10", nullptr, 0, "95.5", "110", nullptr);
  tl_advance(run, 1000);
  const std::string d = tl_debrief(run);
  CHECK(d.find("\"score\":{\"version\":1") != std::string::npos);
  CHECK(d.find("\"trades\":[{\"n\":1") != std::string::npos);
  CHECK(d.find("\"lesson\":") != std::string::npos);
  tl_run_destroy(run);
}
