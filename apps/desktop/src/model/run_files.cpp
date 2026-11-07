#include "model/run_files.hpp"

#include <tradelab/c_api.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "model/storage/json_writer.hpp"

namespace tradelab::desktop {

namespace {

// The engine's run-file calls return a static buffer: one caller at a time.
std::mutex& engine_mutex() {
  static std::mutex m;
  return m;
}

using RunFileCall = const char* (*)(const char*, const uint8_t*, uint32_t, const char*);

json::Value call(RunFileCall fn, const std::string& text, std::span<const std::uint8_t> pack, const std::string& keys_json) {
  std::string out;
  {
    const std::lock_guard<std::mutex> lock(engine_mutex());
    out = fn(text.c_str(), pack.empty() ? nullptr : pack.data(), static_cast<uint32_t>(pack.size()), keys_json.c_str());
  }
  auto v = json::parse(out);
  return v ? *v : json::Value{};
}

// The longest prefix of `s` of at most `max` bytes that does not cut a UTF-8 sequence.
std::string clip(const std::string& s, std::size_t max) {
  if (s.size() <= max) return s;
  std::size_t n = max;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0U) == 0x80U) --n;
  return s.substr(0, n);
}

void decimal_field(storage::JsonWriter& w, std::string_view key, const json::Value& v) {
  const auto text = v.kind() == json::Value::Kind::Number ? decimal_text(v.as_double()) : std::nullopt;
  if (text) w.field(key, *text);
  else w.key(key).null();
}

// One plan note in the file's shape (web noteToFile): amounts as decimal strings.
void write_note(storage::JsonWriter& w, const json::Value& p) {
  const double conf = std::round(p["confidence"].as_double());
  w.begin_object().field("step", p["step"].as_int()).field("side", p["side"].as_string()).field("thesis", clip(p["thesis"].as_string(), 1000));
  decimal_field(w, "invalidation", p["invalidation"]);
  decimal_field(w, "target", p["target"]);
  decimal_field(w, "plannedRisk", p["plannedRisk"]);
  w.field("confidence", static_cast<std::int64_t>(std::clamp(conf, 0.0, 100.0))).field("skipped", p["skipped"].as_bool());
  if (!p["loseScenario"].as_string().empty()) w.field("loseScenario", clip(p["loseScenario"].as_string(), 1000));
  if (!p["instrument"].as_string().empty()) w.field("instrument", clip(p["instrument"].as_string(), 64));
  if (!p["combo"].as_string().empty()) w.field("combo", p["combo"].as_string());
  w.end_object();
}

}  // namespace

std::optional<std::string> decimal_text(double v) {
  if (!std::isfinite(v) || v < 0 || v >= 1e12) return std::nullopt;
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.8f", v);
  std::string s(buf);
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s;
}

std::string run_file_draft(const storage::RunRecord& record, const std::vector<std::string>& actions, const EngineStamp& engine,
                           bool include_notes) {
  storage::JsonWriter w;
  w.begin_object().field("format", "tradelab-run").field("version", 1).field("mission", record.mission_id);
  w.field("pack", record.pack_digest).field("seed", record.seed).key("options").raw(storage::options_json(record.options));
  w.key("engine").begin_object().field("executionPolicy", record.execution_policy).field("abi", engine.abi);
  w.field("version", engine.version).end_object();
  w.key("labels").begin_object().field("campaign", record.campaign.has_value()).field("assisted", record.assisted.has_value()).end_object();
  w.key("actions").begin_array();
  for (const std::string& a : actions) w.raw(a);
  w.end_array().key("journal").begin_array();
  const auto journal = include_notes ? json::parse(record.journal) : std::nullopt;
  if (journal) {
    for (const json::Value& p : journal->items()) write_note(w, p);
  }
  return w.end_array().end_object().text();
}

std::optional<std::string> export_block(const storage::RunRecord& record, std::size_t actions, const std::vector<storage::StoredPack>& packs) {
  if (actions == 0) return "no_actions";
  if (!record.pack_digest) return std::nullopt;
  const auto it = std::find_if(packs.begin(), packs.end(), [&](const storage::StoredPack& p) { return p.digest == *record.pack_digest; });
  if (it == packs.end()) return "pack_missing";
  if (it->is_private || it->origin == "import") return "private_pack";
  return std::nullopt;
}

json::Value verify_run_file(const std::string& text, std::span<const std::uint8_t> pack, const std::string& keys_json) {
  return call(tl_run_file_verify, text, pack, keys_json);
}

json::Value seal_run_file(const std::string& text, std::span<const std::uint8_t> pack, const std::string& keys_json) {
  return call(tl_run_file_seal, text, pack, keys_json);
}

std::string run_file_name(const std::string& mission, std::uint32_t seed) {
  std::string m = mission;
  for (char& c : m) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-';
    if (!ok) c = '_';
  }
  return "tradelab-" + m + "-" + std::to_string(seed) + ".tlrun";
}

}  // namespace tradelab::desktop
