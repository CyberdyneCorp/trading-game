#include <doctest/doctest.h>

#include <set>
#include <string>

#include "golden.hpp"
#include "tradelab/c_api.h"

using namespace tradelab;
using detail::JsonValue;

namespace {

std::int64_t execution_policy() {
  return detail::parse_json(tl_version()).value.find("executionPolicy")->integer;
}

// The state's last two fields, exactly as the C ABI writes them: both digests, then the end.
std::string digest_tail(const JsonValue& expected) {
  return "\"digest\":\"" + golden::text(expected, "digest") + "\",\"eventsDigest\":\"" +
         golden::text(expected, "eventsDigest") + "\"}";
}

void check_run(const JsonValue& fx) {
  const JsonValue& expected = *fx.find("expected");
  const golden::RunResult r = golden::replay_run(fx);
  REQUIRE(r.ok);
  CHECK(r.state.substr(r.state.find("\"digest\"")) == digest_tail(expected));
  CHECK(r.state.find("\"status\":\"" + golden::text(expected, "status") + "\"") != std::string::npos);
  const std::vector<JsonValue>& rejected = expected.find("rejected")->items;
  REQUIRE(r.rejected.size() == rejected.size());
  for (std::size_t i = 0; i < rejected.size(); ++i) {
    CHECK(r.rejected[i].action == static_cast<std::size_t>(rejected[i].find("action")->integer));
    CHECK(r.rejected[i].reason == golden::text(rejected[i], "reason"));
  }
}

}  // namespace

TEST_CASE("Golden fixtures: every shared scripted run and reveal replays to its frozen digests") {
  std::set<std::string> kinds;
  for (const golden::Fixture& f : golden::fixtures()) {
    INFO("fixture ", f.name);
    const detail::JsonParse p = golden::load(f.name);
    REQUIRE(p.ok);
    const JsonValue& fx = p.value;
    CHECK(fx.find("format")->integer == golden::kFormat);
    CHECK(golden::text(fx, "name") == f.name);
    CHECK(fx.find("versions")->find("executionPolicy")->integer == execution_policy());
    const std::string kind = golden::text(fx, "kind");
    kinds.insert(kind);
    if (kind == "reveal") {
      CHECK(golden::reveal_sha256(fx) == golden::text(*fx.find("expected"), "sha256"));
    } else {
      REQUIRE(kind == "run");
      check_run(fx);
    }
  }
  CHECK(kinds == std::set<std::string>{"reveal", "run"});
  CHECK(golden::fixtures().size() >= 13);  // m01, m02 (advice), m05-m12, the CSV pack, two reveals
}

TEST_CASE("Golden fixtures: replay is deterministic and an unknown action kind is a rejection") {
  const detail::JsonParse p = golden::load("m01-false-breakout");
  REQUIRE(p.ok);
  CHECK(golden::replay_run(p.value).state == golden::replay_run(p.value).state);
  JsonValue fx = p.value;
  for (detail::JsonMember& m : fx.members) {
    if (m.key == "actions") m.value.items.front().members.front().value.text = "nope";
  }
  const golden::RunResult r = golden::replay_run(fx);
  REQUIRE_FALSE(r.rejected.empty());
  CHECK(r.rejected.front().action == 0);
  CHECK(r.rejected.front().reason == "unknown_action_kind");
  CHECK_FALSE(golden::load("missing").ok);
}
