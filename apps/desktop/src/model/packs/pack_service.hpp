// The catalog and CSV-import screens' access to packs (port of the web's catalog.svelte.ts and the
// session's installPack / validateCsv / importCsv). Every job runs on the simulation worker
// (EngineHost::post_storage_job: the engine's run-less pack calls share one buffer) and reports back on
// the UI thread through `Post` as a PackEvent, so the viewmodel never blocks.
//
// Downloads go in 64 KB ranges (PackDownloader): each committed chunk is reported, pause() stops after
// the current chunk and keeps what arrived, and a later download() resumes from the committed offset,
// also after a restart. A complete file is checked (size, SHA-256) and verified by the engine before it
// is installed (verify-then-install).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "model/packs/catalog.hpp"
#include "model/packs/http.hpp"
#include "model/storage/run_store.hpp"

namespace tradelab::desktop {
class EngineHost;
}  // namespace tradelab::desktop

namespace tradelab::desktop::packs {

// web catalog.svelte.ts CatalogStatus: Ready = fresh from the network; Offline / Unavailable = the last
// catalog seen on this device (maybe none).
enum class CatalogStatus : std::uint8_t { Idle, Loading, Ready, Offline, Unavailable };

enum class DownloadPhase : std::uint8_t { Downloading, Verifying, Failed, Refused, Installed };

struct DownloadState {
  DownloadPhase phase = DownloadPhase::Downloading;
  std::int64_t received = 0;
  std::int64_t size = 0;
  std::string reason;  // Failed: interrupted | aborted | http | size_mismatch | hash_mismatch | storage;
                       // Refused: the engine's pack reason
  bool operator==(const DownloadState&) const = default;
};

struct PackEvent {
  enum class Kind : std::uint8_t { Library, Catalog, Download, CsvReport, CsvImported };
  Kind kind = Kind::Library;
  // Library: installed packs (oldest first), the ones whose file is gone, bytes kept of unfinished downloads.
  std::vector<storage::StoredPack> packs;
  std::vector<std::string> missing;
  std::map<std::string, std::int64_t> partial;
  // Catalog
  CatalogStatus status = CatalogStatus::Idle;
  std::vector<CatalogEntry> entries;
  // Download
  std::string digest;
  DownloadState download;
  // CsvReport: the engine's report JSON; CsvImported: ok / reason (and the new pack's digest)
  std::string report_json;
  bool ok = false;
  std::string reason;
};

// What viewmodels ask (PackService in the app, fakes in tests).
class PackCommands {
 public:
  virtual ~PackCommands() = default;
  virtual void refresh() = 0;
  virtual void load_catalog() = 0;
  virtual void download(const CatalogEntry& entry, const std::string& label) = 0;
  virtual void pause(const std::string& digest) = 0;
  virtual void remove(const std::string& digest) = 0;
  virtual void validate_csv(const std::string& csv, const std::string& meta_json) = 0;
  virtual void import_csv(const std::string& csv, const std::string& meta_json, const std::string& label) = 0;
};

class PackService final : public PackCommands {
 public:
  using Task = std::function<void()>;
  using Post = std::function<void(Task)>;
  using Sink = std::function<void(const PackEvent&)>;

  // `http` may be null (no libcurl): the catalog reads "unavailable", imports and installed packs work.
  PackService(EngineHost& host, std::unique_ptr<HttpClient> http, std::string catalog_url, Post post, Sink sink);
  ~PackService() override;
  PackService(const PackService&) = delete;
  PackService& operator=(const PackService&) = delete;

  void refresh() override;
  void load_catalog() override;
  void download(const CatalogEntry& entry, const std::string& label) override;
  void pause(const std::string& digest) override;
  void remove(const std::string& digest) override;
  void validate_csv(const std::string& csv, const std::string& meta_json) override;
  void import_csv(const std::string& csv, const std::string& meta_json, const std::string& label) override;
  // Asks every running download to stop after its current chunk (shutdown: before the worker is joined).
  void stop_downloads();

 private:
  // Shared with posted tasks: a report that arrives after the service is gone does nothing.
  struct Shared {
    std::mutex mu;
    bool closed = false;
    Sink sink;
  };
  void report(PackEvent e) const;
  std::shared_ptr<std::atomic<bool>> stop_flag(const std::string& digest);

  EngineHost& host_;
  std::shared_ptr<HttpClient> http_;
  std::string catalog_url_;
  Post post_;
  std::shared_ptr<Shared> shared_;
  std::mutex flags_mu_;
  std::map<std::string, std::shared_ptr<std::atomic<bool>>> stop_;
};

// The library state (installed, missing, partial) read on the worker.
PackEvent library_event(storage::RunStore& store, const std::vector<std::string>& missing);

}  // namespace tradelab::desktop::packs
