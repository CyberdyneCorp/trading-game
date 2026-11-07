// C ABI of run files (add-run-sharing-and-journal-stats design D2): an untrusted run file is parsed
// with the strict reader, checked against the version 1 schema, replayed from a fresh run with the
// shared action applier and accepted only when it reproduces its recorded status and digests.

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>

#include "c_api_run.hpp"
#include "json_in.hpp"
#include "json_out.hpp"
#include "run_file.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/idl_types.hpp"
#include "tradelab/sha256.hpp"

using tradelab::detail::JsonOut;
using tradelab::detail::JsonValue;

namespace tradelab {
namespace {

using Kind = JsonValue::Kind;

struct RunDeleter {
  void operator()(tl_run* r) const { tl_run_destroy(r); }
};
using RunPtr = std::unique_ptr<tl_run, RunDeleter>;

const JsonValue kNull{};

const JsonValue& member(const JsonValue& obj, std::string_view key) {
  const JsonValue* v = obj.find(key);
  return v != nullptr ? *v : kNull;
}

std::string text_of(const JsonValue& obj, std::string_view key) {
  const JsonValue& v = member(obj, key);
  return v.is(Kind::String) ? v.text : std::string{};
}

const char* emit(JsonOut& j) {
  static std::string buffer;
  buffer = j.text();
  return buffer.c_str();
}

// `path`: the first invalid member; `pack`: the digest a pack_required file needs; `detail`: the
// file's execution policy for policy_mismatch.
const char* refuse(std::string_view reason, std::string_view path = {}, std::string_view pack = {}, std::string_view detail = {}) {
  JsonOut j;
  j.begin_obj().field("ok", false).field("reason", reason).field("path", path).field("pack", pack).field("detail", detail).end_obj();
  return emit(j);
}

// The run a file starts from: its mission with its start options, or its catalog pack (a pack run
// starts with the default options, as every client starts one). `why` names a refusal.
struct Start {
  RunPtr run;
  std::string why;
};

Start start_mission(const JsonValue& file) {
  const std::string mission = text_of(file, "mission");
  bool known = false;
  for (const MissionInfo& m : mission_catalog()) known = known || m.id == mission;
  if (!known) return {nullptr, "unknown_mission"};
  const JsonValue& o = member(file, "options");
  const JsonValue& balance = member(o, "startingBalance");
  RunPtr run(tl_run_create_ex(mission.c_str(), static_cast<std::uint32_t>(member(file, "seed").integer),
                              static_cast<std::uint32_t>(member(o, "adviceAllowance").integer),
                              static_cast<std::uint32_t>(member(o, "newsLead").integer), member(o, "calendar").boolean ? 1 : 0,
                              balance.is(Kind::String) ? balance.text.c_str() : nullptr,
                              static_cast<std::int32_t>(member(o, "leverageCap").integer)));
  std::string why = run ? "" : "invalid_options";
  return {std::move(run), std::move(why)};
}

Start start_pack(const JsonValue& file, const std::uint8_t* pack, std::uint32_t pack_len, const char* keys_json) {
  if (pack == nullptr || pack_len == 0) return {nullptr, "pack_required"};
  RunPtr run(tl_run_create_pack(pack, pack_len, keys_json == nullptr ? "[]" : keys_json,
                                static_cast<std::uint32_t>(member(file, "seed").integer)));
  if (!run) return {nullptr, "pack_invalid"};
  if (run->pack.digest != text_of(file, "pack") || run->scenario.id != text_of(file, "mission")) return {nullptr, "pack_mismatch"};
  return {std::move(run), ""};
}

// Replays the file's actions (client ids 1, 2, ... for every action except an advance) and returns
// how many commands the engine rejected.
std::uint32_t replay(tl_run* run, const JsonValue& file) {
  std::uint32_t cid = 1;
  std::uint32_t rejected = 0;
  for (const JsonValue& a : member(file, "actions").items) {
    if (detail::is_advance(a)) {
      tl_advance(run, static_cast<std::int32_t>(member(a, "n").integer));
      continue;
    }
    rejected += detail::apply_action(run, cid++, a).empty() ? 0U : 1U;
  }
  return rejected;
}

bool reproduces(const RunState& st, const JsonValue& result) {
  return json_name(st.status) == text_of(result, "status") && to_hex(state_digest(st)) == text_of(result, "digest") &&
         to_hex(events_digest(st)) == text_of(result, "eventsDigest");
}

const char* accept(tl_run* run, const JsonValue& file, std::uint32_t rejected) {
  const std::string canonical = detail::canonical_json(file);
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(canonical.data());
  const std::string file_digest = to_hex(sha256(std::span<const std::uint8_t>(bytes, canonical.size())));
  const RunState& st = run->state;
  JsonOut j;
  j.begin_obj().field("ok", true).field("mission", text_of(file, "mission")).field("seed", st.seed);
  j.field("pack", text_of(file, "pack")).field("status", json_name(st.status)).field("step", st.step);
  j.field("digest", to_hex(state_digest(st))).field("eventsDigest", to_hex(events_digest(st)));
  j.field("actions", static_cast<std::uint32_t>(member(file, "actions").items.size())).field("rejected", rejected);
  j.field("fileDigest", file_digest).field("run", canonical).key("debrief");
  if (st.status == RunStatus::Active) j.null();
  else j.raw(tl_debrief(run));
  return emit(j.end_obj());
}

JsonValue text_value(std::string s) {
  JsonValue v;
  v.kind = Kind::String;
  v.text = std::move(s);
  return v;
}

// Sets the file's "result" member (replacing it, or appending it when absent).
void set_result(JsonValue& file, std::string status, std::string digest, std::string events) {
  JsonValue result;
  result.kind = Kind::Object;
  result.members = {{"status", text_value(std::move(status))}, {"digest", text_value(std::move(digest))},
                    {"eventsDigest", text_value(std::move(events))}};
  for (detail::JsonMember& m : file.members) {
    if (m.key == "result") {
      m.value = std::move(result);
      return;
    }
  }
  file.members.push_back({"result", std::move(result)});
}

// Parses the text; nullptr on success, otherwise the refusal to return.
const char* parse_file(const char* file_json, JsonValue& out) {
  if (file_json == nullptr) return refuse("malformed");
  if (std::strlen(file_json) > detail::kRunFileMaxBytes) return refuse("too_large");
  detail::JsonParse parsed = detail::parse_json(file_json, detail::run_file_limits());
  if (!parsed.ok) return refuse(parsed.error == "too_large" ? "too_large" : "malformed");
  out = std::move(parsed.value);
  return nullptr;
}

// Verify and seal share every step but the last: verify compares the replay with the recorded
// result, seal writes the replay's result into the file (the exporter's path: a client never
// computes a result itself, so both clients write the same file for the same run).
const char* process(const char* file_json, const std::uint8_t* pack, std::uint32_t pack_len, const char* keys_json, bool seal) {
  JsonValue file;
  if (const char* refused = parse_file(file_json, file)) return refused;
  if (seal && file.is(Kind::Object)) set_result(file, "active", std::string(64, '0'), std::string(64, '0'));  // placeholder
  const detail::RunFileProblem problem = detail::check_run_file(file);
  if (!problem.reason.empty()) return refuse(problem.reason, problem.path);
  const std::int64_t policy = member(member(file, "engine"), "executionPolicy").integer;
  if (policy != kExecutionPolicyVersion) return refuse("policy_mismatch", "engine.executionPolicy", {}, std::to_string(policy));
  const bool on_pack = member(file, "pack").is(Kind::String);
  Start start = on_pack ? start_pack(file, pack, pack_len, keys_json) : start_mission(file);
  if (!start.run) return refuse(start.why, {}, text_of(file, "pack"));
  const std::uint32_t rejected = replay(start.run.get(), file);
  const RunState& st = start.run->state;
  if (seal) set_result(file, std::string(json_name(st.status)), to_hex(state_digest(st)), to_hex(events_digest(st)));
  else if (!reproduces(st, member(file, "result"))) return refuse("digest_mismatch");
  return accept(start.run.get(), file, rejected);
}

}  // namespace
}  // namespace tradelab

using namespace tradelab;

extern "C" {

const char* tl_run_file_verify(const char* file_json, const uint8_t* pack, uint32_t pack_len, const char* keys_json) {
  return process(file_json, pack, pack_len, keys_json, false);
}

const char* tl_run_file_seal(const char* file_json, const uint8_t* pack, uint32_t pack_len, const char* keys_json) {
  return process(file_json, pack, pack_len, keys_json, true);
}

}  // extern "C"
