#include "golden.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../src/run_file.hpp"
#include "tradelab/c_api.h"
#include "tradelab/sha256.hpp"

namespace tradelab::golden {

using detail::JsonValue;
using Kind = detail::JsonValue::Kind;

namespace {

// NOLINTNEXTLINE(*-avoid-c-arrays): sized by the generated list
constexpr Fixture kFixtures[] = {
#include "golden_fixtures.inc"
};

const JsonValue kNull{};

const JsonValue& member(const JsonValue& obj, std::string_view key) {
  const JsonValue* v = obj.find(key);
  return v != nullptr ? *v : kNull;
}

// NULL for an absent or null member: the C ABI's "not set".
const char* opt_text(const JsonValue& obj, std::string_view key) {
  const JsonValue& v = member(obj, key);
  return v.is(Kind::String) ? v.text.c_str() : nullptr;
}

std::int32_t int_of(const JsonValue& obj, std::string_view key) {
  return static_cast<std::int32_t>(member(obj, key).integer);
}

std::int32_t flag(const JsonValue& obj, std::string_view key) { return member(obj, key).boolean ? 1 : 0; }

tl_run* create_pack_run(const JsonValue& pack, std::uint32_t seed) {
  const std::string seed_hex = text(pack, "deviceSeed");
  const std::string meta = detail::canonical_json(member(pack, "meta"));
  const std::string built = tl_pack_build_csv(csv_text(pack).c_str(), meta.c_str(), seed_hex.c_str());
  if (built.find("\"ok\":true") == std::string::npos) return nullptr;
  const std::vector<std::uint8_t> data(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const detail::JsonParse key = detail::parse_json(tl_device_key(seed_hex.c_str()));
  const std::string keys =
      "[{\"id\":\"" + text(key.value, "id") + "\",\"key\":\"" + text(key.value, "publicKey") + "\"}]";
  return tl_run_create_pack(data.data(), static_cast<std::uint32_t>(data.size()), keys.c_str(), seed);
}

tl_run* create_run(const JsonValue& fx) {
  const auto seed = static_cast<std::uint32_t>(member(fx, "seed").integer);
  if (const JsonValue* pack = fx.find("csvPack")) return create_pack_run(*pack, seed);
  const JsonValue& o = member(fx, "options");  // the client's run-record shape; absent = defaults
  return tl_run_create_ex(text(fx, "mission").c_str(), seed, static_cast<std::uint32_t>(int_of(o, "adviceAllowance")),
                          static_cast<std::uint32_t>(int_of(o, "newsLead")), flag(o, "calendar"),
                          opt_text(o, "startingBalance"), int_of(o, "leverageCap"));
}

}  // namespace

std::span<const Fixture> fixtures() { return kFixtures; }

detail::JsonParse load(std::string_view name) {
  for (const Fixture& f : kFixtures) {
    if (f.name == name) return detail::parse_json(f.json, {.max_bytes = std::size_t{1} << 20U});
  }
  return {};
}

std::string text(const JsonValue& obj, std::string_view key) {
  const JsonValue& v = member(obj, key);
  return v.is(Kind::String) ? v.text : std::string{};
}

std::string csv_text(const JsonValue& pack) {
  std::string out;
  for (const JsonValue& line : member(pack, "csv").items) out += line.text + "\n";
  return out;
}

RunResult replay_run(const JsonValue& fx) {
  RunResult out;
  tl_run* run = create_run(fx);
  if (run == nullptr) return out;
  std::uint32_t cid = 1;  // assigned to every non-advance action, as the web client does
  const std::vector<JsonValue>& actions = member(fx, "actions").items;
  for (std::size_t i = 0; i < actions.size(); ++i) {
    if (detail::is_advance(actions[i])) {
      tl_advance(run, int_of(actions[i], "n"));
      continue;
    }
    std::string reason = detail::apply_action(run, cid++, actions[i]);
    if (!reason.empty()) out.rejected.push_back({i, std::move(reason)});
  }
  out.ok = true;
  out.state = tl_state(run);
  out.debrief = tl_debrief(run);
  out.coach = tl_coach_review(run);
  tl_run_destroy(run);
  return out;
}

std::string reveal_sha256(const JsonValue& fx) {
  const std::string json =
      tl_mission_reveal(text(fx, "mission").c_str(), static_cast<std::uint32_t>(member(fx, "seed").integer));
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(json.data());
  return to_hex(sha256(std::span<const std::uint8_t>(bytes, json.size())));
}

std::string whatif_output(const JsonValue& fx) {
  const std::string out = tl_option_whatif(detail::canonical_json(member(fx, "request")).c_str());
  const detail::JsonParse p = detail::parse_json(out);
  return p.ok ? detail::canonical_json(p.value) : out;
}

std::string whatif_expected(const JsonValue& fx) { return detail::canonical_json(member(member(fx, "expected"), "result")); }

std::string coach_expected(const JsonValue& fx) { return detail::canonical_json(member(member(fx, "expected"), "review")); }

std::string coach_output(const RunResult& r) {
  const detail::JsonParse p = detail::parse_json(r.coach);
  return p.ok ? detail::canonical_json(p.value) : r.coach;
}

}  // namespace tradelab::golden
