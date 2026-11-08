#include <doctest/doctest.h>

#include <string>

#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/checkpoint.hpp"
#include "tradelab/generator.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {
std::string digests(const RunState& st) { return to_hex(state_digest(st)) + to_hex(events_digest(st)); }

// Applies a deterministic pseudo-random command script for steps [from, to).
void script(const Scenario& sc, RunState& st, std::uint64_t& seq, std::uint32_t from, std::uint32_t to) {
  Pcg32 rng(seq * 7919 + from, Stream::Sandbox);
  for (std::uint32_t i = from; i < to && st.status == RunStatus::Active; ++i) {
    Command c;
    c.seq = ++seq;
    c.client_id = seq;
    c.decision_step = st.step;
    c.qty = Q("5");
    c.side = rng.next_bounded(2) == 0 ? OrderSide::Buy : OrderSide::Sell;
    if (rng.next_bounded(3) == 0) (void)apply_command(sc, st, c);
    advance(sc, st);
  }
}
}  // namespace

TEST_CASE("Checkpoint: restore + continue equals an uninterrupted run (randomized)") {
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    Scenario sc;
    REQUIRE(build_mission(seed % 2 == 0 ? "m05-options-writer" : "m01-false-breakout", seed, sc));
    RunState full = start_run(sc, seed);
    std::uint64_t seq_full = 0;
    script(sc, full, seq_full, 0, 30);
    const auto bytes = save_checkpoint(full, sc.id);
    script(sc, full, seq_full, 30, 60);

    RunState resumed = start_run(sc, seed);
    std::uint64_t seq_res = 0;
    script(sc, resumed, seq_res, 0, 30);
    auto restored = load_checkpoint(bytes, sc.id);
    REQUIRE(restored.ok());
    CHECK(digests(restored.value()) == digests(resumed));
    RunState cont = restored.value();
    script(sc, cont, seq_res, 30, 60);
    CHECK(digests(cont) == digests(full));
  }
}

TEST_CASE("Checkpoint: other scenario, damaged data and other policy versions are rejected") {
  Scenario sc;
  REQUIRE(build_mission("m01-false-breakout", 3, sc));
  RunState st = start_run(sc, 3);
  advance(sc, st);
  auto bytes = save_checkpoint(st, sc.id);
  CHECK(load_checkpoint(bytes, "m02-funding-squeeze").error() == ErrorCode::ScenarioMismatch);
  auto truncated = bytes;
  truncated.resize(truncated.size() - 3);
  CHECK(load_checkpoint(truncated, sc.id).error() == ErrorCode::TruncatedInput);
  auto trailing = bytes;
  trailing.push_back(0);
  CHECK(load_checkpoint(trailing, sc.id).error() == ErrorCode::NonCanonicalEncoding);
  auto other_policy = bytes;
  other_policy[8] = static_cast<std::uint8_t>(other_policy[8] + 1);  // policy u32 at offset 8
  CHECK(load_checkpoint(other_policy, sc.id).error() == ErrorCode::IncompatiblePolicy);
  auto bad_magic = bytes;
  bad_magic[0] = 0;
  CHECK(load_checkpoint(bad_magic, sc.id).error() == ErrorCode::NonCanonicalEncoding);
  // Formats 4 and 5 (before lesson answers, before the policy v18 order fields) are refused, never read as 6.
  REQUIRE(kCheckpointFormat == 6);
  for (const std::uint8_t old : {std::uint8_t{4}, std::uint8_t{5}}) {
    auto earlier = bytes;
    earlier[4] = old;  // format u32 (little endian) at offset 4
    CHECK(load_checkpoint(earlier, sc.id).error() == ErrorCode::NonCanonicalEncoding);
  }
}

TEST_CASE("C ABI: checkpoint and restore continue identically") {
  tl_run* a = tl_run_create("m03-volatility-shock", 21);
  tl_set_leverage(a, 1, 10);
  tl_place_order(a, 2, 0, 0, "50", nullptr, 0);
  tl_advance(a, 7);
  std::string cp = tl_checkpoint(a);
  const auto start = cp.find(":\"") + 2;
  const std::string hex = cp.substr(start, cp.find('"', start) - start);
  tl_run* b = tl_run_restore("m03-volatility-shock", 21, hex.c_str());
  REQUIRE(b != nullptr);
  CHECK(tl_run_restore("m01-false-breakout", 21, hex.c_str()) == nullptr);
  CHECK(tl_run_restore("m03-volatility-shock", 21, "zz") == nullptr);
  tl_close_position(a, 3);
  tl_close_position(b, 3);
  const std::string sa = tl_advance(a, 1000);
  const std::string sb = tl_advance(b, 1000);
  CHECK(sa.substr(sa.find("\"digest\"")) == sb.substr(sb.find("\"digest\"")));
  tl_run_destroy(a);
  tl_run_destroy(b);
}

TEST_CASE("Adjust margin: add moves liquidation away; remove only down to initial margin") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  Driver d(sc);
  CHECK(d.margin("10").reason == Reason::PositionClosed);
  d.leverage(10);
  d.order(OrderType::Market, OrderSide::Buy, "10");  // notional 1000, IM 100
  d.step();
  Price before;
  REQUIRE(detail_liq(d, &before));
  CHECK(d.margin("50").accepted);
  Price after;
  REQUIRE(detail_liq(d, &after));
  CHECK(after < before);  // more margin, liquidation further away for a long
  CHECK(d.margin("-50").accepted);
  CHECK(d.margin("-1").reason == Reason::MarginBelowInitial);
  CHECK(d.margin("100000").reason == Reason::InsufficientMargin);
  CHECK(d.state().position.margin == M("100"));
  CHECK(last_of(d.state(), EventType::MarginAdjusted) != nullptr);
}
