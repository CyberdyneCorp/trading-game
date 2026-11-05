// Building packs: the signed archive writer and the private CSV-import pack (design D1, D5).

#include <algorithm>

#include "json_in.hpp"
#include "json_out.hpp"
#include "pack_codec.hpp"
#include "tradelab/engine.hpp"
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

struct CsvPlan {
  const CsvMeta& meta;
  const CsvReport& report;
  std::uint32_t warmup = 0;
  std::int32_t leverage_cap = 1;
  ContractSpec contract;
  std::string scenario_id;
  const char* gap_policy = nullptr;  // data.gapPolicy when gaps were filled with continuity bars
};

std::string manifest_text(const CsvPlan& p, std::uint32_t bars) {
  const ContractSpec& c = p.contract;
  JsonOut j;
  j.begin_obj().field("schemaVersion", kPackSchemaVersion).field("scenarioId", p.scenario_id).field("packVersion", std::uint32_t{1});
  j.field("mode", "historical").field("private", true);
  j.key("engineCompatibility").begin_obj().field("min", kEngineVersion).field("max", kEngineVersion).end_obj();
  j.field("executionPolicyVersion", kExecutionPolicyVersion).field("scoringVersion", kScoringVersion);
  j.field("timeframe", timeframe_name(p.meta.interval_seconds)).field("stepSeconds", p.meta.interval_seconds);
  j.field("displayPrecision", std::uint32_t{2}).field("aliasPolicy", "per-run");
  j.key("instruments").begin_obj().key("perpetual").begin_obj();
  j.field("multiplier", c.multiplier).field("tickSize", c.tick_size).field("lotSize", c.lot_size).field("minQty", c.min_qty);
  j.field("maxLeverage", c.max_leverage).end_obj().end_obj();
  j.key("costModel").begin_obj().field("makerFee", c.maker_fee).field("takerFee", c.taker_fee);
  j.field("halfSpread", Rate::from_raw(1'000'000)).field("slippage", Rate::from_raw(2'000'000)).end_obj();
  j.key("liquidityModel").begin_obj().field("capacityFraction", Rate::from_raw(1'000'000'000)).end_obj();
  j.key("fundingModel").begin_obj().field("intervalSteps", c.funding_interval);
  j.field("source", p.report.has_funding ? "file" : "none").end_obj();
  j.key("ivModel").begin_obj().field("kind", "none").end_obj();
  j.key("marginModel").begin_obj().field("maintenanceMargin", c.maintenance_margin).end_obj();
  j.key("missionRules").begin_obj().field("initialBalance", Money::from_raw(10'000 * Money::kScale));
  j.field("leverageCap", p.leverage_cap).field("warmupBars", p.warmup).field("allowShort", true).end_obj();
  j.key("data").begin_obj().field("bars", bars).field("gaps", p.report.gap_count);
  if (p.gap_policy != nullptr) j.field("gapPolicy", p.gap_policy);
  j.end_obj();
  j.key("files").begin_arr().end_arr().field("signingKeyId", "");
  j.key("provenance").begin_obj().field("source", "csv-import").field("tool", std::string("tradelab-engine ") + kEngineVersion).end_obj();
  j.key("license").begin_obj().field("name", "private-import").field("attribution", "").field("transformation", true);
  j.field("commercial", false).field("offlineRedistribution", false).end_obj();
  j.key("distribution").begin_obj().field("downloadable", false).end_obj();
  j.end_obj();
  return j.text();
}

std::string reveal_text(const CsvPlan& p) {
  JsonOut j;
  j.begin_obj().field("asset", p.meta.asset).field("pair", p.meta.pair).field("venue", p.meta.venue);
  j.field("periodFrom", iso_utc(p.report.first_time)).field("periodTo", iso_utc(p.report.last_time));
  j.field("source", "csv-import");
  j.field("transformation", "prices x 100 / open of bar " + std::to_string(p.warmup) + ", half-even to 0.01; volume x the inverse");
  j.end_obj();
  return j.text();
}

std::vector<std::uint8_t> as_bytes(const std::string& s) { return {s.begin(), s.end()}; }

PackFile stored_bin(std::string name, const std::vector<std::uint8_t>& raw) {
  return {std::move(name), detail::zstd_store(raw), raw.size()};
}

bool meta_ok(const CsvMeta& m) {
  return m.asset.size() <= kMaxMetaText && m.pair.size() <= kMaxMetaText && m.venue.size() <= kMaxMetaText;
}

PackBuild failed(PackBuild out, const char* reason) {
  out.reason = reason;
  return out;
}

std::uint32_t auto_warmup(std::uint32_t rows) { return std::clamp<std::uint32_t>(rows / 4, 20, 60); }

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

PackBuild build_csv_pack(std::string_view csv, const CsvMeta& meta, const ImportRules& rules, const SecretSeed& device_seed) {
  PackBuild out;
  std::vector<CsvRow> rows;
  out.report = import_csv(csv, meta, &rows);
  if (!out.report.ok) return failed(std::move(out), "csv_invalid");
  std::vector<Bar> raw;
  std::vector<Rate> funding;
  const bool fill = rules.fill_gaps.has_value() && out.report.gap_count > 0;
  csv_bars(rows, meta, fill, &raw, &funding);
  if (raw.size() > kCsvMaxFilledBars) return failed(std::move(out), "too_many_bars");
  const auto n = static_cast<std::uint32_t>(raw.size());
  CsvPlan plan{meta, out.report, rules.warmup_bars == 0 ? auto_warmup(n) : rules.warmup_bars, rules.leverage_cap,
               practice_contract(meta.interval_seconds), {}};
  if (fill) plan.gap_policy = *rules.fill_gaps == GapPolicy::CarryForward ? "carryForward" : "skip";
  if (!meta_ok(meta) || plan.leverage_cap < 1 || plan.leverage_cap > 20 || plan.warmup + kMinPlayable > n) {
    return failed(std::move(out), "invalid_rules");
  }
  std::vector<Bar> masked;
  if (!mask_bars(raw, plan.warmup, plan.contract.tick_size, &masked)) return failed(std::move(out), "masking_failed");
  const auto bars_bin = encode_bars(masked);
  plan.scenario_id = "csv-" + to_hex(sha256(bars_bin)).substr(0, 12);
  std::vector<PackFile> files = {stored_bin("bars.bin", bars_bin)};
  if (out.report.has_funding) files.push_back(stored_bin("funding.bin", encode_funding(funding)));
  const std::string reveal = reveal_text(plan);
  files.push_back({"reveal.json", as_bytes(reveal), reveal.size()});
  const KeyPair key = ed25519_keypair(device_seed);
  out.bytes = write_pack(manifest_text(plan, n), files, key, key_id("device", key.public_key));
  out.scenario_id = plan.scenario_id;
  out.digest = to_hex(sha256(out.bytes));
  return out;
}

}  // namespace tradelab
