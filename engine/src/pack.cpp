// Pack validation and loading (spec scenario-packs "Integrity and compatibility", "Pack safety
// limits", "Licensing record"; design D1). Checks run cheapest first and stop at the first
// failure; no scenario is produced from a pack that failed any of them.
//
// Reason codes: too_large, malformed_container, too_many_files, bad_file_name, duplicate_file,
// missing_file, unlisted_file, unknown_file, unsupported_feature, manifest_too_large,
// manifest_invalid, missing_field, invalid_field, unsupported_schema, untrusted_key,
// bad_signature, incompatible_engine, too_many_bars, too_many_events, too_many_series,
// size_mismatch, hash_mismatch, decompression_limit, malformed_data, invalid_scenario,
// private_pack, license_incomplete.

#include <algorithm>
#include <array>
#include <limits>
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
constexpr std::array<std::string_view, 5> kDataFiles = {"bars.bin", "funding.bin", "events.bin", "options.bin", "reveal.json"};
constexpr std::array<std::string_view, 2> kReservedFiles = {"mission.json", "i18n.json"};
constexpr std::size_t kColumnHeader = 16;
constexpr std::uint32_t kBarColumns = 5;
constexpr std::uint32_t kNewsColumns = 7;       // release_step, kind, tone, credibility, expectation, actual, announce_lead
constexpr std::uint32_t kLiquidityColumns = 4;  // start, bars, spread_pct, capacity_pct
constexpr std::uint32_t kSeriesColumns = 5;     // id, kind, strike, list_step, expiry_step

// Inclusive range of each column of a table, so narrowing to the scenario's types never wraps;
// semantic checks (steps inside the market, kinds, ids) are validate(Scenario)'s.
struct ColumnRange {
  std::int64_t lo;
  std::int64_t hi;
};
constexpr std::int64_t kU32 = 0xFFFF'FFFF;
constexpr std::int64_t kMaxRaw = std::numeric_limits<std::int64_t>::max();
constexpr std::array<ColumnRange, kNewsColumns> kNewsRanges = {{{0, kU32}, {0, 255}, {-1, 1}, {0, 100}, {-2, 2}, {-2, 2}, {0, kU32}}};
constexpr std::array<ColumnRange, kLiquidityColumns> kLiquidityRanges = {{{0, kU32}, {0, kU32}, {0, kU32}, {0, kU32}}};
constexpr std::array<ColumnRange, kSeriesColumns> kSeriesRanges = {{{0, kU32}, {0, 1}, {1, kMaxRaw}, {0, kU32}, {0, kU32}}};
constexpr std::int64_t kMaxIvRaw = std::int64_t{10} * Vol::kScale;  // 1,000% annualized

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

// One columnar little-endian int64 table at `*pos`: header (magic, rows, columns, 0) then each
// column in turn. Advances *pos past the table.
bool decode_table(std::span<const std::uint8_t> raw, std::size_t* pos, std::string_view magic, std::uint32_t rows,
                  std::uint32_t columns, std::vector<std::int64_t>* out) {
  const std::size_t size = kColumnHeader + std::size_t{8} * rows * columns;
  if (raw.size() - *pos < size) return false;
  const auto t = raw.subspan(*pos, size);
  if (!std::equal(magic.begin(), magic.end(), t.begin())) return false;
  if (get_le(t, 4, 4) != rows || get_le(t, 8, 4) != columns || get_le(t, 12, 4) != 0) return false;
  out->resize(std::size_t{rows} * columns);
  for (std::size_t i = 0; i < out->size(); ++i) (*out)[i] = static_cast<std::int64_t>(get_le(t, kColumnHeader + 8 * i, 8));
  *pos += size;
  return true;
}

// A file holding exactly one table.
bool decode_columns(std::span<const std::uint8_t> raw, std::string_view magic, std::uint32_t rows, std::uint32_t columns,
                    std::vector<std::int64_t>* out) {
  std::size_t pos = 0;
  return decode_table(raw, &pos, magic, rows, columns, out) && pos == raw.size();
}

// Every value of each column (n rows) within its range.
template <std::size_t N>
bool in_ranges(const std::vector<std::int64_t>& cols, std::size_t n, const std::array<ColumnRange, N>& ranges) {
  for (std::size_t c = 0; c < N; ++c) {
    const auto first = cols.begin() + static_cast<std::ptrdiff_t>(c * n);
    const ColumnRange r = ranges[c];
    if (!std::all_of(first, first + static_cast<std::ptrdiff_t>(n), [r](std::int64_t v) { return v >= r.lo && v <= r.hi; })) return false;
  }
  return true;
}

void put_table(std::vector<std::uint8_t>& out, std::string_view magic, std::uint32_t rows,
               std::initializer_list<std::vector<std::int64_t>> columns) {
  const auto header = column_header(magic, rows, static_cast<std::uint32_t>(columns.size()));
  out.insert(out.end(), header.begin(), header.end());
  for (const auto& col : columns) {
    for (const std::int64_t v : col) put_i64(out, v);
  }
}

template <typename T, typename F>
std::vector<std::int64_t> column(std::span<const T> rows, F field) {
  std::vector<std::int64_t> out;
  out.reserve(rows.size());
  for (const T& r : rows) out.push_back(static_cast<std::int64_t>(field(r)));
  return out;
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

  bool news_table(std::span<const std::uint8_t> raw, std::size_t* pos) {
    std::vector<std::int64_t> c;
    const std::size_t n = m_.events;
    if (!decode_table(raw, pos, "TLE1", m_.events, kNewsColumns, &c) || !in_ranges(c, n, kNewsRanges)) return false;
    news_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      NewsItem& item = news_[i];
      item.release_step = static_cast<std::uint32_t>(c[i]);
      item.kind = static_cast<NewsKind>(c[n + i]);
      item.tone = static_cast<std::int8_t>(c[2 * n + i]);
      item.credibility = static_cast<std::uint8_t>(c[3 * n + i]);
      item.expectation = static_cast<std::int8_t>(c[4 * n + i]);
      item.actual = static_cast<std::int8_t>(c[5 * n + i]);
      item.announce_lead = static_cast<std::uint32_t>(c[6 * n + i]);
    }
    return true;
  }

  bool liquidity_table(std::span<const std::uint8_t> raw, std::size_t* pos) {
    std::vector<std::int64_t> c;
    const std::size_t n = m_.liquidity_shocks;
    if (!decode_table(raw, pos, "TLQ1", m_.liquidity_shocks, kLiquidityColumns, &c) || !in_ranges(c, n, kLiquidityRanges)) return false;
    liquidity_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      liquidity_[i] = {static_cast<std::uint32_t>(c[i]), static_cast<std::uint32_t>(c[n + i]), static_cast<std::uint32_t>(c[2 * n + i]),
                       static_cast<std::uint32_t>(c[3 * n + i])};
    }
    return true;
  }

  // events.bin: the news table, then the liquidity-shock table.
  bool events() {
    if (entry("events.bin") == nullptr) return (m_.events == 0 && m_.liquidity_shocks == 0) || fail("missing_file", "events.bin");
    std::vector<std::uint8_t> raw;
    if (!inflate("events.bin", &raw)) return false;
    std::size_t pos = 0;
    const bool ok = news_table(raw, &pos) && liquidity_table(raw, &pos) && pos == raw.size();
    return ok || fail("malformed_data", "events.bin");
  }

  bool series_table(std::span<const std::uint8_t> raw, std::size_t* pos) {
    std::vector<std::int64_t> c;
    const std::size_t n = m_.option_series;
    if (!decode_table(raw, pos, "TLO1", m_.option_series, kSeriesColumns, &c) || !in_ranges(c, n, kSeriesRanges)) return false;
    series_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      series_[i] = {static_cast<std::uint32_t>(c[i]), static_cast<OptionKind>(c[n + i]), Price::from_raw(c[2 * n + i]),
                    static_cast<std::uint32_t>(c[3 * n + i]), static_cast<std::uint32_t>(c[4 * n + i])};
    }
    return true;
  }

  bool iv_table(std::span<const std::uint8_t> raw, std::size_t* pos) {
    std::vector<std::int64_t> c;
    const std::uint32_t rows = m_.iv_kind == "file" ? m_.bars : 0;
    if (!decode_table(raw, pos, "TLI1", rows, 1, &c)) return false;
    if (std::any_of(c.begin(), c.end(), [](std::int64_t v) { return v <= 0 || v > kMaxIvRaw; })) return false;
    iv_.resize(rows);
    std::transform(c.begin(), c.end(), iv_.begin(), [](std::int64_t v) { return Vol::from_raw(v); });
    return true;
  }

  // options.bin: the series table, then the IV table (one row per bar with ivModel "file", else empty).
  bool options() {
    const bool present = entry("options.bin") != nullptr;
    if (!m_.has_options) return (!present && m_.iv_kind != "file") || fail("missing_field", "instruments.options");
    if (!present) return fail("missing_file", "options.bin");
    std::vector<std::uint8_t> raw;
    if (!inflate("options.bin", &raw)) return false;
    std::size_t pos = 0;
    const bool ok = series_table(raw, &pos) && iv_table(raw, &pos) && pos == raw.size();
    return ok || fail("malformed_data", "options.bin");
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

  // A synthetic pack is generated throughout; in a branch pack every bar after the branch point is
  // (spec "Branch labeling"); historical bars are not.
  bool label_bars() {
    if (m_.mode == "synthetic") {
      for (Bar& b : bars_) b.synthetic = true;
    }
    if (!m_.branch_step) return true;
    if (*m_.branch_step + 1 >= m_.bars) return fail("invalid_field", "data.branchStep");
    for (std::size_t i = *m_.branch_step + 1; i < bars_.size(); ++i) bars_[i].synthetic = true;
    return true;
  }

  bool data() {
    // Row limits first: nothing is decompressed for a pack that declares too much.
    if (m_.bars > lim_.max_bars) return fail("too_many_bars");
    if (m_.events > lim_.max_events || m_.liquidity_shocks > lim_.max_events) return fail("too_many_events");
    if (m_.option_series > lim_.max_option_series) return fail("too_many_series");
    return bars() && funding() && events() && options() && reveal() && label_bars();
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
    sc.news = std::move(news_);
    sc.liquidity = std::move(liquidity_);
    if (m_.has_options) {
      sc.options = m_.options;
      sc.options.series = std::move(series_);
      sc.options.iv_base = std::move(iv_);
    }
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
  std::vector<NewsItem> news_;
  std::vector<LiquidityShock> liquidity_;
  std::vector<OptionSeries> series_;
  std::vector<Vol> iv_;
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

std::vector<std::uint8_t> encode_events(std::span<const NewsItem> news, std::span<const LiquidityShock> liquidity) {
  std::vector<std::uint8_t> out;
  put_table(out, "TLE1", static_cast<std::uint32_t>(news.size()),
            {column(news, [](const NewsItem& n) { return n.release_step; }),
             column(news, [](const NewsItem& n) { return static_cast<int>(n.kind); }),
             column(news, [](const NewsItem& n) { return n.tone; }), column(news, [](const NewsItem& n) { return n.credibility; }),
             column(news, [](const NewsItem& n) { return n.expectation; }), column(news, [](const NewsItem& n) { return n.actual; }),
             column(news, [](const NewsItem& n) { return n.announce_lead; })});
  put_table(out, "TLQ1", static_cast<std::uint32_t>(liquidity.size()),
            {column(liquidity, [](const LiquidityShock& l) { return l.start; }),
             column(liquidity, [](const LiquidityShock& l) { return l.bars; }),
             column(liquidity, [](const LiquidityShock& l) { return l.spread_pct; }),
             column(liquidity, [](const LiquidityShock& l) { return l.capacity_pct; })});
  return out;
}

std::vector<std::uint8_t> encode_options(const OptionSpec& options) {
  const std::span<const OptionSeries> s = options.series;
  std::vector<std::uint8_t> out;
  put_table(out, "TLO1", static_cast<std::uint32_t>(s.size()),
            {column(s, [](const OptionSeries& x) { return x.id; }),
             column(s, [](const OptionSeries& x) { return static_cast<int>(x.kind); }),
             column(s, [](const OptionSeries& x) { return x.strike.raw; }), column(s, [](const OptionSeries& x) { return x.list_step; }),
             column(s, [](const OptionSeries& x) { return x.expiry_step; })});
  const std::span<const Vol> iv = options.iv_base;
  put_table(out, "TLI1", static_cast<std::uint32_t>(iv.size()), {column(iv, [](const Vol& v) { return v.raw; })});
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
