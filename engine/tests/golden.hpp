#pragma once

// Shared golden fixtures (tests/golden/*.json, format in engine/README.md), embedded at configure
// time, and their replay through the C ABI. bindings/python/tests/golden.py is the Python twin.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../src/json_in.hpp"

namespace tradelab::golden {

inline constexpr std::int64_t kFormat = 1;

struct Fixture {
  std::string_view name;
  std::string_view json;
};

// Every fixture listed in tests/golden/index.json, in index order.
std::span<const Fixture> fixtures();

// Parsed fixture by name; ok == false when unknown or malformed.
detail::JsonParse load(std::string_view name);

struct Rejection {
  std::size_t action = 0;  // index in "actions" (advances included)
  std::string reason;
};

struct RunResult {
  bool ok = false;     // false: the market could not be built (unknown mission, pack refused)
  std::string state;   // tl_state after the last action
  std::string debrief; // tl_debrief (an error object while the run is active)
  std::string coach;   // tl_coach_review (an error object while the run is active)
  std::vector<Rejection> rejected;
};

// Replays a "run" or "coach" fixture: its market (mission or CSV pack), seed, start options and action log.
RunResult replay_run(const detail::JsonValue& fx);

// SHA-256 (hex) of the exact bytes tl_mission_reveal returns for a "reveal" fixture.
std::string reveal_sha256(const detail::JsonValue& fx);

// tl_option_whatif's output for a "whatif" fixture's request, and the fixture's expected result, both
// in canonical JSON (RFC 8785) so they compare as text.
std::string whatif_output(const detail::JsonValue& fx);
std::string whatif_expected(const detail::JsonValue& fx);

// A "coach" fixture's expected review in canonical JSON, and a replay's review in canonical JSON.
std::string coach_expected(const detail::JsonValue& fx);
std::string coach_output(const RunResult& r);

// The CSV text of a "csvPack" market: its lines joined with '\n', with a final '\n'.
std::string csv_text(const detail::JsonValue& pack);

// Text of a string member, "" when absent or not a string.
std::string text(const detail::JsonValue& obj, std::string_view key);

}  // namespace tradelab::golden
