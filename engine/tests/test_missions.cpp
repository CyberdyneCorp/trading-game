#include <doctest/doctest.h>

#include <span>
#include <string>

#include "tradelab/c_api.h"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/sha256.hpp"

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
// Policy v9 (funding from the last closed bar) changed the state digests via the version.
// Policy v10 (news priced on arrival, per-seed schedule) regenerates every mission market: both digests.
// Policy v7 changed m05 (jump before the first expiry): new m05 digests; m01 only through the policy version.
// Policy v4 adds plans and calibration answers to the state digest (event digests unchanged).
// Policy v11 appends start options and advice to the state digest: m01 and m05 state digests changed
// through the version and the (default) new fields only; their event digests are unchanged, so a
// default run executes exactly as before. The advisor golden is in test_advisor.cpp.
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
        "\"digest\":\"0296b3e88ee2cc9697a1d904530457a71bde1f58ba1b52136c154e5c2e0907f1\","
        "\"eventsDigest\":\"ac136e4b4ff59a6b7b2ae93973250bf79f3cc82d22187ffae13be1d4e9f63f19\"}");
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
        "\"digest\":\"ac68db66659602d035e03c9950285400df3b2e5f450c1103b4cecfb927050bd7\","
        "\"eventsDigest\":\"908ac0b1ffd77daf85c8c88e59123a55cb3ddc5ab1509965ae294ae637c4c8aa\"}");
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
  Scenario m01;
  REQUIRE(build_mission("m01-false-breakout", 4, m01));
  const std::uint32_t rumor = m01.news[0].release_step;  // 72 plus this seed's lead
  tl_run* run = tl_run_create("m01-false-breakout", 4);
  std::string s = tl_state(run);
  CHECK(s.find("\"news\":[]") != std::string::npos);  // nothing released at the first decision
  s = tl_advance(run, rumor - m01.mission.warmup_bars);  // the rumor's step
  CHECK(s.find("\"kind\":\"institutional_adoption\"") != std::string::npos);
  CHECK(s.find("rumor_denied") == std::string::npos);   // released 10 bars later: still in the future
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
  CHECK(d.find("\"score\":{\"version\":11") != std::string::npos);
  CHECK(d.find("\"trades\":[{\"n\":1") != std::string::npos);
  CHECK(d.find("\"stop\":\"95.5") != std::string::npos);
  CHECK(d.find("\"safeLeverage\":") != std::string::npos);
  CHECK(d.find("\"spreadSlippage\":") != std::string::npos);
  CHECK(d.find("\"greeks\":null") != std::string::npos);  // no option traded
  CHECK(d.find("\"lesson\":") != std::string::npos);
  tl_run_destroy(run);
}

TEST_CASE("Regression: C ABI order events name their order type, bracket legs included") {
  // Before: the client learned order types only from the live order list, so bracket legs placed and
  // filled or cancelled within one advance showed as "LMT/STOP/TP" in the feed.
  tl_run* run = tl_run_create("m01-false-breakout", 5);
  REQUIRE(run != nullptr);
  tl_place_order_ex(run, 1, 0, 0, "10", nullptr, 0, "95.5", "110", nullptr);
  tl_advance(run, 1000);
  const std::string s = tl_state(run);
  CHECK(s.find("\"orderType\":\"market\"") != std::string::npos);
  CHECK(s.find("\"orderType\":\"stop_market\"") != std::string::npos);
  CHECK(s.find("\"orderType\":\"take_profit\"") != std::string::npos);
  std::size_t at = 0;
  std::size_t order_events = 0;
  while ((at = s.find("\"type\":\"order\",\"orderId\":", at)) != std::string::npos) {
    const std::size_t next = s.find('}', at);
    CHECK(s.substr(at, next - at).find("\"orderType\":null") == std::string::npos);
    ++order_events;
    at = next;
  }
  CHECK(order_events >= 3);
  tl_run_destroy(run);
}

// Phase-2 batch A (design D8): each new mission adds its own golden with default options; the m01 and
// m05 goldens above stay byte-identical (no execution-policy change).
namespace {

std::string digests_of(const std::string& state) { return state.substr(state.find("\"digest\"")); }

std::string reveal_digest(const char* mission, std::uint32_t seed) {
  const std::string json = tl_mission_reveal(mission, seed);
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(json.data());
  return to_hex(sha256(std::span<const std::uint8_t>(bytes, json.size())));
}

}  // namespace

TEST_CASE("Golden: m06 thin book, limit entry with a bracket and a market exit") {
  tl_run* run = tl_run_create("m06-thin-book", 2026);
  REQUIRE(run != nullptr);
  tl_set_leverage(run, 1, 2);
  tl_place_order_ex(run, 2, 1, 0, "40", "99.5", 0, "97.5", "102", nullptr);  // limit buy with a bracket
  tl_advance(run, 30);
  tl_place_order(run, 3, 0, 0, "150", nullptr, 0);  // larger than a thin bar's capacity
  tl_advance(run, 40);
  tl_close_position(run, 4);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(digests_of(s) ==
        "\"digest\":\"73915792a3b323a91ec8bd84fc7f73c8c20a9f922675ec7dd25a25442312cd25\","
        "\"eventsDigest\":\"b084466a7e0ab87dacc53c40794afcbc017917decfac736279404e82558f009a\"}");
}

TEST_CASE("Golden: m07 size and exit, a market entry then a trailing stop") {
  tl_run* run = tl_run_create("m07-trend-trailing", 2026);
  REQUIRE(run != nullptr);
  tl_set_leverage(run, 1, 5);
  tl_advance(run, 15);
  tl_place_order(run, 2, 0, 0, "60", nullptr, 0);
  tl_advance(run, 1);
  tl_place_order_ex(run, 3, 4, 1, "60", nullptr, 1, nullptr, nullptr, "2.5");  // trailing stop, reduce-only
  tl_advance(run, 40);
  tl_place_order(run, 4, 0, 1, "30", nullptr, 0);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(digests_of(s) ==
        "\"digest\":\"7bef8536703594ef7e1c7d20a5bf72a486e2165d09cfec01fcf2b72cf85d25a4\","
        "\"eventsDigest\":\"911e8dce2b537d94c11f7c8a8af67a1472f169032d0c6e98d137b4bb95488bd0\"}");
}

TEST_CASE("Golden: m08 regime switch, a fade and a follow") {
  tl_run* run = tl_run_create("m08-regime-switch", 2026);
  REQUIRE(run != nullptr);
  tl_set_leverage(run, 1, 3);
  tl_place_order_ex(run, 2, 0, 1, "50", nullptr, 0, "104", "96", nullptr);
  tl_advance(run, 35);
  tl_close_position(run, 3);
  tl_place_order_ex(run, 4, 0, 0, "80", nullptr, 0, "90", nullptr, nullptr);
  tl_advance(run, 30);
  const std::string s = tl_advance(run, 1000);
  const std::string d = tl_debrief(run);
  tl_run_destroy(run);
  CHECK(digests_of(s) ==
        "\"digest\":\"9cca2b0bb58cba31fb272c72780e4d0937dd784538d28bb36869c74e82fea9d8\","
        "\"eventsDigest\":\"e96bbb50aedd88cce5e21de92081431436bc4595affec276d05384d682d1a6bb\"}");
  CHECK(s.find("\"regimes\"") == std::string::npos);  // the run state never carries the reveal (design D6)
  CHECK(s.find("\"spans\"") == std::string::npos);
  CHECK(d.find("\"regimes\":[{\"start\":60") != std::string::npos);  // signals by regime, from the first decision
  CHECK(d.find("\"rsiFade\":{\"hits\":") != std::string::npos);
  CHECK(d.find("\"regime\":\"") != std::string::npos);  // each trade tagged with its entry regime
}

TEST_CASE("G4: tl_mission_reveal is a pure function of mission and seed (Python parity digest)") {
  CHECK(reveal_digest("m08-regime-switch", 7) == reveal_digest("m08-regime-switch", 7));
  CHECK(reveal_digest("m08-regime-switch", 7) != reveal_digest("m08-regime-switch", 8));
  CHECK(reveal_digest("m08-regime-switch", 7) == "5ac2b53bfc31fa0cdf7d25d56bda08345c3bde8978808ef514f9ba76af9ee02f");
  CHECK(std::string(tl_mission_reveal("nope", 1)) == "{\"error\":\"unknown_mission\"}");
  const std::string m01 = tl_mission_reveal("m01-false-breakout", 4);
  CHECK(m01.find("\"kind\":\"rumor_denied\",\"actual\":-1,\"surprise\":-2,\"intensityPct\":100") != std::string::npos);
}

TEST_CASE("m06 is long only at 2x with a thin book; m08 is a 180-bar chain") {
  tl_run* run = tl_run_create("m06-thin-book", 3);
  const std::string s = tl_state(run);
  CHECK(s.find("\"allowShort\":false") != std::string::npos);
  CHECK(s.find("\"leverageCap\":2") != std::string::npos);
  CHECK(s.find("\"halfSpread\":\"0.0010000000\"") != std::string::npos);
  CHECK(s.find("\"capacityFraction\":\"0.0400000000\"") != std::string::npos);
  const std::string r = tl_place_order(run, 1, 0, 1, "10", nullptr, 0);
  CHECK(r.find("short_not_allowed") != std::string::npos);
  tl_run_destroy(run);
  Scenario m08;
  REQUIRE(build_mission("m08-regime-switch", 3, m08));
  CHECK(m08.bars.size() == 180);
  CHECK(m08.news.empty());
}

// Phase-2 batch B (design D8): m09-m12 add their own goldens with default options; every golden above
// stays byte-identical (drawn outcomes and announcements act only where a mission declares them).
TEST_CASE("Golden: m09 the day before, a straddle bought before the decision and sold after") {
  tl_run* run = tl_run_create("m09-event-straddle", 2026);
  REQUIRE(run != nullptr);
  tl_advance(run, 8);
  tl_place_option_order(run, 1, 7, 0, 0, "10", nullptr);  // ATM call, first expiry
  tl_place_option_order(run, 2, 8, 0, 0, "10", nullptr);  // ATM put
  tl_advance(run, 12);
  tl_place_option_order(run, 3, 7, 1, 0, "10", nullptr);
  tl_place_option_order(run, 4, 8, 1, 0, "10", nullptr);
  tl_place_order(run, 5, 0, 0, "30", nullptr, 0);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(digests_of(s) ==
        "\"digest\":\"1392b2ad137a4d857a12a57ad1b154d8e7d6f6c061b3dec388a8e52c049b0e08\","
        "\"eventsDigest\":\"6afafb44fd90acf3ba94b9a18ffb072d0757af70047b7103f75adcd0a617ea83\"}");
}

TEST_CASE("Golden: m10 cover, a collar on a long through the rumor") {
  tl_run* run = tl_run_create("m10-hedged-book", 2026);
  REQUIRE(run != nullptr);
  tl_set_leverage(run, 1, 1);
  tl_place_order(run, 2, 0, 0, "40", nullptr, 0);
  tl_place_option_order(run, 3, 20, 0, 0, "40", nullptr);  // put one strike below ATM, second expiry
  tl_place_option_order(run, 4, 23, 1, 0, "40", nullptr);  // call one strike above ATM, written
  tl_advance(run, 50);
  tl_close_position(run, 5);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(digests_of(s) ==
        "\"digest\":\"c72b368e8a95c70f3544ac0834af7cb60f641ff7b259747f56db903bbd078ffc\","
        "\"eventsDigest\":\"ae540e1bed50c0d2e6995cc7d63fe6b8d04d772c11c34fd198d65688b87f3251\"}");
}

TEST_CASE("Golden: m11 calendar, flat into a release and a trade after it") {
  tl_run* run = tl_run_create("m11-news-calendar", 2026);
  REQUIRE(run != nullptr);
  tl_set_leverage(run, 1, 3);
  tl_place_order_ex(run, 2, 0, 0, "60", nullptr, 0, "95", "110", nullptr);
  tl_advance(run, 20);
  tl_close_position(run, 3);
  tl_advance(run, 30);
  tl_place_order_ex(run, 4, 0, 1, "60", nullptr, 0, "112", "90", nullptr);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(digests_of(s) ==
        "\"digest\":\"c3df3be7347ec2f5f9b94c3c4d7d6d5e982aeb9c9600b25ea36b2004a3bf2fe5\","
        "\"eventsDigest\":\"839e5f90afee251e6cc920fb1b83a13faa1789a97071fabd9f6b538870a1babb\"}");
}

TEST_CASE("Golden: m12 final exam, a long, a written straddle and a short") {
  tl_run* run = tl_run_create("m12-capstone", 2026);
  REQUIRE(run != nullptr);
  tl_set_leverage(run, 1, 4);
  tl_place_order_ex(run, 2, 0, 0, "80", nullptr, 0, "95", nullptr, nullptr);
  tl_place_option_order(run, 3, 7, 1, 0, "10", nullptr);
  tl_place_option_order(run, 4, 8, 1, 0, "10", nullptr);
  tl_advance(run, 45);
  tl_close_position(run, 5);
  tl_advance(run, 30);
  tl_place_order_ex(run, 6, 0, 1, "50", nullptr, 0, "115", nullptr, nullptr);
  const std::string s = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(digests_of(s) ==
        "\"digest\":\"6cf7a03b9800436de835ffcba145e3dd4c02071f6bceab57adc11fc47fd56465\","
        "\"eventsDigest\":\"70f0eb16beb797db73246a800de61e636a5f4593dd699e5a6f64bd4c55fac459\"}");
}

TEST_CASE("G4 with G2: the reveal reports each event's drawn outcome, intensity, gap and implied move") {
  CHECK(reveal_digest("m11-news-calendar", 7) == reveal_digest("m11-news-calendar", 7));
  CHECK(reveal_digest("m11-news-calendar", 7) == "4282fd1e468d68bd8be98bd86422c185e85c48e25559b8c5ff3d6e710b1b3fe8");
  MissionReveal r;
  bool intensity_varies = false;
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    REQUIRE(mission_reveal("m09-event-straddle", seed, r));
    REQUIRE(r.events.size() == 1);
    const EventReveal& e = r.events.front();
    CHECK(e.intensity_pct >= 60);
    CHECK(e.intensity_pct <= 140);
    intensity_varies = intensity_varies || e.intensity_pct != 100;
    CHECK(e.announce_lead == 20);
    CHECK(e.implied_bp > 0);  // the straddle the day before
    Scenario sc;
    REQUIRE(build_mission("m09-event-straddle", seed, sc));
    CHECK(sc.news.front().actual == e.actual);
  }
  CHECK(intensity_varies);
  const std::string json = tl_mission_reveal("m09-event-straddle", 3);
  CHECK(json.find("\"announceLead\":20,\"gapBp\":") != std::string::npos);
  CHECK(json.find("\"impliedBp\":") != std::string::npos);
}

TEST_CASE("G3 through the C ABI: m09 lists the decision as upcoming with its consensus only") {
  Scenario sc;
  REQUIRE(build_mission("m09-event-straddle", 5, sc));
  const NewsItem& n = sc.news.front();
  tl_run* run = tl_run_create("m09-event-straddle", 5);
  const std::string start = tl_state(run);
  const std::string listed = "\"upcoming\":[{\"step\":" + std::to_string(n.release_step) + ",\"kind\":\"rate_decision\",\"expectation\":0}]";
  CHECK(start.find(listed) != std::string::npos);  // announced 20 bars ahead: before the first decision
  CHECK(start.find("\"kind\":\"rate_decision\",\"tone\"") == std::string::npos);
  const std::string released = tl_advance(run, static_cast<std::int32_t>(n.release_step - sc.mission.warmup_bars));
  CHECK(released.find("\"upcoming\":[]") != std::string::npos);
  CHECK(released.find("\"actual\":" + std::to_string(n.actual)) != std::string::npos);
  tl_run_destroy(run);
}

TEST_CASE("m09 buys options only (Options III comes later); m10 and m12 write; m11 has no chain") {
  tl_run* m09 = tl_run_create("m09-event-straddle", 3);
  CHECK(std::string(tl_state(m09)).find("\"allowShort\":false") != std::string::npos);
  CHECK(std::string(tl_place_option_order(m09, 1, 7, 1, 0, "1", nullptr)).find("short_options_not_allowed") != std::string::npos);
  CHECK(std::string(tl_place_option_order(m09, 2, 7, 0, 0, "1", nullptr)).find("\"accepted\":true") != std::string::npos);
  tl_run_destroy(m09);
  for (const char* id : {"m10-hedged-book", "m12-capstone"}) {
    tl_run* run = tl_run_create(id, 3);
    CHECK(std::string(tl_place_option_order(run, 1, 7, 1, 0, "1", nullptr)).find("\"accepted\":true") != std::string::npos);
    tl_run_destroy(run);
  }
  Scenario m11;
  REQUIRE(build_mission("m11-news-calendar", 3, m11));
  CHECK(m11.options.series.empty());
  CHECK(m11.bars.size() == 200);
  Scenario m12;
  REQUIRE(build_mission("m12-capstone", 3, m12));
  CHECK(m12.bars.size() == 230);
  CHECK(m12.contract.funding_interval == 8);
}
