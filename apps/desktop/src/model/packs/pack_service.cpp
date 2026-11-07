#include "model/packs/pack_service.hpp"

#include <memory>
#include <optional>
#include <utility>

#include "model/engine_host.hpp"
#include "model/packs/download.hpp"

namespace tradelab::desktop::packs {

namespace {

// The web's DownloadView after a stopped or failed attempt.
DownloadState failed_state(const DownloadResult& r, std::int64_t size) {
  if (r.failure == DownloadFailure::Refused) return {DownloadPhase::Refused, size, size, r.check.reason};
  return {DownloadPhase::Failed, r.received, size, std::string(download_failure_name(r.failure))};
}

}  // namespace

PackEvent library_event(storage::RunStore& store, const std::vector<std::string>& missing) {
  PackEvent e;
  e.kind = PackEvent::Kind::Library;
  try {
    e.packs = store.packs();
    for (const storage::Download& d : store.downloads()) e.partial[d.digest] = d.received;
  } catch (const storage::Error&) {  // an unreadable index lists nothing; the packs stay on disk
  }
  e.missing = missing;
  return e;
}

PackService::PackService(EngineHost& host, std::unique_ptr<HttpClient> http, std::string catalog_url, Post post,
                         Sink sink)
    : host_(host),
      http_(std::move(http)),
      catalog_url_(std::move(catalog_url)),
      post_(std::move(post)),
      shared_(std::make_shared<Shared>()) {
  shared_->sink = std::move(sink);
}

PackService::~PackService() {
  stop_downloads();
  std::lock_guard lock(shared_->mu);
  shared_->closed = true;
}

void PackService::stop_downloads() {
  {
    std::lock_guard lock(flags_mu_);
    for (auto& [digest, flag] : stop_) flag->store(true);
  }
  fetches_.stop();
}

void PackService::report(PackEvent e) const {
  post_([shared = shared_, e = std::move(e)] {
    Sink sink;
    {
      std::lock_guard lock(shared->mu);
      if (shared->closed) return;
      sink = shared->sink;
    }
    if (sink) sink(e);
  });
}

std::shared_ptr<std::atomic<bool>> PackService::stop_flag(const std::string& digest) {
  std::lock_guard lock(flags_mu_);
  auto& flag = stop_[digest];
  flag = std::make_shared<std::atomic<bool>>(false);
  return flag;
}

void PackService::refresh() {
  host_.post_storage_job([this](storage::Storage& s) { report(library_event(s.store(), s.packs().missing())); });
}

void PackService::load_catalog() {
  PackEvent loading;
  loading.kind = PackEvent::Kind::Catalog;
  loading.status = CatalogStatus::Loading;
  report(loading);
  if (!http_) {
    PackEvent e;
    e.kind = PackEvent::Kind::Catalog;
    e.status = CatalogStatus::Unavailable;
    return report(std::move(e));
  }
  // The network off the worker, then the cache (or the offline copy) on it, where the store lives.
  fetches_.post([this] {
    auto response = std::make_shared<const HttpResponse>(catalog_url_.empty() ? HttpResponse{} : http_->get(catalog_url_, std::nullopt));
    host_.post_storage_job([this, response](storage::Storage& s) {
      PackDownloader::CatalogRead read = PackDownloader::read_catalog(*response, catalog_url_, s.store());
      PackEvent e;
      e.kind = PackEvent::Kind::Catalog;
      if (read.catalog) e.entries = std::move(read.catalog->entries);
      e.status = read.fresh ? CatalogStatus::Ready : read.offline ? CatalogStatus::Offline : CatalogStatus::Unavailable;
      report(std::move(e));
      report(library_event(s.store(), s.packs().missing()));
    });
  });
}

void PackService::download(const CatalogEntry& entry, const std::string& label) {
  if (!http_) return;
  auto stop = stop_flag(entry.digest);
  host_.post_storage_job([this, entry, label, stop](storage::Storage& s) {
    PackEvent e;
    e.kind = PackEvent::Kind::Download;
    e.digest = entry.digest;
    PackDownloader downloader(s.packs(), s.store(), *http_);
    e.download = {DownloadPhase::Downloading, downloader.received(entry.digest), entry.size, {}};
    report(e);
    const auto progress = [&](std::int64_t received, std::int64_t size) {
      e.download = {received >= size ? DownloadPhase::Verifying : DownloadPhase::Downloading, received, size, {}};
      report(e);
      return !stop->load();
    };
    const DownloadResult r = downloader.install(entry, label, progress);
    e.download = r.failure == DownloadFailure::None ? DownloadState{DownloadPhase::Installed, entry.size, entry.size, {}}
                                                    : failed_state(r, entry.size);
    report(e);
    report(library_event(s.store(), s.packs().missing()));
  });
}

void PackService::pause(const std::string& digest) {
  std::lock_guard lock(flags_mu_);
  if (auto it = stop_.find(digest); it != stop_.end()) it->second->store(true);
}

void PackService::remove(const std::string& digest) {
  host_.post_storage_job([this, digest](storage::Storage& s) {
    s.packs().remove(digest);
    report(library_event(s.store(), s.packs().missing()));
  });
}

void PackService::validate_csv(const std::string& csv, const std::string& meta_json) {
  host_.post_storage_job([this, csv, meta_json](storage::Storage& s) {
    PackEvent e;
    e.kind = PackEvent::Kind::CsvReport;
    e.report_json = s.packs().validate_csv(csv, meta_json);
    report(std::move(e));
  });
}

void PackService::import_csv(const std::string& csv, const std::string& meta_json, const std::string& label) {
  host_.post_storage_job([this, csv, meta_json, label](storage::Storage& s) {
    const CsvImportResult r = s.packs().import_csv(csv, meta_json, label);
    PackEvent e;
    e.kind = PackEvent::Kind::CsvImported;
    e.ok = r.ok;
    e.reason = r.reason;
    e.digest = r.digest;
    e.report_json = r.report_json;
    report(std::move(e));
    report(library_event(s.store(), s.packs().missing()));
  });
}

}  // namespace tradelab::desktop::packs
