// Campaign start options (spec gameplay-loop "Game over on bankruptcy", spec curriculum
// "Progressive unlocks"; policy v12): carried capital and the curriculum's leverage gate.

#include <doctest/doctest.h>

#include <string>

#include "tradelab/c_api.h"
#include "tradelab/checkpoint.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;

namespace {

bool has(const std::string& json, const std::string& needle) { return json.find(needle) != std::string::npos; }

std::string tail_digests(const std::string& s) { return s.substr(s.find("\"digest\"")); }

std::string checkpoint_hex(tl_run* run) {
  const std::string cp = tl_checkpoint(run);
  const auto start = cp.find(":\"") + 2;
  return cp.substr(start, cp.find('"', start) - start);
}

}  // namespace

TEST_CASE("Campaign: the starting balance replaces the mission's capital, in the state, score and benchmark") {
  Scenario sc;
  REQUIRE(build_mission("m01-false-breakout", 5, sc));
  RunOptions o;
  o.starting_balance = Money::from_raw(7'250 * Money::kScale);
  RunState st = start_run(sc, 5, o);
  CHECK(st.wallet == o.starting_balance);
  CHECK(run_initial_balance(sc, st) == o.starting_balance);
  CHECK(st.metrics.peak_equity == o.starting_balance);
  while (st.status == RunStatus::Active) advance(sc, st);
  CHECK(score_run(sc, st).net_pnl.raw == 0);  // no trades: nothing won or lost against the carried capital

  const RunState plain = start_run(sc, 5);
  CHECK(run_initial_balance(sc, plain) == sc.mission.initial_balance);
  CHECK(plain.wallet == sc.mission.initial_balance);
}

TEST_CASE("Campaign: the leverage gate lowers the mission's cap, never raises it") {
  Scenario sc;
  REQUIRE(build_mission("m01-false-breakout", 5, sc));  // mission cap 5x
  RunOptions o;
  o.leverage_cap = 2;
  RunState st = start_run(sc, 5, o);
  CHECK(run_leverage_cap(sc, st) == 2);
  Command c;
  c.type = CommandType::SetLeverage;
  c.decision_step = st.step;
  c.seq = 1;
  c.client_id = 1;
  c.leverage = 3;
  CHECK(apply_command(sc, st, c).reason == Reason::LeverageAboveCap);
  c.seq = 2;
  c.client_id = 2;
  c.leverage = 2;
  CHECK(apply_command(sc, st, c).accepted);

  o.leverage_cap = 50;
  CHECK(run_leverage_cap(sc, start_run(sc, 5, o)) == 5);
  CHECK(run_leverage_cap(sc, start_run(sc, 5)) == 5);
}

TEST_CASE("C ABI: campaign options reach the state and the debrief, and survive a checkpoint") {
  tl_run* run = tl_run_create_ex("m01-false-breakout", 5, 0, 0, 0, "8000.5", 2);
  REQUIRE(run != nullptr);
  std::string s = tl_state(run);
  CHECK(has(s, "\"initialBalance\":\"8000.5"));
  CHECK(has(s, "\"leverageCap\":2"));
  CHECK(has(tl_set_leverage(run, 1, 3), "leverage_above_cap"));
  CHECK(has(tl_set_leverage(run, 2, 2), "\"accepted\":true"));

  tl_run* copy = tl_run_restore("m01-false-breakout", 5, checkpoint_hex(run).c_str());
  REQUIRE(copy != nullptr);
  CHECK(has(tl_state(copy), "\"leverageCap\":2"));
  const std::string a = tl_advance(run, 10000);
  const std::string b = tl_advance(copy, 10000);
  CHECK(tail_digests(a) == tail_digests(b));
  const std::string d = tl_debrief(run);
  CHECK(has(d, "\"finalEquity\":\"8000.5"));
  CHECK(has(d, "\"netPnl\":\"0"));
  tl_run_destroy(run);
  tl_run_destroy(copy);
}

TEST_CASE("C ABI: the campaign options are part of the state digest, not of the events") {
  tl_run* plain = tl_run_create("m01-false-breakout", 9);
  tl_run* gated = tl_run_create_ex("m01-false-breakout", 9, 0, 0, 0, "10000", 2);  // same capital as the mission
  const std::string a = tl_advance(plain, 10000);
  const std::string b = tl_advance(gated, 10000);
  CHECK(a.substr(a.find("\"eventsDigest\"")) == b.substr(b.find("\"eventsDigest\"")));
  CHECK(tail_digests(a) != tail_digests(b));
  tl_run_destroy(plain);
  tl_run_destroy(gated);
}

TEST_CASE("C ABI: invalid campaign options are refused") {
  CHECK(tl_run_create_ex("m01-false-breakout", 5, 0, 0, 0, "0", 0) == nullptr);
  CHECK(tl_run_create_ex("m01-false-breakout", 5, 0, 0, 0, "-100", 0) == nullptr);
  CHECK(tl_run_create_ex("m01-false-breakout", 5, 0, 0, 0, "lots", 0) == nullptr);
  CHECK(tl_run_create_ex("m01-false-breakout", 5, 0, 0, 0, nullptr, -1) == nullptr);
  tl_run* empty = tl_run_create_ex("m01-false-breakout", 5, 0, 0, 0, "", 0);  // "" = the mission's capital
  REQUIRE(empty != nullptr);
  CHECK(has(tl_state(empty), "\"initialBalance\":\"10000"));
  tl_run_destroy(empty);
}
