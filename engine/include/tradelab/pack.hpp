#pragma once

// Scenario packs and private CSV import (spec: add-scenarios-and-gameplay scenario-packs;
// design D1, D4, D5). Everything here reads untrusted bytes: limits are checked before work is
// done, and every failure carries a stable reason code that clients map to text.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tradelab/ed25519.hpp"
#include "tradelab/scenario.hpp"

namespace tradelab {

constexpr std::uint32_t kPackSchemaVersion = 1;

struct PackLimits {
  std::size_t max_pack_bytes = std::size_t{16} << 20U;
  std::size_t max_files = 8;
  std::size_t max_manifest_bytes = std::size_t{64} << 10U;
  std::size_t max_decompressed_file = std::size_t{8} << 20U;  // per data file
  std::uint32_t max_bars = 20'000;
};

// ---------------------------------------------------------------- CSV import

enum class TimestampUnit : std::uint8_t { Seconds, Milliseconds };
enum class VolumeUnit : std::uint8_t { Base, Quote };

// What the player declares about the file (spec "Private CSV import").
struct CsvMeta {
  std::string asset;
  std::string pair;
  std::string venue;
  VolumeUnit volume_unit = VolumeUnit::Base;
  std::uint32_t interval_seconds = 3600;
  TimestampUnit timestamp_unit = TimestampUnit::Seconds;
};

struct CsvRow {
  std::int64_t time = 0;  // UTC seconds
  Price open, high, low, close;
  Qty volume;                // base units (quote volume is converted with the close)
  Rate funding;              // per funding boundary; carried forward when the cell is empty
};

struct CsvIssue {
  std::uint32_t line = 0;  // 1-based line in the file; 0 = whole file
  std::string code;        // stable: see csv_import.cpp
};

struct CsvGap {
  std::uint32_t line = 0;  // first line after the gap
  std::int64_t from = 0;   // last timestamp before the gap (UTC seconds)
  std::int64_t to = 0;     // first timestamp after it
  std::uint32_t missing = 0;
};

struct CsvReport {
  bool ok = false;
  std::vector<CsvIssue> errors;  // first kCsvMaxListed, in file order
  std::uint32_t error_count = 0;
  std::vector<CsvGap> gaps;      // reported, never filled (first kCsvMaxListed)
  std::uint32_t gap_count = 0;
  std::uint32_t rows = 0;
  bool has_funding = false;
  std::int64_t first_time = 0;
  std::int64_t last_time = 0;
};

constexpr std::size_t kCsvMaxListed = 50;
constexpr std::size_t kCsvMaxBytes = std::size_t{4} << 20U;
constexpr std::uint32_t kCsvMinRows = 50;

// Parses and validates; `rows` receives the parsed rows only when the report is ok.
CsvReport import_csv(std::string_view text, const CsvMeta& meta, std::vector<CsvRow>* rows);

// ---------------------------------------------------------------- masking (design D4)

// Rebases prices so the open of bar `reference` (the first bar of the playable window, visible at
// the first decision point) becomes 100: every OHLC value is multiplied by 100 / reference open
// and rounded half-even to `tick`. Volume is multiplied by the inverse factor so notional volume
// and capacity are unchanged. Fails (false) when a masked price would round to zero or overflow.
bool mask_bars(std::span<const Bar> in, std::uint32_t reference, Price tick, std::vector<Bar>* out);

// ---------------------------------------------------------------- packs

struct TrustedKey {
  std::string id;
  PublicKey key{};
};

enum class PackIntent : std::uint8_t { Install, Publish };

// Shown only in the debrief (spec "Reveal metadata").
struct PackReveal {
  std::string asset, pair, venue, period_from, period_to, source, transformation;
};

struct PackInfo {
  std::string scenario_id;
  std::string mode;  // historical | synthetic | branch
  bool private_pack = false;
  std::string timeframe;
  std::uint32_t step_seconds = 0;
  std::string signing_key_id;
  std::string digest;  // SHA-256 of the whole pack (cache key)
  std::uint32_t gaps = 0;
  PackReveal reveal;
};

struct PackOpen {
  std::string reason;  // empty when ok; stable code otherwise
  std::string detail;  // e.g. the missing field or the offending file
  PackInfo info;
  Scenario scenario;   // valid only when ok
  [[nodiscard]] bool ok() const { return reason.empty(); }
};

// Verifies container, limits, manifest schema, signature (against `trusted`), engine
// compatibility, file sizes and hashes, decompression and data, then builds the scenario.
// Publish intent additionally requires a complete license record (LIC01) and a non-private pack.
PackOpen open_pack(std::span<const std::uint8_t> bytes, std::span<const TrustedKey> trusted, PackIntent intent,
                   const PackLimits& limits = {});

// Mission rules for a private practice pack built from a CSV import.
struct ImportRules {
  std::int32_t leverage_cap = 5;
  std::uint32_t warmup_bars = 0;  // 0 = automatic (a quarter of the rows, 20..60)
};

struct PackBuild {
  std::string reason;  // empty when ok
  CsvReport report;
  std::vector<std::uint8_t> bytes;
  std::string scenario_id;
  std::string digest;
  [[nodiscard]] bool ok() const { return reason.empty(); }
};

// CSV -> validated, masked, private pack signed with the device key (spec: imported packs are
// private and labeled as known scenarios).
PackBuild build_csv_pack(std::string_view csv, const CsvMeta& meta, const ImportRules& rules, const SecretSeed& device_seed);

// Lower level, used by build_csv_pack, the packer and tests: signs a manifest (canonical JSON of
// `manifest_json`, with the files' sizes and hashes filled in) and writes the archive.
struct PackFile {
  std::string name;
  std::vector<std::uint8_t> stored;  // bytes as stored (zstd frame for .bin files)
  std::uint64_t decompressed_size = 0;
};
std::vector<std::uint8_t> write_pack(std::string_view manifest_json, const std::vector<PackFile>& files, const KeyPair& key,
                                     const std::string& key_id);

// 64 lowercase hex digits -> 32 bytes (keys and seeds crossing the C ABI).
std::optional<std::array<std::uint8_t, 32>> parse_hex32(std::string_view hex);

// Columnar encodings of bars.bin / funding.bin (before zstd).
std::vector<std::uint8_t> encode_bars(std::span<const Bar> bars);
std::vector<std::uint8_t> encode_funding(std::span<const Rate> rates);

}  // namespace tradelab
