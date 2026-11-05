// C ABI for scenario packs and private CSV import (c_api.h). Inputs are untrusted JSON and bytes;
// outputs never contain bars or reveal metadata (those reach the client only through tl_state up
// to the current step, and tl_debrief).

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include "c_api_run.hpp"
#include "pack_codec.hpp"
#include "json_in.hpp"
#include "json_out.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/pack.hpp"

namespace tradelab {
namespace {

using detail::JsonOut;
using detail::JsonValue;

std::string g_buffer;               // run-less JSON results
std::vector<std::uint8_t> g_blob;   // last built pack

const char* emit(const JsonOut& j) {
  g_buffer = j.text();
  return g_buffer.c_str();
}

std::string text_field(const JsonValue& v, std::string_view key) {
  const JsonValue* f = v.find(key);
  return f != nullptr && f->is(JsonValue::Kind::String) ? f->text : std::string();
}

std::optional<std::int64_t> int_field(const JsonValue& v, std::string_view key, std::int64_t fallback) {
  const JsonValue* f = v.find(key);
  if (f == nullptr) return fallback;
  if (!f->is(JsonValue::Kind::Int)) return std::nullopt;
  return f->integer;
}

// Returns the offending field, or an empty string when the metadata is usable.
std::string parse_meta(const char* json, CsvMeta* meta, ImportRules* rules) {
  const auto parsed = detail::parse_json(json == nullptr ? "" : json, {4096, 4, 128, 16});
  if (!parsed.ok || !parsed.value.is(JsonValue::Kind::Object)) return "meta";
  const JsonValue& v = parsed.value;
  meta->asset = text_field(v, "asset");
  meta->pair = text_field(v, "pair");
  meta->venue = text_field(v, "venue");
  const std::string volume = text_field(v, "volumeUnit");
  const std::string unit = text_field(v, "timestampUnit");
  if (volume != "base" && volume != "quote") return "volumeUnit";
  if (unit != "s" && unit != "ms") return "timestampUnit";
  meta->volume_unit = volume == "quote" ? VolumeUnit::Quote : VolumeUnit::Base;
  meta->timestamp_unit = unit == "ms" ? TimestampUnit::Milliseconds : TimestampUnit::Seconds;
  const auto interval = int_field(v, "intervalSeconds", 0);
  if (!interval || *interval < 60 || *interval > std::int64_t{7} * 86'400) return "intervalSeconds";
  meta->interval_seconds = static_cast<std::uint32_t>(*interval);
  const auto lev = int_field(v, "leverageCap", 5);
  const auto warmup = int_field(v, "warmupBars", 0);
  if (!lev || *lev < 1 || *lev > 20) return "leverageCap";
  if (!warmup || *warmup < 0 || *warmup > 10'000) return "warmupBars";
  rules->leverage_cap = static_cast<std::int32_t>(*lev);
  rules->warmup_bars = static_cast<std::uint32_t>(*warmup);
  return {};
}

bool parse_trusted(const char* json, std::vector<TrustedKey>* out) {
  const auto parsed = detail::parse_json(json == nullptr ? "[]" : json, {std::size_t{16} << 10U, 4, 128, 64});
  if (!parsed.ok || !parsed.value.is(JsonValue::Kind::Array)) return false;
  for (const JsonValue& e : parsed.value.items) {
    const auto key = parse_hex32(text_field(e, "key"));
    const std::string id = text_field(e, "id");
    if (!key || id.empty()) return false;
    out->push_back({id, *key});
  }
  return true;
}

std::optional<SecretSeed> parse_seed(const char* hex) { return parse_hex32(hex == nullptr ? "" : hex); }

void write_report(JsonOut& j, const CsvReport& r) {
  j.field("ok", r.ok).field("rows", r.rows).key("errors").begin_arr();
  for (const CsvIssue& e : r.errors) j.begin_obj().field("line", e.line).field("code", e.code).end_obj();
  j.end_arr().field("errorCount", r.error_count).key("gaps").begin_arr();
  for (const CsvGap& g : r.gaps) {
    j.begin_obj().field("line", g.line).field("from", g.from).field("to", g.to).field("missing", g.missing).end_obj();
  }
  j.end_arr().field("gapCount", r.gap_count).field("hasFunding", r.has_funding);
  j.field("firstTime", r.first_time).field("lastTime", r.last_time);
}

// What the menu may show before a run: identity and rules, never prices or the reveal.
void write_summary(JsonOut& j, const PackOpen& p) {
  const Scenario& sc = p.scenario;
  j.key("pack").begin_obj().field("scenarioId", p.info.scenario_id).field("mode", p.info.mode);
  j.field("private", p.info.private_pack).field("knownScenario", p.info.private_pack).field("digest", p.info.digest);
  j.field("signingKeyId", p.info.signing_key_id).field("timeframe", p.info.timeframe);
  j.field("totalBars", static_cast<std::uint32_t>(sc.bars.size())).field("warmupBars", sc.mission.warmup_bars);
  j.field("leverageCap", std::min(sc.mission.leverage_cap, sc.contract.max_leverage)).field("allowShort", sc.mission.allow_short);
  j.field("gaps", p.info.gaps).end_obj();
}

bool prefix_ok(const char* prefix) {
  const std::string_view p = prefix == nullptr ? "" : prefix;
  return !p.empty() && p.size() <= 16 && std::all_of(p.begin(), p.end(), [](char c) { return c >= 'a' && c <= 'z'; });
}

// Branch fields of a CSV source's metadata; nullopt without "branchStep". `bad` names a bad field.
std::optional<BranchRequest> parse_branch(const char* json, std::string* bad) {
  const auto parsed = detail::parse_json(json == nullptr ? "" : json, {4096, 4, 128, 16});
  if (!parsed.ok || parsed.value.find("branchStep") == nullptr) return std::nullopt;
  const JsonValue& v = parsed.value;
  const auto step = int_field(v, "branchStep", 0);
  const auto seed = int_field(v, "branchSeed", 0);
  BranchRequest b{0, text_field(v, "branchMission"), 0};
  if (!step || *step < 0 || *step > 1'000'000) *bad = "branchStep";
  else if (!seed || *seed < 0 || *seed > 0xFFFF'FFFFLL) *bad = "branchSeed";
  else if (b.mission.empty()) *bad = "branchMission";
  if (!bad->empty()) return std::nullopt;
  b.step = static_cast<std::uint32_t>(*step);
  b.seed = static_cast<std::uint64_t>(*seed);
  return b;
}

const char* emit_source(JsonOut& j, const PackSource& src) {
  j.field("ok", src.ok()).field("reason", src.reason).field("detail", src.detail).field("scenarioId", src.scenario_id);
  j.key("files").begin_arr();
  for (const PackFile& f : src.files) j.str(f.name);
  j.end_arr().end_obj();
  if (src.ok()) g_blob = source_archive(src);
  return emit(j);
}

const char* emit_build(PackBuild b) {
  JsonOut j;
  j.begin_obj().field("ok", b.ok()).field("reason", b.reason).field("digest", b.digest);
  j.field("size", static_cast<std::uint32_t>(b.bytes.size()));
  std::string id;
  if (b.ok()) {
    std::vector<detail::TarEntry> entries;
    (void)detail::tar_read(b.bytes, 8, &entries);
    const auto parsed = detail::parse_json(std::string(entries.front().data.begin(), entries.front().data.end()));
    id = text_field(parsed.value, "signingKeyId");
  }
  j.field("signingKeyId", id).end_obj();
  g_blob = std::move(b.bytes);
  return emit(j);
}

PackOpen open_from_abi(const std::uint8_t* data, std::uint32_t size, const char* keys_json, PackIntent intent) {
  std::vector<TrustedKey> keys;
  PackOpen out;
  if (!parse_trusted(keys_json, &keys)) {
    out.reason = "invalid_trusted_keys";
    return out;
  }
  if (data == nullptr && size > 0) {
    out.reason = "malformed_container";
    return out;
  }
  return open_pack({data, size}, keys, intent);
}

}  // namespace
}  // namespace tradelab

using namespace tradelab;

extern "C" {

const char* tl_csv_validate(const char* csv, const char* meta_json) {
  CsvMeta meta;
  ImportRules rules;
  JsonOut j;
  j.begin_obj();
  const std::string bad = parse_meta(meta_json, &meta, &rules);
  if (!bad.empty()) return emit(j.field("ok", false).field("metaError", bad).end_obj());
  write_report(j, import_csv(csv == nullptr ? "" : csv, meta, nullptr));
  return emit(j.end_obj());
}

const char* tl_pack_build_csv(const char* csv, const char* meta_json, const char* device_seed_hex) {
  CsvMeta meta;
  ImportRules rules;
  JsonOut j;
  j.begin_obj();
  g_blob.clear();
  const std::string bad = parse_meta(meta_json, &meta, &rules);
  const auto seed = parse_seed(device_seed_hex);
  if (!bad.empty() || !seed) {
    return emit(j.field("ok", false).field("reason", bad.empty() ? "invalid_seed" : "invalid_meta").field("detail", bad).end_obj());
  }
  PackBuild b = build_csv_pack(csv == nullptr ? "" : csv, meta, rules, *seed);
  j.field("ok", b.ok()).field("reason", b.reason).key("report").begin_obj();
  write_report(j, b.report);
  j.end_obj().field("scenarioId", b.scenario_id).field("digest", b.digest);
  j.field("size", static_cast<std::uint32_t>(b.bytes.size())).end_obj();
  g_blob = std::move(b.bytes);
  return emit(j);
}

const uint8_t* tl_blob_data(void) { return g_blob.data(); }
uint32_t tl_blob_size(void) { return static_cast<uint32_t>(g_blob.size()); }

const char* tl_device_key(const char* device_seed_hex) {
  JsonOut j;
  j.begin_obj();
  const auto seed = parse_seed(device_seed_hex);
  if (!seed) return emit(j.field("error", "invalid_seed").end_obj());
  const KeyPair k = ed25519_keypair(*seed);
  std::string hex = to_hex(k.public_key);
  return emit(j.field("id", key_id("device", k.public_key)).field("publicKey", hex).end_obj());
}

const char* tl_pack_validate(const uint8_t* data, uint32_t size, const char* trusted_keys_json, int32_t publish) {
  const PackOpen p = open_from_abi(data, size, trusted_keys_json, publish != 0 ? PackIntent::Publish : PackIntent::Install);
  JsonOut j;
  j.begin_obj().field("ok", p.ok()).field("reason", p.reason).field("detail", p.detail);
  if (p.ok()) write_summary(j, p);
  return emit(j.end_obj());
}

tl_run* tl_run_create_pack(const uint8_t* data, uint32_t size, const char* trusted_keys_json, uint32_t seed) {
  PackOpen p = open_from_abi(data, size, trusted_keys_json, PackIntent::Install);
  if (!p.ok()) return nullptr;
  auto* run = new tl_run();
  run->scenario = std::move(p.scenario);
  run->scenario.alias = market_alias(seed);
  run->from_pack = true;
  run->pack = std::move(p.info);
  run->state = start_run(run->scenario, seed);
  return run;
}

const char* tl_pack_source_mission(const char* mission_id, uint32_t seed) {
  g_blob.clear();
  JsonOut j;
  j.begin_obj();
  return emit_source(j, mission_pack_source(mission_id == nullptr ? "" : mission_id, seed));
}

const char* tl_pack_source_csv(const char* csv, const char* meta_json) {
  g_blob.clear();
  CsvMeta meta;
  ImportRules rules;
  JsonOut j;
  j.begin_obj();
  std::string bad = parse_meta(meta_json, &meta, &rules);
  const std::optional<BranchRequest> branch = bad.empty() ? parse_branch(meta_json, &bad) : std::nullopt;
  if (!bad.empty()) return emit(j.field("ok", false).field("reason", "invalid_meta").field("detail", bad).end_obj());
  PackSource src = csv_pack_source(csv == nullptr ? "" : csv, meta, rules, false, branch);
  j.key("report").begin_obj();
  write_report(j, src.report);
  j.end_obj();
  return emit_source(j, src);
}

const char* tl_pack_write(const char* manifest_json, const uint8_t* files_tar, uint32_t size, const char* seed_hex,
                          const char* key_prefix) {
  g_blob.clear();
  const auto seed = parse_seed(seed_hex);
  if (!seed || !prefix_ok(key_prefix) || (files_tar == nullptr && size > 0)) {
    PackBuild b;
    b.reason = !seed ? "invalid_seed" : (!prefix_ok(key_prefix) ? "invalid_prefix" : "malformed_container");
    return emit_build(std::move(b));
  }
  return emit_build(assemble_pack(manifest_json == nullptr ? "" : manifest_json, {files_tar, size}, *seed, key_prefix));
}

const char* tl_pack_resign(const uint8_t* pack, uint32_t size, const char* seed_hex, const char* key_prefix) {
  g_blob.clear();
  const auto seed = parse_seed(seed_hex);
  if (!seed || !prefix_ok(key_prefix) || (pack == nullptr && size > 0)) {
    PackBuild b;
    b.reason = !seed ? "invalid_seed" : (!prefix_ok(key_prefix) ? "invalid_prefix" : "malformed_container");
    return emit_build(std::move(b));
  }
  return emit_build(resign_pack({pack, size}, *seed, key_prefix));
}

const char* tl_signing_key(const char* seed_hex, const char* key_prefix) {
  JsonOut j;
  j.begin_obj();
  const auto seed = parse_seed(seed_hex);
  if (!seed) return emit(j.field("error", "invalid_seed").end_obj());
  if (!prefix_ok(key_prefix)) return emit(j.field("error", "invalid_prefix").end_obj());
  const KeyPair k = ed25519_keypair(*seed);
  return emit(j.field("id", key_id(key_prefix, k.public_key)).field("publicKey", to_hex(k.public_key)).end_obj());
}

tl_run* tl_run_restore_pack(const uint8_t* data, uint32_t size, const char* trusted_keys_json, uint32_t seed,
                            const char* checkpoint_hex) {
  tl_run* run = tl_run_create_pack(data, size, trusted_keys_json, seed);
  if (run == nullptr || detail::apply_checkpoint(*run, checkpoint_hex)) return run;
  delete run;
  return nullptr;
}

}  // extern "C"
