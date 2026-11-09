// Shared golden fixtures (engine/tests/golden, format in engine/README.md) replayed by the desktop build
// (add-native-clients task 1.6): the engine linked here, with the desktop's own flags, must reach every
// frozen digest. Each run goes through the desktop's action adapter (storage::apply_action: the same
// path EngineHost uses for live play), and each mission run is also recorded to SQLite and resumed from
// it, so a saved run replays to the engine's digest too.
#include <doctest/doctest.h>

#include <tradelab/c_api.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "model/engine_run.hpp"
#include "model/json.hpp"
#include "model/packs/files.hpp"
#include "model/storage/actions.hpp"
#include "model/storage/json_writer.hpp"
#include "model/storage/run_recorder.hpp"
#include "model/storage/storage.hpp"
#include "support/storage_fixtures.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::TempDir;

namespace {

constexpr std::int64_t kFormat = 1;

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

json::Value load(std::string_view name) {
  const auto v = json::parse(read_file(std::string(TRADELAB_GOLDEN_DIR) + "/" + std::string(name) + ".json"));
  REQUIRE_MESSAGE(v.has_value(), "unreadable fixture ", name);
  return *v;
}

std::vector<std::string> fixture_names() {
  std::vector<std::string> names;
  const json::Value index = load("index");
  for (const auto& n : index["fixtures"].items()) names.push_back(n.as_string());
  return names;
}

// The fixture's start options, in the run-record shape the desktop stores (storage::options_from_json).
RunOptions options_of(const json::Value& fx) {
  return fx.has("options") ? storage::options_from_json(storage::dump(fx["options"])) : RunOptions{};
}

std::string csv_text(const json::Value& pack) {
  std::string out;
  for (const auto& line : pack["csv"].items()) out += line.as_string() + "\n";
  return out;
}

// A csvPack market: built and signed with the fixture's device seed, trusted by that key only.
std::unique_ptr<EngineRun> pack_run(const json::Value& fx) {
  const json::Value& pack = fx["csvPack"];
  const std::string seed_hex = pack["deviceSeed"].as_string();
  const auto built = json::parse(tl_pack_build_csv(csv_text(pack).c_str(), storage::dump(pack["meta"]).c_str(), seed_hex.c_str()));
  REQUIRE((built && (*built)["ok"].as_bool(false)));
  const std::vector<std::uint8_t> bytes(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const auto key = json::parse(tl_device_key(seed_hex.c_str()));
  REQUIRE(key);
  const std::string trusted =
      R"([{"id":")" + (*key)["id"].as_string() + R"(","key":")" + (*key)["publicKey"].as_string() + R"("}])";
  const auto seed = static_cast<std::uint32_t>(fx["seed"].as_int());
  return EngineRun::adopt(tl_run_create_pack(bytes.data(), static_cast<std::uint32_t>(bytes.size()), trusted.c_str(), seed),
                          seed);
}

std::unique_ptr<EngineRun> market_of(const json::Value& fx) {
  if (fx.has("csvPack")) return pack_run(fx);
  return EngineRun::create(fx["mission"].as_string(), static_cast<std::uint32_t>(fx["seed"].as_int()), options_of(fx));
}

struct Rejection {
  std::size_t action = 0;
  std::string reason;
  bool operator==(const Rejection&) const = default;
};

std::vector<Rejection> expected_rejections(const json::Value& fx) {
  std::vector<Rejection> out;
  for (const auto& r : fx["expected"]["rejected"].items()) {
    out.push_back({static_cast<std::size_t>(r["action"].as_int()), r["reason"].as_string()});
  }
  return out;
}

// Replays the action log through the desktop's adapter; unknown kinds count as rejections, as in
// engine/tests/golden.cpp.
std::vector<Rejection> replay(EngineRun& run, const json::Value& fx) {
  std::vector<Rejection> rejected;
  const auto& acts = fx["actions"].items();
  for (std::size_t i = 0; i < acts.size(); ++i) {
    const storage::ApplyResult r = storage::apply_action(run, acts[i]);
    if (!r.known) rejected.push_back({i, "unknown_action_kind"});
    else if (!r.advance && !r.accepted) rejected.push_back({i, r.reason});
  }
  return rejected;
}

void check_state(const EngineRun& run, const json::Value& expected) {
  const auto state = json::parse(run.state_json());
  REQUIRE(state);
  CHECK((*state)["digest"].as_string() == expected["digest"].as_string());
  CHECK((*state)["eventsDigest"].as_string() == expected["eventsDigest"].as_string());
  CHECK((*state)["status"].as_string() == expected["status"].as_string());
}

// Mission runs: recorded action by action (commit before apply, settle after), then resumed from the
// database by a new recorder; the resumed run must be the fixture's.
void check_saved_and_resumed(const json::Value& fx) {
  TempDir dir;
  {
    storage::Storage storage(dir.path());
    storage::RunRecorder rec(storage);
    auto run = rec.start(fx["mission"].as_string(), static_cast<std::uint32_t>(fx["seed"].as_int()), options_of(fx));
    REQUIRE(run);
    for (const auto& a : fx["actions"].items()) {
      rec.perform(*run, storage::Action{storage::dump(a)});
      rec.settle(*run, run->snapshot());
    }
    CHECK(rec.durable());
  }
  storage::Storage storage(dir.path());
  storage::RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed);
  REQUIRE(resumed->run);
  CHECK(resumed->notice.kind == storage::ResumeNoticeKind::None);
  check_state(*resumed->run, fx["expected"]);
}

std::int64_t execution_policy() {
  return json::parse(tl_version()).value_or(json::Value{})["executionPolicy"].as_int(-1);
}

void check_reveal(const json::Value& fx) {
  const std::string text = tl_mission_reveal(fx["mission"].as_string().c_str(), static_cast<std::uint32_t>(fx["seed"].as_int()));
  const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  CHECK(packs::sha256_hex(bytes) == fx["expected"]["sha256"].as_string());
}

// Report-only valuation (add-academy-and-strategies D4): the output, field by field.
void check_whatif(const json::Value& fx) {
  const auto out = json::parse(tl_option_whatif(storage::dump(fx["request"]).c_str()));
  REQUIRE(out.has_value());
  CHECK(storage::dump(*out) == storage::dump(fx["expected"]["result"]));
}

// A run (kind "run"), or kind "coach": a mission run whose finished state's coach review
// (tl_coach_review, add-practice-and-advanced-strategies D17) is frozen too, compared field by field.
void check_run(const json::Value& fx, bool coach) {
  auto run = market_of(fx);
  REQUIRE(run);
  CHECK(replay(*run, fx) == expected_rejections(fx));
  check_state(*run, fx["expected"]);
  if (coach) {
    const auto review = json::parse(run->coach_review());
    REQUIRE(review.has_value());
    CHECK(storage::dump(*review) == storage::dump(fx["expected"]["review"]));
  }
  if (fx.has("mission")) check_saved_and_resumed(fx);
}

}  // namespace

TEST_CASE("desktop goldens: every shared fixture replays to its frozen digests through the desktop build") {
  std::set<std::string> kinds;
  const auto names = fixture_names();
  CHECK(names.size() >= 13);
  for (const auto& name : names) {
    INFO("fixture ", name);
    const json::Value fx = load(name);
    REQUIRE(fx["format"].as_int() == kFormat);
    CHECK(fx["name"].as_string() == name);
    CHECK(fx["versions"]["executionPolicy"].as_int() == execution_policy());
    const std::string kind = fx["kind"].as_string();
    kinds.insert(kind);
    if (kind == "reveal") {
      check_reveal(fx);
    } else if (kind == "whatif") {
      check_whatif(fx);
    } else {
      REQUIRE((kind == "run" || kind == "coach"));
      check_run(fx, kind == "coach");
    }
  }
  CHECK(kinds == std::set<std::string>{"coach", "reveal", "run", "whatif"});
}

// Refused while the run is active, like the reveal (design D17).
TEST_CASE("desktop goldens: the coach review is refused while the run is active") {
  const json::Value fx = load("coach-m23-second-chance");
  auto run = market_of(fx);
  REQUIRE(run);
  const auto review = json::parse(run->coach_review());
  REQUIRE(review.has_value());
  CHECK((*review)["error"].as_string() == "run_active");
}

TEST_CASE("desktop goldens: a changed action log does not reach the frozen digest") {
  const json::Value fx = load("m01-false-breakout");
  auto run = market_of(fx);
  REQUIRE(run);
  const auto& acts = fx["actions"].items();
  REQUIRE(acts.size() > 1);
  for (std::size_t i = 0; i + 1 < acts.size(); ++i) storage::apply_action(*run, acts[i]);  // the last one dropped
  const auto state = json::parse(run->state_json());
  REQUIRE(state);
  CHECK((*state)["digest"].as_string() != fx["expected"]["digest"].as_string());
}
