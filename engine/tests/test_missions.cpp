#include <doctest/doctest.h>

#include <map>
#include <span>
#include <string>
#include <vector>

#include "golden.hpp"
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

// The scripted goldens (m01 and m05 since phase 1, m02 with start options and advice, one per phase-2
// mission m06-m12, a CSV pack run) and the reveal digests are shared fixtures in tests/golden, replayed
// by test_golden.cpp, the Python binding and the native clients. engine/README.md keeps their history.

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
  CHECK(d.find("\"score\":{\"version\":12") != std::string::npos);
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

// Phase-2 batch A (design D8): each new mission adds its own golden (tests/golden) with default options;
// the m01 and m05 goldens stay byte-identical (no execution-policy change).
namespace {

std::string reveal_digest(const char* mission, std::uint32_t seed) {
  const std::string json = tl_mission_reveal(mission, seed);
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(json.data());
  return to_hex(sha256(std::span<const std::uint8_t>(bytes, json.size())));
}

}  // namespace

TEST_CASE("Golden m08 (tests/golden): the run state never carries the reveal; the debrief has signals by regime") {
  const detail::JsonParse fx = golden::load("m08-regime-switch");
  REQUIRE(fx.ok);
  const golden::RunResult r = golden::replay_run(fx.value);
  REQUIRE(r.ok);
  const std::string& s = r.state;
  const std::string& d = r.debrief;
  CHECK(s.find("\"regimes\"") == std::string::npos);  // the run state never carries the reveal (design D6)
  CHECK(s.find("\"spans\"") == std::string::npos);
  CHECK(d.find("\"regimes\":[{\"start\":60") != std::string::npos);  // signals by regime, from the first decision
  CHECK(d.find("\"rsiFade\":{\"hits\":") != std::string::npos);
  CHECK(d.find("\"regime\":\"") != std::string::npos);  // each trade tagged with its entry regime
}

TEST_CASE("G4: tl_mission_reveal is a pure function of mission and seed (digest: tests/golden/reveal-m08)") {
  CHECK(reveal_digest("m08-regime-switch", 7) == reveal_digest("m08-regime-switch", 7));
  CHECK(reveal_digest("m08-regime-switch", 7) != reveal_digest("m08-regime-switch", 8));
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

// Phase-2 batch B (design D8): m09-m12 add their own goldens (tests/golden) with default options; every
// earlier golden stays byte-identical (drawn outcomes and announcements act only where a mission declares them).
TEST_CASE("G4 with G2: the reveal reports each event's drawn outcome, intensity, gap and implied move (digest: tests/golden/reveal-m11)") {
  CHECK(reveal_digest("m11-news-calendar", 7) == reveal_digest("m11-news-calendar", 7));
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

// Strategies (add-academy-and-strategies design D8): m13-m17 add their own goldens (tests/golden) with
// default options and use existing generator capabilities only; every earlier golden stays byte-identical.
namespace {

struct StrategyShape {
  const char* id;
  std::uint32_t bars;
  std::vector<std::uint32_t> expiries;  // bars after the listing
  std::uint32_t strikes_each_side;
  bool writes;
};

std::vector<std::uint32_t> expiry_offsets(const Scenario& sc) {
  std::vector<std::uint32_t> out;
  for (const OptionSeries& s : sc.options.series) {
    const std::uint32_t e = s.expiry_step - s.list_step;
    if (out.empty() || out.back() != e) out.push_back(e);
  }
  return out;
}

}  // namespace

TEST_CASE("Strategy missions m13-m17: last in the catalog, 3x, a 2.5-step chain listed at the first decision") {
  const std::vector<StrategyShape> shapes = {{"m13-call-spread", 151, {40, 90}, 4, true},
                                             {"m14-covered-call", 181, {24, 48, 72, 96, 120}, 6, true},
                                             {"m15-protective-put", 161, {40, 100}, 4, false},
                                             {"m16-range-condor", 131, {30, 70}, 5, true},
                                             {"m17-before-expiry", 156, {20, 45, 70, 95}, 6, true}};
  const std::vector<MissionInfo> catalog = mission_catalog();
  REQUIRE(catalog.size() == 17);
  for (std::size_t i = 0; i < shapes.size(); ++i) {
    const StrategyShape& want = shapes[i];
    CAPTURE(want.id);
    CHECK(catalog[12 + i].id == want.id);
    CHECK(catalog[12 + i].leverage_cap == 3);
    Scenario sc;
    REQUIRE(build_mission(want.id, 5, sc));
    CHECK(sc.bars.size() == want.bars);
    CHECK(sc.options.series.front().list_step == 60);
    CHECK(expiry_offsets(sc) == want.expiries);
    CHECK(sc.options.series.size() == want.expiries.size() * (2 * want.strikes_each_side + 1) * 2);
    CHECK(sc.options.series[2].strike.raw - sc.options.series[0].strike.raw == Price::kScale * 5 / 2);
    CHECK(sc.options.allow_short == want.writes);
    CHECK(sc.options.series.back().expiry_step == sc.bars.size() - 1);  // the last expiry settles on the last bar
  }
}

TEST_CASE("Strategy missions: m15 buys only; the others write; objectives come from the mission") {
  tl_run* m15 = tl_run_create("m15-protective-put", 3);
  CHECK(std::string(tl_place_option_order(m15, 1, 9, 1, 0, "1", nullptr)).find("short_options_not_allowed") != std::string::npos);
  CHECK(std::string(tl_place_option_order(m15, 2, 10, 0, 0, "1", nullptr)).find("\"accepted\":true") != std::string::npos);
  CHECK(std::string(tl_state(m15)).find("{\"code\":\"floor_before_buying\",\"valueBp\":0}") != std::string::npos);
  tl_run_destroy(m15);
  tl_run* m13 = tl_run_create("m13-call-spread", 3);
  CHECK(std::string(tl_place_option_order(m13, 1, 9, 0, 0, "1", nullptr)).find("\"accepted\":true") != std::string::npos);
  CHECK(std::string(tl_place_option_order(m13, 2, 11, 1, 0, "1", nullptr)).find("\"accepted\":true") != std::string::npos);
  const std::string state = tl_state(m13);
  CHECK(state.find("{\"code\":\"spread_breakevens\",\"valueBp\":0},{\"code\":\"premium_budget\",\"valueBp\":200},"
                   "{\"code\":\"cover_short_leg\",\"valueBp\":0}") != std::string::npos);
  tl_run_destroy(m13);
}

TEST_CASE("Strategy missions: the drawn stories are in the reveal, never in the briefing") {
  std::map<int, int> m14;
  std::map<int, int> m15;
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    MissionReveal r;
    REQUIRE(mission_reveal("m14-covered-call", seed, r));
    REQUIRE(r.events.size() == 1);
    CHECK(r.events.front().announce_lead == 0);  // the rally headline is unannounced
    m14[r.events.front().actual] += 1;
    REQUIRE(mission_reveal("m15-protective-put", seed, r));
    m15[r.events.front().actual] += 1;
    REQUIRE(mission_reveal("m16-range-condor", seed, r));
    CHECK(r.events.front().kind == NewsKind::RegulationRumor);
    REQUIRE(mission_reveal("m17-before-expiry", seed, r));
    CHECK(r.events.empty());
    CHECK(r.spans.size() >= 3);  // a drawn path of regimes (G1)
  }
  CHECK(m14.size() == 2);  // no rally (0) or a strong one (+2)
  CHECK(m14[2] >= 12);
  CHECK(m15.size() == 3);  // harmless, mild or severe
}
