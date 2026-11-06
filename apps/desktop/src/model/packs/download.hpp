// Resumable pack downloads (port of apps/web/src/lib/game/download.ts; design "Storage" > Packs).
//
// A pack is fetched in 64 KB byte ranges; each chunk is appended (fsync) to <packs>/<digest>.part and only
// then the offset is committed to SQLite (`downloads`), so a lost connection, a quit or a crash keeps what
// arrived and the next attempt asks only for the missing bytes. A server that ignores Range (200 with the
// whole file) is accepted too. The complete file must match the catalog's size and SHA-256 before the
// engine verifies it (PackLibrary::install); a size or hash mismatch drops the partial data, which would
// otherwise fail forever.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "model/packs/catalog.hpp"
#include "model/packs/http.hpp"
#include "model/packs/pack_library.hpp"

namespace tradelab::desktop::packs {

constexpr std::int64_t kChunkBytes = 64 * 1024;

enum class DownloadFailure : std::uint8_t {
  None,
  Interrupted,   // no response or a broken one: partial data kept
  Http,          // an HTTP error status: partial data kept
  Aborted,       // the progress callback asked to stop: partial data kept
  SizeMismatch,  // the server's bytes do not fit the catalog's size: partial data dropped
  HashMismatch,  // the whole file's SHA-256 is not the catalog's: partial data dropped
  Storage,       // the .part file or the database could not be written
  Refused,       // complete and hashed, but the engine refused it (PackCheck has the reason)
};

std::string_view download_failure_name(DownloadFailure f);

struct DownloadResult {
  DownloadFailure failure = DownloadFailure::None;
  std::int64_t received = 0;  // bytes kept for a later attempt
  PackCheck check;            // the install outcome when the download completed
};

// Called after each committed chunk; return false to stop (kept as partial).
using DownloadProgress = std::function<bool(std::int64_t received, std::int64_t size)>;

class PackDownloader {
 public:
  PackDownloader(PackLibrary& library, storage::RunStore& store, HttpClient& http, std::int64_t chunk_bytes = kChunkBytes)
      : library_(library), store_(store), http_(http), chunk_bytes_(chunk_bytes) {}

  // Downloads (or resumes) the entry's pack, then verifies and installs it (origin "catalog").
  DownloadResult install(const CatalogEntry& entry, const std::string& label, const DownloadProgress& progress = {});

  // Bytes already committed for `digest` (0 when none).
  [[nodiscard]] std::int64_t received(const std::string& digest);

  // The catalog manifest at `url`, cached in meta for offline reading; nullopt when neither is available.
  std::optional<Catalog> fetch_catalog(const std::string& url);

 private:
  std::int64_t prepare(const CatalogEntry& entry);
  DownloadFailure step(const CatalogEntry& entry, std::int64_t& received);
  DownloadFailure store_whole(const CatalogEntry& entry, const std::vector<std::uint8_t>& body, std::int64_t& received);
  DownloadFailure store_chunk(const CatalogEntry& entry, const HttpResponse& r, std::int64_t& received);
  void drop_partial(const std::string& digest);
  DownloadResult finish(const CatalogEntry& entry, const std::string& label);

  PackLibrary& library_;
  storage::RunStore& store_;
  HttpClient& http_;
  std::int64_t chunk_bytes_;
};

}  // namespace tradelab::desktop::packs
