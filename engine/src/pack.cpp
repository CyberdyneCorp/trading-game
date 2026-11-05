// Pack validation and loading (spec scenario-packs "Integrity and compatibility", "Pack safety
// limits", "Licensing record"; design D1). Checks run cheapest first and stop at the first
// failure; no scenario is produced from a pack that failed any of them.
//
// Reason codes: too_large, malformed_container, too_many_files, bad_file_name, duplicate_file,
// missing_file, unlisted_file, unknown_file, unsupported_feature, manifest_too_large,
// manifest_invalid, missing_field, invalid_field, unsupported_schema, untrusted_key,
// bad_signature, incompatible_engine, too_many_bars, size_mismatch, hash_mismatch,
// decompression_limit, malformed_data, invalid_scenario, private_pack, license_incomplete.

#include <algorithm>
#include <array>
#include <optional>

#include "json_in.hpp"
#include "pack_codec.hpp"
#include "pack_manifest.hpp"
#include "tradelab/engine.hpp"
#include "tradelab/pack.hpp"
#include "tradelab/sha256.hpp"

namespace tradelab {
namespace {

using detail::JsonValue;
using detail::Manifest;
using detail::TarEntry;

constexpr std::string_view kManifestName = "manifest.json";
constexpr std::string_view kSignatureName = "manifest.sig";
constexpr std::array<std::string_view, 3> kDataFiles = {"bars.bin", "funding.bin", "reveal.json"};
constexpr std::array<std::string_view, 4> kReservedFiles = {"events.bin", "options.bin", "mission.json", "i18n.json"};
constexpr std::size_t kColumnHeader = 16;
constexpr std::uint32_t kBarColumns = 5;

bool listed_in(std::string_view name, std::span<const std::string_view> names) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (unsigned k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8U * k)));
}

void put_i64(std::vector<std::uint8_t>& out, std::int64_t v) {
  const auto u = static_cast<std::uint64_t>(v);
  for (unsigned k = 0; k < 8; ++k) out.push_back(static_cast<std::uint8_t>(u >> (8U * k)));
}

std::uint64_t get_le(std::span<const std::uint8_t> s, std::size_t at, unsigned width) {
  std::uint64_t v = 0;
  for (unsigned k = 0; k < width; ++k) v |= static_cast<std::uint64_t>(s[at + k]) << (8U * k);
  return v;
}

std::vector<std::uint8_t> column_header(std::string_view magic, std::uint32_t rows, std::uint32_t columns) {
  std::vector<std::uint8_t> out(magic.begin(), magic.end());
  put_u32(out, rows);
  put_u32(out, columns);
  put_u32(out, 0);
  return out;
}

// Columnar little-endian int64: header (magic, rows, columns, 0) then each column in turn.
bool decode_columns(std::span<const std::uint8_t> raw, std::string_view magic, std::uint32_t rows, std::uint32_t columns,
                    std::vector<std::int64_t>* out) {
  const std::size_t expected = kColumnHeader + std::size_t{8} * rows * columns;
  if (raw.size() != expected || !std::equal(magic.begin(), magic.end(), raw.begin())) return false;
  if (get_le(raw, 4, 4) != rows || get_le(raw, 8, 4) != columns || get_le(raw, 12, 4) != 0) return false;
  out->resize(std::size_t{rows} * columns);
  for (std::size_t i = 0; i < out->size(); ++i) (*out)[i] = static_cast<std::int64_t>(get_le(raw, kColumnHeader + 8 * i, 8));
  return true;
}

class Opener {
 public:
  Opener(std::span<const std::uint8_t> bytes, std::span<const TrustedKey> trusted, PackIntent intent, const PackLimits& lim)
      : bytes_(bytes), trusted_(trusted), intent_(intent), lim_(lim) {}

  PackOpen run() {
    const bool ok = container() && manifest() && signature() && compatible() && files() && data() && scenario() && publishable();
    if (!ok) out_.scenario = Scenario{};
    return std::move(out_);
  }

 private:
  bool fail(std::string reason, std::string detail = {}) {
    out_.reason = std::move(reason);
    out_.detail = std::move(detail);
    return false;
  }

  [[nodiscard]] const TarEntry* entry(std::string_view name) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [name](const TarEntry& e) { return e.name == name; });
    return it == entries_.end() ? nullptr : &*it;
  }

  bool container() {
    if (bytes_.size() > lim_.max_pack_bytes) return fail("too_large");
    switch (detail::tar_read(bytes_, lim_.max_files, &entries_)) {
      case detail::TarStatus::Ok: break;
      case detail::TarStatus::TooManyFiles: return fail("too_many_files");
      case detail::TarStatus::BadName: return fail("bad_file_name");
      case detail::TarStatus::Duplicate: return fail("duplicate_file");
      case detail::TarStatus::Malformed: return fail("malformed_container");
    }
    out_.info.digest = to_hex(sha256(bytes_));
    if (entry(kManifestName) == nullptr) return fail("missing_file", std::string(kManifestName));
    if (entry(kSignatureName) == nullptr) return fail("missing_file", std::string(kSignatureName));
    return true;
  }

  bool manifest() {
    const TarEntry* e = entry(kManifestName);
    if (e->data.size() > lim_.max_manifest_bytes) return fail("manifest_too_large");
    const std::string_view text(reinterpret_cast<const char*>(e->data.data()), e->data.size());
    detail::JsonLimits jl;
    jl.max_bytes = lim_.max_manifest_bytes;
    auto parsed = detail::parse_json(text, jl);
    if (!parsed.ok) return fail("manifest_invalid", parsed.error);
    root_ = std::move(parsed.value);
    detail::ManifestError err;
    if (!detail::parse_manifest(root_, &m_, &err)) return fail(err.reason, err.field);
    return true;
  }

  bool signature() {
    const auto key = std::find_if(trusted_.begin(), trusted_.end(), [this](const TrustedKey& k) { return k.id == m_.signing_key_id; });
    if (key == trusted_.end()) return fail("untrusted_key", m_.signing_key_id);
    const TarEntry* sig = entry(kSignatureName);
    if (sig->data.size() != Signature{}.size()) return fail("bad_signature");
    Signature s{};
    std::copy(sig->data.begin(), sig->data.end(), s.begin());
    const std::string canonical = detail::canonical_json(root_);
    const std::span<const std::uint8_t> msg(reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size());
    if (!ed25519_verify(key->key, msg, s)) return fail("bad_signature");
    out_.info.signing_key_id = m_.signing_key_id;
    return true;
  }

  bool compatible() {
    if (m_.schema_version != kPackSchemaVersion) return fail("unsupported_schema");
    std::array<std::uint32_t, 3> lo{};
    std::array<std::uint32_t, 3> hi{};
    std::array<std::uint32_t, 3> self{};
    if (!detail::parse_semver(m_.engine_min, &lo)) return fail("invalid_field", "engineCompatibility.min");
    if (!detail::parse_semver(m_.engine_max, &hi)) return fail("invalid_field", "engineCompatibility.max");
    (void)detail::parse_semver(kEngineVersion, &self);
    const bool in_range = lo <= self && self <= hi && m_.execution_policy <= kExecutionPolicyVersion;
    return in_range || fail("incompatible_engine", m_.engine_min + ".." + m_.engine_max);
  }

  bool file_ok(const detail::ManifestFile& f) {
    if (listed_in(f.name, kReservedFiles)) return fail("unsupported_feature", f.name);
    if (!listed_in(f.name, kDataFiles)) return fail("unknown_file", f.name);
    const TarEntry* e = entry(f.name);
    if (e == nullptr) return fail("missing_file", f.name);
    if (e->data.size() != f.size) return fail("size_mismatch", f.name);
    if (to_hex(sha256(e->data)) != f.sha256) return fail("hash_mismatch", f.name);  // DATA02
    return true;
  }

  bool files() {
    for (const auto& f : m_.files) {
      if (!file_ok(f)) return false;
    }
    for (const TarEntry& e : entries_) {
      const bool meta = e.name == kManifestName || e.name == kSignatureName;
      const bool listed = std::any_of(m_.files.begin(), m_.files.end(), [&e](const auto& f) { return f.name == e.name; });
      if (!meta && !listed) return fail("unlisted_file", e.name);
    }
    if (entry("bars.bin") == nullptr) return fail("missing_file", "bars.bin");
    if (m_.funding_source == "file" && entry("funding.bin") == nullptr) return fail("missing_file", "funding.bin");
    return true;
  }

  [[nodiscard]] const detail::ManifestFile& listed(std::string_view name) const {
    return *std::find_if(m_.files.begin(), m_.files.end(), [name](const auto& f) { return f.name == name; });
  }

  bool inflate(std::string_view name, std::vector<std::uint8_t>* out) {
    const detail::ManifestFile& f = listed(name);
    if (f.decompressed_size > lim_.max_decompressed_file) return fail("decompression_limit", std::string(name));
    const auto declared = static_cast<std::size_t>(f.decompressed_size);
    switch (detail::zstd_decompress(entry(name)->data, declared, lim_.max_decompressed_file, out)) {
      case detail::ZstdStatus::Ok: return true;
      case detail::ZstdStatus::TooLarge: return fail("decompression_limit", std::string(name));
      case detail::ZstdStatus::Malformed: break;
    }
    return fail("malformed_data", std::string(name));
  }

  bool bars() {
    std::vector<std::uint8_t> raw;
    std::vector<std::int64_t> cols;
    if (!inflate("bars.bin", &raw)) return false;
    // A declared gap policy adds the continuity column (0 or 1 per bar).
    const bool gaps_filled = !m_.gap_policy.empty();
    const std::uint32_t columns = gaps_filled ? kBarColumns + 1 : kBarColumns;
    if (!decode_columns(raw, "TLB1", m_.bars, columns, &cols)) return fail("malformed_data", "bars.bin");
    const std::size_t n = m_.bars;
    bars_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      const std::int64_t flag = gaps_filled ? cols[5 * n + i] : 0;
      if (flag != 0 && flag != 1) return fail("malformed_data", "bars.bin");
      bars_[i] = Bar{Price::from_raw(cols[i]), Price::from_raw(cols[n + i]), Price::from_raw(cols[2 * n + i]),
                     Price::from_raw(cols[3 * n + i]), Qty::from_raw(cols[4 * n + i]), false, flag == 1};
    }
    return true;
  }

  bool funding() {
    funding_.assign(m_.bars, Rate{});
    if (m_.funding_source != "file") return true;
    std::vector<std::uint8_t> raw;
    std::vector<std::int64_t> col;
    if (!inflate("funding.bin", &raw)) return false;
    if (!decode_columns(raw, "TLF1", m_.bars, 1, &col)) return fail("malformed_data", "funding.bin");
    std::transform(col.begin(), col.end(), funding_.begin(), [](std::int64_t r) { return Rate::from_raw(r); });
    return true;
  }

  bool reveal() {
    const TarEntry* e = entry("reveal.json");
    if (e == nullptr) return true;
    const auto parsed = detail::parse_json({reinterpret_cast<const char*>(e->data.data()), e->data.size()});
    if (!parsed.ok || !parsed.value.is(JsonValue::Kind::Object)) return fail("malformed_data", "reveal.json");
    const auto text = [&parsed](std::string_view key) {
      const JsonValue* v = parsed.value.find(key);
      return v != nullptr && v->is(JsonValue::Kind::String) ? v->text : std::string();
    };
    PackReveal& r = out_.info.reveal;
    r = {text("asset"), text("pair"), text("venue"), text("periodFrom"), text("periodTo"), text("source"), text("transformation")};
    return true;
  }

  bool data() {
    if (m_.bars > lim_.max_bars) return fail("too_many_bars");
    return bars() && funding() && reveal();
  }

  bool scenario() {
    Scenario& sc = out_.scenario;
    sc.id = m_.scenario_id;
    sc.contract = m_.contract;
    sc.costs = m_.costs;
    sc.mission = m_.mission;
    sc.mission.gap_policy = m_.gap_policy == "carryForward" ? GapPolicy::CarryForward : GapPolicy::Skip;
    sc.bars = std::move(bars_);
    sc.funding_rate = std::move(funding_);
    const ScenarioError e = validate(sc);
    if (e != ScenarioError::Ok) return fail("invalid_scenario", std::to_string(static_cast<int>(e)));
    PackInfo& info = out_.info;
    info.scenario_id = m_.scenario_id;
    info.mode = m_.mode;
    info.private_pack = m_.private_pack;
    info.timeframe = m_.timeframe;
    info.step_seconds = m_.step_seconds;
    info.gaps = m_.gaps;
    info.gap_policy = m_.gap_policy;
    return true;
  }

  // LIC01: publishing needs the rights for the intended use recorded.
  bool publishable() {
    if (intent_ != PackIntent::Publish) return true;
    if (m_.private_pack) return fail("private_pack");
    const detail::ManifestLicense& l = m_.license;
    if (m_.downloadable && !l.offline_redistribution) return fail("license_incomplete", "license.offlineRedistribution");
    if (m_.mode == "historical" && !l.transformation) return fail("license_incomplete", "license.transformation");
    if (m_.mode == "historical" && l.attribution.empty()) return fail("license_incomplete", "license.attribution");
    return true;
  }

  std::span<const std::uint8_t> bytes_;
  std::span<const TrustedKey> trusted_;
  PackIntent intent_;
  const PackLimits& lim_;
  PackOpen out_;
  std::vector<TarEntry> entries_;
  JsonValue root_;
  Manifest m_;
  std::vector<Bar> bars_;
  std::vector<Rate> funding_;
};

}  // namespace

std::vector<std::uint8_t> encode_bars(std::span<const Bar> bars) {
  const bool gaps_filled = std::any_of(bars.begin(), bars.end(), [](const Bar& b) { return b.continuity; });
  auto out = column_header("TLB1", static_cast<std::uint32_t>(bars.size()), gaps_filled ? kBarColumns + 1 : kBarColumns);
  for (const Bar& b : bars) put_i64(out, b.open.raw);
  for (const Bar& b : bars) put_i64(out, b.high.raw);
  for (const Bar& b : bars) put_i64(out, b.low.raw);
  for (const Bar& b : bars) put_i64(out, b.close.raw);
  for (const Bar& b : bars) put_i64(out, b.volume.raw);
  if (gaps_filled) {
    for (const Bar& b : bars) put_i64(out, b.continuity ? 1 : 0);
  }
  return out;
}

std::vector<std::uint8_t> encode_funding(std::span<const Rate> rates) {
  auto out = column_header("TLF1", static_cast<std::uint32_t>(rates.size()), 1);
  for (const Rate& r : rates) put_i64(out, r.raw);
  return out;
}

PackOpen open_pack(std::span<const std::uint8_t> bytes, std::span<const TrustedKey> trusted, PackIntent intent,
                   const PackLimits& limits) {
  return Opener(bytes, trusted, intent, limits).run();
}

std::optional<std::array<std::uint8_t, 32>> parse_hex32(std::string_view hex) {
  if (hex.size() != 64) return std::nullopt;
  std::array<std::uint8_t, 32> out{};
  for (std::size_t i = 0; i < 32; ++i) {
    const auto hi = hex[2 * i];
    const auto lo = hex[2 * i + 1];
    const auto nib = [](char c) { return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1; };
    if (nib(hi) < 0 || nib(lo) < 0) return std::nullopt;
    out[i] = static_cast<std::uint8_t>(nib(hi) * 16 + nib(lo));
  }
  return out;
}

}  // namespace tradelab
