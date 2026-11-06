// Installed scenario packs (spec desktop-client "Pack installation through the engine"; port of the web's
// installPack / importCsv / PackLibrary). Verify-then-install: only bytes the engine accepted against the
// pinned publisher keys (resources/packs/pinned_keys.json, generated from the web's packs.ts) plus this
// device's key are written, atomically, to <dir>/<digest>.tlpack, and only then indexed in SQLite.
//
// Threading: the engine's run-less calls (tl_pack_validate, tl_csv_*, tl_device_key, tl_blob_*) share one
// static buffer, so a PackLibrary is used on the simulation worker only (EngineHost::post_storage_job).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "model/json.hpp"
#include "model/snapshot.hpp"
#include "model/storage/run_store.hpp"

namespace tradelab::desktop::packs {

// Same limits as the engine (PackLimits::max_pack_bytes, kCsvMaxBytes) and the web (packs.ts).
constexpr std::size_t kPackMaxBytes = 16U * 1024U * 1024U;
constexpr std::size_t kCsvMaxBytes = 4U * 1024U * 1024U;

struct PackCheck {
  bool ok = false;
  std::string reason;  // engine reason code, or: too_large, digest_mismatch, storage_failed
  std::string detail;
  std::optional<storage::StoredPack> entry;
  std::vector<PackText> texts;  // a schema-v2 pack's own mission texts (empty without them)
};

// The "texts" of a tl_pack_validate summary ({"locale","title","briefing"} per locale).
std::vector<PackText> pack_texts_from_summary(const json::Value& pack);

struct CsvImportResult {
  bool ok = false;
  std::string reason;
  std::string report_json;  // the engine's CSV report ({"ok","rows","errors",...})
  std::string digest;
};

class PackLibrary {
 public:
  PackLibrary(storage::RunStore& store, std::filesystem::path dir);

  [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }
  [[nodiscard]] std::filesystem::path pack_path(const std::string& digest) const;
  [[nodiscard]] std::filesystem::path part_path(const std::string& digest) const;

  // [{"id","key"}]: the pinned publisher keys and this device's key.
  [[nodiscard]] std::string trusted_keys_json();
  [[nodiscard]] const std::string& device_seed();

  PackCheck install(std::span<const std::uint8_t> bytes, const std::string& label, const std::string& origin);
  // The mission texts of a pack's bytes (verified by the engine again); empty without them.
  [[nodiscard]] std::vector<PackText> texts(std::span<const std::uint8_t> bytes);
  // The pack's bytes; nullopt when it is not indexed, its file is gone or has another size.
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> bytes(const std::string& digest);
  // Start-up check: indexed packs whose file is missing or truncated (shown as missing, never played).
  [[nodiscard]] std::vector<std::string> missing();
  void remove(const std::string& digest);

  // The engine's CSV report for `meta_json` ({"asset","pair","venue","volumeUnit","intervalSeconds",...}).
  [[nodiscard]] std::string validate_csv(const std::string& csv, const std::string& meta_json);
  // CSV -> private pack signed by this device -> verified and installed like any pack.
  CsvImportResult import_csv(const std::string& csv, const std::string& meta_json, const std::string& label);

 private:
  storage::RunStore& store_;
  std::filesystem::path dir_;
  std::string device_seed_;
  std::string device_key_entry_;
};

std::string random_hex(std::size_t bytes);

}  // namespace tradelab::desktop::packs
