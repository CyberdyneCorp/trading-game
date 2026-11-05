#include "golden.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

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

// -1 for an unknown name: the engine rejects it and the fixture's "rejected" list no longer matches.
std::int32_t code_of(const JsonValue& obj, std::string_view key, std::span<const std::string_view> names) {
  const std::string& name = member(obj, key).text;
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (names[i] == name) return static_cast<std::int32_t>(i);
  }
  return -1;
}

constexpr std::string_view kSides[] = {"buy", "sell"};                                                   // NOLINT
constexpr std::string_view kOrderTypes[] = {"market", "limit", "stop_market", "take_profit", "trailing_stop"};  // NOLINT
constexpr std::string_view kOptionTypes[] = {"market", "limit"};                                         // NOLINT

std::int32_t side(const JsonValue& a) { return code_of(a, "side", kSides); }

const char* place_order(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  const std::int32_t type = code_of(a, "type", kOrderTypes);
  const char* rate = opt_text(a, "trailRate");
  if (type == 4 && rate != nullptr && *rate != '\0') {
    return tl_place_trailing_rate(run, cid, side(a), opt_text(a, "qty"), flag(a, "ro"), rate);
  }
  return tl_place_order_ex(run, cid, type, side(a), opt_text(a, "qty"), opt_text(a, "price"), flag(a, "ro"),
                           opt_text(a, "stop"), opt_text(a, "tp"), opt_text(a, "trail"));
}

const char* place_option(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  return tl_place_option_order(run, cid, static_cast<std::uint32_t>(int_of(a, "series")), side(a),
                               code_of(a, "type", kOptionTypes), opt_text(a, "qty"), opt_text(a, "premium"));
}

const char* record_plan(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  return tl_record_plan(run, cid, side(a), flag(a, "skipped"), flag(a, "option"), flag(a, "lose"), opt_text(a, "inv"),
                        opt_text(a, "target"), int_of(a, "conf"), opt_text(a, "risk"));
}

const char* answer_lesson(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  return tl_answer_lesson(run, cid, static_cast<std::uint32_t>(int_of(a, "q")),
                          static_cast<std::uint32_t>(int_of(a, "choice")), opt_text(a, "value"), flag(a, "skipped"));
}

using Handler = const char* (*)(tl_run*, std::uint32_t, const JsonValue&);

// The web client's action kinds (bindings/python/tradelab_engine/replay.py documents the fields).
const std::map<std::string_view, Handler>& handlers() {
  static const std::map<std::string_view, Handler> table = {
      {"lev", [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_set_leverage(r, c, int_of(a, "lev")); }},
      {"order", place_order},
      {"opt", place_option},
      {"margin", [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_adjust_margin(r, c, opt_text(a, "amount")); }},
      {"plan", record_plan},
      {"ans",
       [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_answer_question(r, c, int_of(a, "pBp"), flag(a, "skipped")); }},
      {"lesson", answer_lesson},
      {"cancel",
       [](tl_run* r, std::uint32_t c, const JsonValue& a) { return tl_cancel_order(r, c, static_cast<std::uint32_t>(int_of(a, "id"))); }},
      {"move",
       [](tl_run* r, std::uint32_t c, const JsonValue& a) {
         return tl_move_order(r, c, static_cast<std::uint32_t>(int_of(a, "id")), opt_text(a, "price"));
       }},
      {"close", [](tl_run* r, std::uint32_t c, const JsonValue&) { return tl_close_position(r, c); }},
      {"advice", [](tl_run* r, std::uint32_t c, const JsonValue&) { return tl_request_advice(r, c); }},
  };
  return table;
}

// The command result's reason when it was rejected; "" when accepted. Unknown kinds are rejections.
std::string apply(tl_run* run, std::uint32_t cid, const JsonValue& a) {
  const auto it = handlers().find(member(a, "k").text);
  if (it == handlers().end()) return "unknown_action_kind";
  const detail::JsonParse result = detail::parse_json(it->second(run, cid, a));
  if (!result.ok) return "unparsable_result";
  return member(result.value, "accepted").boolean ? "" : member(result.value, "reason").text;
}

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
    if (member(actions[i], "k").text == "adv") {
      tl_advance(run, int_of(actions[i], "n"));
      continue;
    }
    std::string reason = apply(run, cid++, actions[i]);
    if (!reason.empty()) out.rejected.push_back({i, std::move(reason)});
  }
  out.ok = true;
  out.state = tl_state(run);
  out.debrief = tl_debrief(run);
  tl_run_destroy(run);
  return out;
}

std::string reveal_sha256(const JsonValue& fx) {
  const std::string json =
      tl_mission_reveal(text(fx, "mission").c_str(), static_cast<std::uint32_t>(member(fx, "seed").integer));
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(json.data());
  return to_hex(sha256(std::span<const std::uint8_t>(bytes, json.size())));
}

}  // namespace tradelab::golden
