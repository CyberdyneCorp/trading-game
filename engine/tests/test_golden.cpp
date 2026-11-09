#include <doctest/doctest.h>

#include <set>
#include <string_view>
#include <utility>
#include <vector>
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

golden::RunResult check_run(const JsonValue& fx) {
  const JsonValue& expected = *fx.find("expected");
  golden::RunResult r = golden::replay_run(fx);
  REQUIRE(r.ok);
  CHECK(r.state.substr(r.state.find("\"digest\"")) == digest_tail(expected));
  CHECK(r.state.find("\"status\":\"" + golden::text(expected, "status") + "\"") != std::string::npos);
  const std::vector<JsonValue>& rejected = expected.find("rejected")->items;
  REQUIRE(r.rejected.size() == rejected.size());
  for (std::size_t i = 0; i < rejected.size(); ++i) {
    CHECK(r.rejected[i].action == static_cast<std::size_t>(rejected[i].find("action")->integer));
    CHECK(r.rejected[i].reason == golden::text(rejected[i], "reason"));
  }
  return r;
}

}  // namespace

TEST_CASE("Golden fixtures: every shared scripted run, reveal and what-if replays to its frozen output") {
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
    } else if (kind == "whatif") {
      CHECK(golden::whatif_expected(fx).find("\"breakevens\"") != std::string::npos);
      CHECK(golden::whatif_output(fx) == golden::whatif_expected(fx));
    } else if (kind == "coach") {  // a scripted run and its coach review (add-practice-and-advanced-strategies D17)
      const golden::RunResult r = check_run(fx);
      CHECK(golden::coach_expected(fx).find("\"moments\":[{") != std::string::npos);
      CHECK(golden::coach_output(r) == golden::coach_expected(fx));
    } else {
      REQUIRE(kind == "run");
      check_run(fx);
    }
  }
  CHECK(kinds == std::set<std::string>{"coach", "reveal", "run", "whatif"});
  CHECK(golden::fixtures().size() >= 16);  // m01, m02 (advice), m05-m12, the CSV pack, two reveals, three what-ifs
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

// Execution policy v18 (design D8 of add-practice-and-advanced-strategies): time in force and OCO
// links are new commands and order fields, so the state digests changed through the policy number and
// the appended order fields only. Every scripted golden that predates v18 must execute exactly as it
// did under v17: its event digest equals the value frozen under v17, listed here.
TEST_CASE("Golden fixtures: every pre-v18 run's event digest equals its frozen v17 value") {
  const std::vector<std::pair<std::string_view, std::string_view>> v17 = {
      {"m01-false-breakout", "ac136e4b4ff59a6b7b2ae93973250bf79f3cc82d22187ffae13be1d4e9f63f19"},
      {"m02-advisor-options", "c83e5525e4d03ad71d5eafcef3af65a2e1d43798c5303ec43e51cbc89e7a888f"},
      {"m05-options-writer", "908ac0b1ffd77daf85c8c88e59123a55cb3ddc5ab1509965ae294ae637c4c8aa"},
      {"m06-thin-book", "b084466a7e0ab87dacc53c40794afcbc017917decfac736279404e82558f009a"},
      {"m07-trend-trailing", "911e8dce2b537d94c11f7c8a8af67a1472f169032d0c6e98d137b4bb95488bd0"},
      {"m08-regime-switch", "e96bbb50aedd88cce5e21de92081431436bc4595affec276d05384d682d1a6bb"},
      {"m09-event-straddle", "6afafb44fd90acf3ba94b9a18ffb072d0757af70047b7103f75adcd0a617ea83"},
      {"m10-hedged-book", "ae540e1bed50c0d2e6995cc7d63fe6b8d04d772c11c34fd198d65688b87f3251"},
      {"m11-news-calendar", "839e5f90afee251e6cc920fb1b83a13faa1789a97071fabd9f6b538870a1babb"},
      {"m12-capstone", "70f0eb16beb797db73246a800de61e636a5f4593dd699e5a6f64bd4c55fac459"},
      {"m13-call-spread", "8b0a3f74575ae2f77bbbde1d16ecc62d3ce5d909c35ed67d6e84ae0c87843433"},
      {"m14-covered-call", "12cb8f47b53a6414d788dad0a8dac3ede3bcc27c8f87fbbd7c18c60f69972097"},
      {"m15-protective-put", "bec9cea0f089d8610e48bd4a2f08e0602aa1ff4a38e36d371a3deb3f5e590084"},
      {"m16-range-condor", "f32b59783303ee7581829fbc1d6808a47d8f96bac490329d53d9fb2eaa97fdae"},
      {"m17-before-expiry", "e633be3ae42db18b3c86f8bff46527a8b61ef0196e592698873cec77525355b8"},
      {"csv-pack", "e3e42dedf85efb5b7fa7b55835e1998d82b3c552100f3c017214b6b585d382da"},
  };
  for (const auto& [name, events] : v17) {
    INFO("fixture ", name);
    const detail::JsonParse p = golden::load(name);
    REQUIRE(p.ok);
    const golden::RunResult r = golden::replay_run(p.value);
    REQUIRE(r.ok);
    CHECK(r.state.find("\"eventsDigest\":\"" + std::string(events) + "\"") != std::string::npos);
    CHECK(golden::text(*p.value.find("expected"), "eventsDigest") == events);
  }
}
