// State of the catalog and CSV-import screens (web Catalog.svelte + catalog.svelte.ts, CsvImport.svelte +
// csvImport.svelte.ts) and the pure helpers ported from catalog.ts and packs.ts. AppViewModel keeps one
// PackScreens; viewmodel/packs.cpp changes it, screens/catalog.cpp and screens/import.cpp show it.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/i18n.hpp"
#include "model/json.hpp"
#include "model/packs/pack_service.hpp"

namespace tradelab::desktop {

// ---- catalog.ts ----

enum class EntryStatus : std::uint8_t { Installed, Partial, None };

// "1,2 MB" / "75 KB" in the locale (catalog.ts fmtBytes).
std::string fmt_bytes(std::int64_t n, Locale locale);

// ---- packs.ts ----

struct IntervalChoice {
  int seconds;
  std::string_view label;
};
inline constexpr IntervalChoice kIntervals[] = {{60, "1m"}, {300, "5m"}, {900, "15m"}, {3600, "1h"}, {14400, "4h"}, {86400, "1d"}};
inline constexpr std::string_view kGapFills[] = {"none", "skip", "carryForward"};  // label keys csv.gapFill.<value>

// The CSV's declared metadata (engine CsvMeta).
struct CsvMeta {
  std::string asset;
  std::string pair;
  std::string venue;
  std::string volume_unit = "base";  // base | quote
  int interval_seconds = 3600;
  std::string timestamp_unit = "s";  // s | ms
  int leverage_cap = 5;
  std::string gap_fill = "none";
  bool operator==(const CsvMeta&) const = default;
};

std::string interval_label(int seconds);
// Timestamp unit and interval guessed from the first two data rows (packs.ts guessMeta).
CsvMeta guess_meta(std::string_view csv, const CsvMeta& base);
// "<pair or asset or file name> · <interval>", at most 60 characters (packs.ts defaultLabel).
std::string default_label(const CsvMeta& meta, const std::string& file_name);
std::string csv_meta_json(const CsvMeta& meta);
// "2024-01-01T00:00:00Z" for seconds since the epoch (packs.ts isoUtc).
std::string iso_utc(std::int64_t seconds);

// ---- Screen state ----

struct PackScreens {
  // Library (installed packs, kept for every screen: pack runs are titled by their label).
  std::vector<storage::StoredPack> installed;
  std::vector<std::string> missing;
  std::map<std::string, std::int64_t> partial;

  // Catalog
  packs::CatalogStatus status = packs::CatalogStatus::Idle;
  std::vector<packs::CatalogEntry> entries;
  std::map<std::string, packs::DownloadState> downloads;
  int selected = 0;  // catalog entry
  int library_selected = 0;

  // CSV import wizard: file -> meta -> report
  enum class Step : std::uint8_t { File, Meta, Report };
  Step step = Step::File;
  std::string path;
  std::string file_name;
  std::string csv;
  CsvMeta meta;
  std::string label;
  std::optional<json::Value> report;  // the engine's CsvReport
  std::string error;
  bool busy = false;

  [[nodiscard]] EntryStatus status_of(const packs::CatalogEntry& e) const;
  [[nodiscard]] bool busy_with(const std::string& digest) const;
  [[nodiscard]] const storage::StoredPack* pack(const std::string& digest) const;
  // The installed pack playing `scenario_id` (pack runs store the scenario id as their mission id).
  [[nodiscard]] const storage::StoredPack* pack_of_scenario(const std::string& scenario_id) const;
  [[nodiscard]] bool is_missing(const std::string& digest) const;
};

}  // namespace tradelab::desktop
