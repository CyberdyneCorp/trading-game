// Building packs (design D1, D5): the signed archive writer, pack sources (a manifest plus the
// uncompressed data files, for the packer to compress and sign), the private CSV-import pack, and
// writing or re-signing a pack from stored files.

#include <algorithm>

#include "json_in.hpp"
#include "json_out.hpp"
#include "pack_codec.hpp"
#include "pack_manifest.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/pack.hpp"
#include "tradelab/scoring.hpp"
#include "tradelab/sha256.hpp"

namespace tradelab {
namespace {

using detail::JsonMember;
using detail::JsonOut;
using detail::JsonValue;

constexpr std::uint32_t kMinPlayable = 20;
constexpr std::uint32_t kFundingPeriodSeconds = 8 * 3600;
constexpr std::size_t kMaxMetaText = 64;
constexpr std::size_t kCsvMaxFilledBars = PackLimits{}.max_bars;  // rows plus continuity bars
constexpr std::uint32_t kMissionStepSeconds = 3600;

JsonValue json_str(std::string s) {
  JsonValue v;
  v.kind = JsonValue::Kind::String;
  v.text = std::move(s);
  return v;
}

JsonValue json_int(std::uint64_t n) {
  JsonValue v;
  v.kind = JsonValue::Kind::Int;
  v.integer = static_cast<std::int64_t>(n);
  return v;
}

void set_member(JsonValue& obj, const std::string& key, JsonValue v) {
  for (auto& m : obj.members) {
    if (m.key == key) {
      m.value = std::move(v);
      return;
    }
  }
  obj.members.push_back(JsonMember{key, std::move(v)});
}

JsonValue files_json(const std::vector<PackFile>& files) {
  JsonValue arr;
  arr.kind = JsonValue::Kind::Array;
  for (const PackFile& f : files) {
    JsonValue e;
    e.kind = JsonValue::Kind::Object;
    set_member(e, "name", json_str(f.name));
    set_member(e, "size", json_int(f.stored.size()));
    set_member(e, "decompressedSize", json_int(f.decompressed_size));
    set_member(e, "sha256", json_str(to_hex(sha256(f.stored))));
    arr.items.push_back(std::move(e));
  }
  return arr;
}

// "YYYY-MM-DDTHH:MM:SSZ" from UTC seconds (Howard Hinnant's civil_from_days).
std::string iso_utc(std::int64_t t) {
  const std::int64_t days = (t >= 0 ? t : t - 86'399) / 86'400;
  const std::int64_t secs = t - days * 86'400;
  const std::int64_t z = days + 719'468;
  const std::int64_t era = (z >= 0 ? z : z - 146'096) / 146'097;
  const std::int64_t doe = z - era * 146'097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36'524 - doe / 146'096) / 365;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
  const std::int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);
  const auto two = [](std::int64_t v) { return (v < 10 ? "0" : "") + std::to_string(v); };
  return std::to_string(y) + "-" + two(m) + "-" + two(d) + "T" + two(secs / 3600) + ":" + two(secs / 60 % 60) + ":" +
         two(secs % 60) + "Z";
}

std::string timeframe_name(std::uint32_t s) {
  if (s % 86'400 == 0) return std::to_string(s / 86'400) + "d";
  if (s % 3600 == 0) return std::to_string(s / 3600) + "h";
  if (s % 60 == 0) return std::to_string(s / 60) + "m";
  return std::to_string(s) + "s";
}

ContractSpec practice_contract(std::uint32_t interval_seconds) {
  ContractSpec c;
  c.multiplier = Qty::from_raw(Qty::kScale);
  c.tick_size = Price::from_raw(Price::kScale / 100);  // 0.01 on the masked (base 100) scale
  c.lot_size = Qty::from_raw(Qty::kScale / 10);
  c.min_qty = c.lot_size;
  c.max_leverage = 50;
  c.maker_fee = Rate::from_raw(2'000'000);            // 0.02%
  c.taker_fee = Rate::from_raw(5'000'000);            // 0.05%
  c.maintenance_margin = Rate::from_raw(50'000'000);  // 0.5%
  c.funding_interval = std::max<std::uint32_t>(1, kFundingPeriodSeconds / interval_seconds);
  return c;
}

CostModel practice_costs() {
  CostModel k;
  k.half_spread = Rate::from_raw(1'000'000);            // 0.01%
  k.slippage = Rate::from_raw(2'000'000);               // 0.02%
  k.capacity_fraction = Rate::from_raw(1'000'000'000);  // 10% of bar volume
  return k;
}

// Everything a manifest says besides the scenario's own contract, costs, rules and data counts.
struct ManifestHead {
  std::string scenario_id;
  std::string mode;  // historical | synthetic | branch
  bool private_pack = false;
  std::uint32_t step_seconds = 3600;
  bool funding_file = false;
  std::uint32_t gaps = 0;
  std::optional<std::uint32_t> branch_step;
  std::vector<std::pair<std::string, std::string>> provenance;
  std::string license_name;
  std::string attribution;
  bool transformation = true;
  bool commercial = false;
  bool offline = false;
  bool downloadable = false;
};

void write_options(JsonOut& j, const OptionSpec& o) {
  j.key("options").begin_obj().field("multiplier", o.multiplier).field("premiumTick", o.premium_tick).field("lotSize", o.lot_size);
  j.field("feeRate", o.fee_rate).field("spread", o.spread).field("skew", o.skew).field("smile", o.smile).field("termSlope", o.term_slope);
  j.field("minutesPerStep", o.minutes_per_step).field("settlementMinutes", o.settlement_minutes);
  j.field("imBase", o.im_base).field("imFloor", o.im_floor).field("mmBase", o.mm_base).field("allowShort", o.allow_short).end_obj();
}

void write_market(JsonOut& j, const Scenario& sc, const ManifestHead& h) {
  const ContractSpec& c = sc.contract;
  const bool options = !sc.options.series.empty();
  j.key("instruments").begin_obj().key("perpetual").begin_obj();
  j.field("multiplier", c.multiplier).field("tickSize", c.tick_size).field("lotSize", c.lot_size).field("minQty", c.min_qty);
  j.field("maxLeverage", c.max_leverage).end_obj();
  if (options) write_options(j, sc.options);
  j.end_obj();
  j.key("costModel").begin_obj().field("makerFee", c.maker_fee).field("takerFee", c.taker_fee);
  j.field("halfSpread", sc.costs.half_spread).field("slippage", sc.costs.slippage).end_obj();
  j.key("liquidityModel").begin_obj().field("capacityFraction", sc.costs.capacity_fraction).end_obj();
  j.key("fundingModel").begin_obj().field("intervalSteps", c.funding_interval).field("source", h.funding_file ? "file" : "none").end_obj();
  const char* iv = options ? (sc.options.iv_base.empty() ? "realized" : "file") : "none";
  j.key("ivModel").begin_obj().field("kind", iv).end_obj();
  j.key("marginModel").begin_obj().field("maintenanceMargin", c.maintenance_margin).end_obj();
}

void write_data(JsonOut& j, const Scenario& sc, const ManifestHead& h) {
  const MissionRules& r = sc.mission;
  j.key("missionRules").begin_obj().field("initialBalance", r.initial_balance);
  j.field("leverageCap", r.leverage_cap).field("warmupBars", r.warmup_bars).field("allowShort", r.allow_short).end_obj();
  j.key("data").begin_obj().field("bars", static_cast<std::uint32_t>(sc.bars.size())).field("gaps", h.gaps);
  // Tables appear only when the pack has them, so a perpetual-only pack keeps the v1 field set.
  if (!sc.news.empty()) j.field("events", static_cast<std::uint32_t>(sc.news.size()));
  if (!sc.liquidity.empty()) j.field("liquidityShocks", static_cast<std::uint32_t>(sc.liquidity.size()));
  if (!sc.options.series.empty()) j.field("optionSeries", static_cast<std::uint32_t>(sc.options.series.size()));
  if (h.branch_step) j.field("branchStep", *h.branch_step);
  // Continuity bars (policy v17) give bars.bin its sixth column; the policy says how indicators treat them.
  if (std::any_of(sc.bars.begin(), sc.bars.end(), [](const Bar& b) { return b.continuity; })) {
    j.field("gapPolicy", r.gap_policy == GapPolicy::CarryForward ? "carryForward" : "skip");
  }
  j.end_obj();
}

void write_rights(JsonOut& j, const ManifestHead& h) {
  j.key("files").begin_arr().end_arr().field("signingKeyId", "");
  j.key("provenance").begin_obj();
  for (const auto& [k, v] : h.provenance) j.field(k, v);
  j.end_obj();
  j.key("license").begin_obj().field("name", h.license_name).field("attribution", h.attribution).field("transformation", h.transformation);
  j.field("commercial", h.commercial).field("offlineRedistribution", h.offline).end_obj();
  j.key("distribution").begin_obj().field("downloadable", h.downloadable).end_obj();
}

// The manifest of a scenario. Stored canonically (sorted keys) when signed, so member order here
// does not matter.
std::string manifest_text(const Scenario& sc, const ManifestHead& h) {
  JsonOut j;
  j.begin_obj().field("schemaVersion", kPackSchemaVersion).field("scenarioId", h.scenario_id).field("packVersion", std::uint32_t{1});
  j.field("mode", h.mode).field("private", h.private_pack);
  j.key("engineCompatibility").begin_obj().field("min", kEngineVersion).field("max", kEngineVersion).end_obj();
  j.field("executionPolicyVersion", kExecutionPolicyVersion).field("scoringVersion", kScoringVersion);
  j.field("timeframe", timeframe_name(h.step_seconds)).field("stepSeconds", h.step_seconds);
  j.field("displayPrecision", std::uint32_t{2}).field("aliasPolicy", "per-run");
  write_market(j, sc, h);
  write_data(j, sc, h);
  write_rights(j, h);
  j.end_obj();
  return j.text();
}

std::vector<std::uint8_t> as_bytes(const std::string& s) { return {s.begin(), s.end()}; }

// The data files of a scenario, uncompressed (stored = raw).
std::vector<PackFile> raw_files(const Scenario& sc, bool funding) {
  const auto raw = [](std::string name, std::vector<std::uint8_t> bytes) {
    const std::uint64_t n = bytes.size();
    return PackFile{std::move(name), std::move(bytes), n};
  };
  std::vector<PackFile> files = {raw("bars.bin", encode_bars(sc.bars))};
  if (funding) files.push_back(raw("funding.bin", encode_funding(sc.funding_rate)));
  if (!sc.news.empty() || !sc.liquidity.empty()) files.push_back(raw("events.bin", encode_events(sc.news, sc.liquidity)));
  if (!sc.options.series.empty()) files.push_back(raw("options.bin", encode_options(sc.options)));
  return files;
}

bool meta_ok(const CsvMeta& m) {
  return m.asset.size() <= kMaxMetaText && m.pair.size() <= kMaxMetaText && m.venue.size() <= kMaxMetaText;
}

std::uint32_t auto_warmup(std::uint32_t rows) { return std::clamp<std::uint32_t>(rows / 4, 20, 60); }

PackSource source_failed(PackSource out, const char* reason, std::string detail = {}) {
  out.reason = reason;
  out.detail = std::move(detail);
  return out;
}

std::string reveal_text(const CsvMeta& meta, const CsvReport& report, std::uint32_t warmup, const std::optional<BranchRequest>& branch) {
  JsonOut j;
  j.begin_obj().field("asset", meta.asset).field("pair", meta.pair).field("venue", meta.venue);
  j.field("periodFrom", iso_utc(report.first_time)).field("periodTo", iso_utc(report.last_time));
  j.field("source", "csv-import");
  std::string transformation = "prices x 100 / open of bar " + std::to_string(warmup) + ", half-even to 0.01; volume x the inverse";
  if (branch) {
    transformation += "; after bar " + std::to_string(branch->step) + ": " + branch->mission + " seed " + std::to_string(branch->seed) +
                      ", rescaled to continue from its close (synthetic)";
  }
  j.field("transformation", transformation);
  j.end_obj();
  return j.text();
}

// The masked CSV market as a scenario with the practice contract and rules.
Scenario csv_scenario(std::vector<Bar> masked, std::vector<Rate> funding, const CsvMeta& meta, std::uint32_t warmup,
                      const ImportRules& rules) {
  Scenario sc;
  sc.contract = practice_contract(meta.interval_seconds);
  sc.costs = practice_costs();
  sc.mission.initial_balance = Money::from_raw(10'000 * Money::kScale);
  sc.mission.leverage_cap = rules.leverage_cap;
  sc.mission.warmup_bars = warmup;
  sc.mission.allow_short = true;
  sc.mission.gap_policy = rules.fill_gaps.value_or(GapPolicy::Skip);
  sc.bars = std::move(masked);
  sc.funding_rate = std::move(funding);
  return sc;
}

// Splices the branch market in; false (with the reason) when it cannot.
bool apply_branch(const BranchRequest& b, Scenario& sc, std::string* reason) {
  Scenario alt;
  if (!build_mission(b.mission, b.seed, alt)) {
    *reason = "unknown_mission";
    return false;
  }
  Scenario out;
  if (b.step < sc.mission.warmup_bars || !splice_branch(sc, b.step, alt, out)) {
    *reason = "invalid_branch";
    return false;
  }
  sc = std::move(out);
  return true;
}

PackFile stored(PackFile f) {
  f.stored = detail::zstd_store(f.stored);
  return f;
}

PackBuild build_failed(PackBuild out, std::string reason) {
  out.reason = std::move(reason);
  return out;
}

bool is_bin(std::string_view name) { return name.size() > 4 && name.substr(name.size() - 4) == ".bin"; }

PackBuild signed_pack(std::string_view manifest, const std::vector<PackFile>& files, const SecretSeed& seed, std::string_view prefix) {
  PackBuild out;
  const KeyPair key = ed25519_keypair(seed);
  out.bytes = write_pack(manifest, files, key, key_id(prefix, key.public_key));
  if (out.bytes.empty()) return build_failed(std::move(out), "manifest_invalid");
  out.digest = to_hex(sha256(out.bytes));
  return out;
}

// Bars and funding from the parsed rows. With `fill`, each missing interval becomes a continuity bar
// (flat at the previous close, no volume, no funding); otherwise the rows are joined as they are.
void csv_bars(const std::vector<CsvRow>& rows, const CsvMeta& meta, bool fill, std::vector<Bar>* bars,
              std::vector<Rate>* funding) {
  const std::int64_t step = meta.interval_seconds;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const CsvRow& r = rows[i];
    const std::int64_t missing = fill && i > 0 ? (r.time - rows[i - 1].time) / step - 1 : 0;
    for (std::int64_t k = 0; k < missing && bars->size() <= kCsvMaxFilledBars; ++k) {
      const Price c = bars->back().close;
      bars->push_back(Bar{c, c, c, c, Qty{}, false, true});
      funding->push_back(Rate{});
    }
    bars->push_back(Bar{r.open, r.high, r.low, r.close, r.volume, false});
    funding->push_back(r.funding);
  }
}

}  // namespace

std::vector<std::uint8_t> write_pack(std::string_view manifest_json, const std::vector<PackFile>& files, const KeyPair& key,
                                     const std::string& key_id) {
  auto parsed = detail::parse_json(manifest_json);
  if (!parsed.ok || !parsed.value.is(JsonValue::Kind::Object)) return {};
  JsonValue root = std::move(parsed.value);
  set_member(root, "files", files_json(files));
  set_member(root, "signingKeyId", json_str(key_id));
  const std::string canonical = detail::canonical_json(root);
  const auto manifest = as_bytes(canonical);
  const Signature sig = ed25519_sign(key, manifest);
  std::vector<detail::TarFile> entries = {{"manifest.json", manifest}, {"manifest.sig", {sig.begin(), sig.end()}}};
  for (const PackFile& f : files) entries.push_back({f.name, f.stored});
  return detail::tar_write(entries);
}

PackSource mission_pack_source(const std::string& mission_id, std::uint64_t seed) {
  PackSource out;
  Scenario sc;
  if (!build_mission(mission_id, seed, sc)) return source_failed(std::move(out), "unknown_mission", mission_id);
  ManifestHead h;
  h.scenario_id = mission_id + "-s" + std::to_string(seed);
  h.mode = "synthetic";
  h.step_seconds = kMissionStepSeconds;
  h.funding_file = true;
  h.provenance = {{"source", "generator"}, {"generatorVersion", std::to_string(kGeneratorVersion)}, {"mission", mission_id},
                  {"seed", std::to_string(seed)}, {"tool", std::string("tradelab-engine ") + kEngineVersion}};
  h.license_name = "tradelab-synthetic";
  h.attribution = "TradeLab synthetic market generator";
  h.commercial = h.offline = h.downloadable = true;
  out.scenario_id = h.scenario_id;
  out.manifest = manifest_text(sc, h);
  out.files = raw_files(sc, true);
  return out;
}

PackSource csv_pack_source(std::string_view csv, const CsvMeta& meta, const ImportRules& rules, bool private_pack,
                           const std::optional<BranchRequest>& branch) {
  PackSource out;
  std::vector<CsvRow> rows;
  out.report = import_csv(csv, meta, &rows);
  if (!out.report.ok) return source_failed(std::move(out), "csv_invalid");
  std::vector<Bar> raw;
  std::vector<Rate> funding;
  csv_bars(rows, meta, rules.fill_gaps.has_value() && out.report.gap_count > 0, &raw, &funding);
  if (raw.size() > kCsvMaxFilledBars) return source_failed(std::move(out), "too_many_bars");
  const auto n = static_cast<std::uint32_t>(raw.size());
  const std::uint32_t warmup = rules.warmup_bars == 0 ? auto_warmup(n) : rules.warmup_bars;
  if (!meta_ok(meta) || rules.leverage_cap < 1 || rules.leverage_cap > 20 || warmup + kMinPlayable > n) {
    return source_failed(std::move(out), "invalid_rules");
  }
  std::vector<Bar> masked;
  if (!mask_bars(raw, warmup, practice_contract(meta.interval_seconds).tick_size, &masked)) return source_failed(std::move(out), "masking_failed");
  const std::string bars_hash = to_hex(sha256(encode_bars(masked)));
  Scenario sc = csv_scenario(std::move(masked), std::move(funding), meta, warmup, rules);
  std::string reason;
  if (branch && !apply_branch(*branch, sc, &reason)) return source_failed(std::move(out), reason.c_str());
  ManifestHead h;
  h.scenario_id = "csv-" + bars_hash.substr(0, 12);
  h.mode = branch ? "branch" : "historical";
  h.private_pack = private_pack;
  h.step_seconds = meta.interval_seconds;
  h.funding_file = out.report.has_funding || branch.has_value();
  h.gaps = out.report.gap_count;
  if (branch) h.branch_step = branch->step;
  h.provenance = {{"source", "csv-import"}, {"tool", std::string("tradelab-engine ") + kEngineVersion}};
  if (branch) {
    h.provenance.insert(h.provenance.end(), {{"generatorVersion", std::to_string(kGeneratorVersion)},
                                             {"branchMission", branch->mission},
                                             {"branchSeed", std::to_string(branch->seed)}});
  }
  h.license_name = private_pack ? "private-import" : "unlicensed";
  out.scenario_id = h.scenario_id;
  out.manifest = manifest_text(sc, h);
  out.files = raw_files(sc, h.funding_file);
  const std::string reveal = reveal_text(meta, out.report, warmup, branch);
  out.files.push_back({"reveal.json", as_bytes(reveal), reveal.size()});
  return out;
}

std::vector<std::uint8_t> source_archive(const PackSource& source) {
  std::vector<detail::TarFile> entries = {{"manifest.json", as_bytes(source.manifest)}};
  for (const PackFile& f : source.files) entries.push_back({f.name, f.stored});
  return detail::tar_write(entries);
}

PackBuild build_csv_pack(std::string_view csv, const CsvMeta& meta, const ImportRules& rules, const SecretSeed& device_seed) {
  PackSource src = csv_pack_source(csv, meta, rules, true, std::nullopt);
  PackBuild out;
  out.report = std::move(src.report);
  if (!src.ok()) return build_failed(std::move(out), src.reason);
  std::vector<PackFile> files;
  files.reserve(src.files.size());
  for (PackFile& f : src.files) files.push_back(is_bin(f.name) ? stored(std::move(f)) : std::move(f));
  PackBuild signed_out = signed_pack(src.manifest, files, device_seed, "device");
  signed_out.report = std::move(out.report);
  signed_out.scenario_id = src.scenario_id;
  return signed_out;
}

PackBuild assemble_pack(std::string_view manifest_json, std::span<const std::uint8_t> files_tar, const SecretSeed& seed,
                        std::string_view key_prefix) {
  std::vector<detail::TarEntry> entries;
  if (detail::tar_read(files_tar, PackLimits{}.max_files - 2, &entries) != detail::TarStatus::Ok) {
    return build_failed({}, "malformed_container");
  }
  std::vector<PackFile> files;
  for (const detail::TarEntry& e : entries) {
    PackFile f{e.name, {e.data.begin(), e.data.end()}, e.data.size()};
    if (is_bin(e.name)) {
      const auto size = detail::zstd_content_size(e.data);
      if (!size) return build_failed({}, "malformed_data:" + e.name);
      f.decompressed_size = *size;
    }
    files.push_back(std::move(f));
  }
  return signed_pack(manifest_json, files, seed, key_prefix);
}

PackBuild resign_pack(std::span<const std::uint8_t> pack, const SecretSeed& seed, std::string_view key_prefix) {
  std::vector<detail::TarEntry> entries;
  if (detail::tar_read(pack, PackLimits{}.max_files, &entries) != detail::TarStatus::Ok) return build_failed({}, "malformed_container");
  const auto manifest = std::find_if(entries.begin(), entries.end(), [](const auto& e) { return e.name == "manifest.json"; });
  if (manifest == entries.end()) return build_failed({}, "missing_file");
  const std::string text(manifest->data.begin(), manifest->data.end());
  const auto parsed = detail::parse_json(text);
  detail::Manifest m;
  detail::ManifestError err;
  if (!parsed.ok || !detail::parse_manifest(parsed.value, &m, &err)) return build_failed({}, parsed.ok ? err.reason : "manifest_invalid");
  if (m.private_pack) return build_failed({}, "private_pack");
  std::vector<PackFile> files;
  for (const detail::ManifestFile& mf : m.files) {
    const auto e = std::find_if(entries.begin(), entries.end(), [&mf](const auto& x) { return x.name == mf.name; });
    if (e == entries.end()) return build_failed({}, "missing_file");
    files.push_back({mf.name, {e->data.begin(), e->data.end()}, mf.decompressed_size});
  }
  return signed_pack(text, files, seed, key_prefix);
}

}  // namespace tradelab
