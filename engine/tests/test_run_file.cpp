// Run files (add-run-sharing-and-journal-stats design D1, D2, D8, D9): the shared fixtures, a round
// trip of every scripted golden, a pack run, limits, a deterministic mutation fuzz loop, and the
// report-only stopMovedAway debrief field.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../src/json_in.hpp"
#include "golden.hpp"
#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/engine.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;
using detail::JsonValue;

namespace {

struct Embedded {
  std::string_view name;
  std::string_view text;
};

// NOLINTNEXTLINE(*-avoid-c-arrays): sized by the generated list
constexpr Embedded kRunFiles[] = {
#include "run_files.inc"
};

std::string embedded(std::string_view name) {
  for (const Embedded& f : kRunFiles) {
    if (f.name == name) return std::string(f.text);
  }
  return {};
}

JsonValue parse(const std::string& text) {
  detail::JsonParse p = detail::parse_json(text, {.max_bytes = std::size_t{4} << 20U, .max_depth = 64, .max_string = 1U << 20U, .max_members = 100'000});
  REQUIRE_MESSAGE(p.ok, text.substr(0, 200));
  return std::move(p.value);
}

JsonValue verify(const std::string& text, const std::vector<std::uint8_t>& pack = {}, const std::string& keys = "[]") {
  return parse(tl_run_file_verify(text.c_str(), pack.empty() ? nullptr : pack.data(), static_cast<std::uint32_t>(pack.size()), keys.c_str()));
}

std::string str(const JsonValue& o, std::string_view key) {
  const JsonValue* v = o.find(key);
  return v != nullptr && v->is(JsonValue::Kind::String) ? v->text : std::string{};
}

bool ok(const JsonValue& verdict) { return verdict.find("ok")->boolean; }

constexpr std::string_view kDefaults = R"({"adviceAllowance":0,"calendar":false,"newsLead":0})";

// The run file a client writes for a golden run (canonical member order does not matter).
std::string file_for(const JsonValue& fx) {
  const JsonValue& expected = *fx.find("expected");
  const JsonValue* options = fx.find("options");
  return std::string(R"({"format":"tradelab-run","version":1,"mission":")") + golden::text(fx, "mission") + R"(","pack":null,"seed":)" +
         std::to_string(fx.find("seed")->integer) + R"(,"options":)" + (options != nullptr ? detail::canonical_json(*options) : std::string(kDefaults)) +
         R"(,"engine":{"executionPolicy":)" + std::to_string(tradelab::kExecutionPolicyVersion) + R"(,"abi":1,"version":"0.1.0"},"labels":{"campaign":false,"assisted":false},"actions":)" +
         detail::canonical_json(*fx.find("actions")) + R"(,"journal":[],"result":{"status":")" + golden::text(expected, "status") +
         R"(","digest":")" + golden::text(expected, "digest") + R"(","eventsDigest":")" + golden::text(expected, "eventsDigest") + "\"}}";
}

std::string replace_once(std::string s, std::string_view from, std::string_view to) {
  const std::size_t at = s.find(from);
  REQUIRE(at != std::string::npos);
  return s.replace(at, from.size(), to);
}

// One random mutation of a file (a byte changed, a range deleted or duplicated, a JSON character
// inserted, or a cut), from a deterministic xorshift64: the same mutants on every target.
struct Mutator {
  std::uint64_t x = 0x9E3779B97F4A7C15ULL;
  std::uint64_t next(std::uint64_t n) {
    x ^= x << 13U;
    x ^= x >> 7U;
    x ^= x << 17U;
    return n == 0 ? 0 : x % n;
  }
  std::string operator()(std::string m) {
    const std::size_t at = next(m.size());
    switch (next(5)) {
      case 0: m[at] = static_cast<char>(next(256)); break;
      case 1: m.erase(at, 1 + next(16)); break;
      case 2: m.insert(at, m.substr(at, 1 + next(32))); break;
      case 3: m.insert(at, 1, std::string_view(" {}[],:\"0-.e\\")[next(13)]); break;
      default: m.resize(at); break;
    }
    if (m.find('\0') != std::string::npos) m.resize(m.find('\0'));  // C strings end at NUL
    return m;
  }
};

}  // namespace

TEST_CASE("Run files: every shared fixture gets the verdict in tests/runfiles/index.json") {
  const JsonValue index = parse(embedded("index.json"));
  CHECK(index.find("format")->integer == 1);
  const auto& files = index.find("files")->items;
  CHECK(files.size() + 1 == std::size(kRunFiles));
  for (const JsonValue& entry : files) {
    const std::string name = str(entry, "name");
    INFO("run file ", name);
    const JsonValue& expected = *entry.find("expected");
    const JsonValue verdict = verify(embedded(name));
    REQUIRE(ok(verdict) == ok(expected));
    for (const detail::JsonMember& m : expected.members) {
      const JsonValue* got = verdict.find(m.key);
      REQUIRE(got != nullptr);
      CHECK(detail::canonical_json(*got) == detail::canonical_json(m.value));
    }
    if (ok(verdict)) {
      // The canonical file verifies again, to the same digest: what a client stores can be exported again.
      const JsonValue again = verify(str(verdict, "run"));
      CHECK(ok(again));
      CHECK(str(again, "fileDigest") == str(verdict, "fileDigest"));
      CHECK(verdict.find("debrief")->is(str(verdict, "status") == "active" ? JsonValue::Kind::Null : JsonValue::Kind::Object));
    }
  }
}

TEST_CASE("Run files: every scripted golden run round-trips and a tampered copy is refused") {
  for (const golden::Fixture& f : golden::fixtures()) {
    const detail::JsonParse p = golden::load(f.name);
    REQUIRE(p.ok);
    if (golden::text(p.value, "kind") != "run" || p.value.find("csvPack") != nullptr) continue;
    INFO("golden ", f.name);
    const std::string file = file_for(p.value);
    const JsonValue verdict = verify(file);
    REQUIRE_MESSAGE(ok(verdict), (str(verdict, "reason") + " " + str(verdict, "path")));
    CHECK(str(verdict, "digest") == golden::text(*p.value.find("expected"), "digest"));
    CHECK(verdict.find("rejected")->integer == static_cast<std::int64_t>(p.value.find("expected")->find("rejected")->items.size()));
    const std::string digest = golden::text(*p.value.find("expected"), "digest");
    const std::string flipped = digest.substr(0, 63) + (digest.back() == '0' ? "1" : "0");
    CHECK(str(verify(replace_once(file, digest, flipped)), "reason") == "digest_mismatch");
  }
}

TEST_CASE("Run files: a catalog pack run needs its pack, the right pack and a trusted key") {
  const detail::JsonParse p = golden::load("csv-pack");
  REQUIRE(p.ok);
  const JsonValue& csv = *p.value.find("csvPack");
  const std::string seed_hex = golden::text(csv, "deviceSeed");
  const std::string built = tl_pack_build_csv(golden::csv_text(csv).c_str(), detail::canonical_json(*csv.find("meta")).c_str(), seed_hex.c_str());
  const JsonValue build = parse(built);
  REQUIRE(ok(build));
  const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const JsonValue key = parse(tl_device_key(seed_hex.c_str()));
  const std::string keys = R"([{"id":")" + str(key, "id") + R"(","key":")" + str(key, "publicKey") + "\"}]";
  const JsonValue check = parse(tl_pack_validate(pack.data(), static_cast<std::uint32_t>(pack.size()), keys.c_str(), 0));
  const std::string digest = str(*check.find("pack"), "digest");
  const std::string scenario = str(*check.find("pack"), "scenarioId");

  std::string file = file_for(p.value);
  file = replace_once(file, R"("mission":"")", R"("mission":")" + scenario + "\"");
  file = replace_once(file, R"("pack":null)", R"("pack":")" + digest + "\"");
  const JsonValue missing = verify(file);
  CHECK(str(missing, "reason") == "pack_required");
  CHECK(str(missing, "pack") == digest);
  CHECK(str(verify(file, pack, "[]"), "reason") == "pack_invalid");  // an untrusted signing key
  const JsonValue accepted = verify(file, pack, keys);
  REQUIRE_MESSAGE(ok(accepted), str(accepted, "reason"));
  CHECK(str(accepted, "digest") == golden::text(*p.value.find("expected"), "digest"));
  CHECK(str(accepted, "pack") == digest);
  const std::string other = std::string(64, 'a');
  CHECK(str(verify(replace_once(file, digest, other), pack, keys), "reason") == "pack_mismatch");
}

TEST_CASE("Run files: limits and malformed input are refused with a reason") {
  const std::string base = embedded("m01-finished.tlrun");
  CHECK(str(parse(tl_run_file_verify(nullptr, nullptr, 0, nullptr)), "reason") == "malformed");
  CHECK(str(verify(""), "reason") == "malformed");
  CHECK(str(verify("[]"), "reason") == "not_a_run_file");
  CHECK(str(verify(base + std::string((1U << 20U), ' ')), "reason") == "too_large");
  CHECK(str(verify(std::string(R"({"format":"tradelab-run","x":)") + std::string(40, '[') + std::string(40, ']') + "}"), "reason") == "malformed");
  CHECK(str(verify(replace_once(base, "tradelab-run", "tradelab-r\xffn")), "reason") == "malformed");  // not UTF-8
  std::string many = R"({"k":"close"})";
  for (int i = 0; i < 20'000; ++i) many += R"(,{"k":"close"})";
  const JsonValue too_many = verify(replace_once(base, R"("actions": [)", R"("actions": [)" + many + ","));
  CHECK(str(too_many, "reason") == "invalid_field");
  CHECK(str(too_many, "path") == "actions");
  for (const char* n : {R"("n": 0)", R"("n": 10001)", R"("n": -1)", R"("n": "1")"}) {
    const JsonValue v = verify(replace_once(base, R"("n": 20)", n));
    CHECK(str(v, "reason") == "invalid_field");
  }
  CHECK(str(verify(replace_once(base, R"("adviceAllowance": 0)", R"("adviceAllowance": 0, "startingBalance": "0")")), "reason") == "invalid_options");
  // Regression (found by the mutation loop): an empty member name is a member like any other.
  const JsonValue empty_key = verify(replace_once(base, R"("adviceAllowance": 0)", R"("adviceAllowance": 0, "": 0)"));
  CHECK(str(empty_key, "reason") == "invalid_field");
  CHECK(str(empty_key, "path") == "options.");
}

TEST_CASE("Run files: seal writes the replay's result, so a sealed file verifies and equals the fixture") {
  const JsonValue index = parse(embedded("index.json"));
  for (const JsonValue& entry : index.find("files")->items) {
    const JsonValue& expected = *entry.find("expected");
    if (!ok(expected)) continue;
    const std::string name = str(entry, "name");
    INFO("run file ", name);
    JsonValue file = parse(embedded(name));
    std::erase_if(file.members, [](const detail::JsonMember& m) { return m.key == "result"; });
    const std::string without_result = detail::canonical_json(file);
    const JsonValue sealed = parse(tl_run_file_seal(without_result.c_str(), nullptr, 0, nullptr));
    REQUIRE(ok(sealed));
    CHECK(str(sealed, "fileDigest") == str(expected, "fileDigest"));
    CHECK(ok(verify(str(sealed, "run"))));
  }
  // A stale or forged result is overwritten; an edited action gives another (self-consistent) file.
  const std::string tampered = embedded("tampered-action.tlrun");
  const JsonValue resealed = parse(tl_run_file_seal(tampered.c_str(), nullptr, 0, nullptr));
  REQUIRE(ok(resealed));
  CHECK(str(resealed, "digest") != str(verify(embedded("m01-finished.tlrun")), "digest"));
  CHECK(ok(verify(str(resealed, "run"))));
  const std::string policy = embedded("other-policy.tlrun");
  CHECK(str(parse(tl_run_file_seal(policy.c_str(), nullptr, 0, nullptr)), "reason") == "policy_mismatch");
  CHECK(str(parse(tl_run_file_seal("{}", nullptr, 0, nullptr)), "reason") == "not_a_run_file");
}

TEST_CASE("Run files: verification never touches another run") {
  tl_run* run = tl_run_create("m03-volatility-shock", 9);
  REQUIRE(run != nullptr);
  tl_advance(run, 3);
  const std::string before = tl_state(run);
  CHECK(ok(verify(embedded("m01-finished.tlrun"))));
  CHECK_FALSE(ok(verify(embedded("tampered-action.tlrun"))));
  CHECK(std::string(tl_state(run)) == before);
  tl_run_destroy(run);
}

TEST_CASE("Run files: n single advances equal one n-bar advance (the replay viewer steps bar by bar)") {
  tl_run* batch = tl_run_create("m07-trend-trailing", 5);
  tl_run* single = tl_run_create("m07-trend-trailing", 5);
  REQUIRE(batch != nullptr);
  REQUIRE(single != nullptr);
  tl_place_order_ex(batch, 1, 0, 0, "1", nullptr, 0, nullptr, nullptr, nullptr);
  tl_place_order_ex(single, 1, 0, 0, "1", nullptr, 0, nullptr, nullptr, nullptr);
  const std::string a = tl_advance(batch, 25);
  std::string b;
  for (int i = 0; i < 25; ++i) b = tl_advance(single, 1);
  CHECK(a == b);
  tl_run_destroy(batch);
  tl_run_destroy(single);
}

TEST_CASE("Run files: random mutations never crash and never change the market or the result of an accepted file") {
  const std::string base = embedded("m07-active.tlrun");
  const JsonValue original = verify(base);
  REQUIRE(ok(original));
  const std::string id = str(original, "fileDigest");
  const JsonValue base_run = parse(str(original, "run"));
  int changed = 0;
  Mutator mutate;
  int accepted = 0;
  for (int i = 0; i < 2500; ++i) {
    const JsonValue v = verify(mutate(base));
    REQUIRE(v.find("ok") != nullptr);
    if (ok(v)) {
      // Accepted only when the market and the recorded result are untouched: the change was layout,
      // an informational member (engine.version, labels) or an action the replay ends up ignoring.
      ++accepted;
      const JsonValue run = parse(str(v, "run"));
      for (const char* key : {"mission", "pack", "seed", "options", "result"}) {
        CHECK(detail::canonical_json(*run.find(key)) == detail::canonical_json(*base_run.find(key)));
      }
      changed += str(v, "fileDigest") == id ? 0 : 1;
    } else {
      CHECK_FALSE(str(v, "reason").empty());
    }
  }
  CHECK(accepted < 2500);
  CHECK(changed < accepted);  // most accepted mutants are layout only
}

TEST_CASE("Debrief: stopMovedAway counts a stop moved away from the entry while the trade is open (D8)") {
  using namespace tradelab::test;
  const auto moved_away = [](OrderSide side, std::initializer_list<const char*> moves, const char* stop, const char* tp) {
    Scenario sc = make_scenario({{"100", "100", "100", "100"},
                                 {"100", "100", "100", "100"},
                                 {"100", "100", "100", "100"},
                                 {"100", "100", "100", "100"},
                                 {"100", "100", "100", "100"}});
    Driver d(sc);
    const auto entry = d.bracket(OrderType::Market, side, "1", nullptr, stop, tp);
    REQUIRE(entry.accepted);
    d.step();
    const auto kids = children_of(d.state(), entry.order_id);
    REQUIRE(kids.size() == 2);
    const bool first_is_stop = kids[0]->type == OrderType::StopMarket;
    const std::uint32_t stop_id = first_is_stop ? kids[0]->id : kids[1]->id;
    const std::uint32_t tp_id = first_is_stop ? kids[1]->id : kids[0]->id;
    for (const char* price : moves) {
      const std::string p(price);
      const bool on_tp = p.front() == 't';
      REQUIRE(d.move(on_tp ? tp_id : stop_id, on_tp ? p.c_str() + 1 : p.c_str()).accepted);
    }
    d.step();
    const ScoreReport r = score_run(sc, d.state());
    REQUIRE(r.trades.size() == 1);
    return r.trades[0].stop_moved_away;
  };
  CHECK(moved_away(OrderSide::Buy, {"96", "97"}, "98", "110") == 1);         // away, then back toward the entry
  CHECK(moved_away(OrderSide::Buy, {"99", "99.5"}, "98", "110") == 0);       // only toward the entry
  CHECK(moved_away(OrderSide::Buy, {"t120", "t105"}, "98", "110") == 0);     // a take-profit is not a stop
  CHECK(moved_away(OrderSide::Sell, {"104", "106", "103"}, "102", "90") == 2);  // a short's stop moved higher twice
  // An option-less golden run reports the field on every perpetual trade of its debrief.
  const detail::JsonParse fx = golden::load("m01-false-breakout");
  REQUIRE(fx.ok);
  const golden::RunResult run = golden::replay_run(fx.value);
  CHECK(run.debrief.find("\"stopMovedAway\":0") != std::string::npos);
}
