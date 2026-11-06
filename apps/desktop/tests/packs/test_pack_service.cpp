// PackService (the catalog and CSV-import screens' model side): jobs run on the EngineHost worker and come
// back as PackEvents on the "UI thread" (a UiQueue). The web's sample catalog served by a fake HTTP
// client: catalog states, a download paused after a chunk and resumed from the committed offset, then
// verified and installed; CSV validation and import.
#include <doctest/doctest.h>

#include <atomic>
#include <functional>
#include <mutex>

#include "model/engine_host.hpp"
#include "model/packs/download.hpp"
#include "model/packs/files.hpp"
#include "model/packs/pack_service.hpp"
#include "model/storage/storage.hpp"
#include "support/storage_fixtures.hpp"
#include "support/ui_queue.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::packs;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::UiQueue;
using tradelab::desktop::testing::web_catalog_dir;

namespace {

constexpr const char* kCatalogUrl = "https://cdn.test/catalog/catalog.json";
constexpr const char* kSampleA = "ec9140b18db6c1997d9ebefa494be5d040fc91052236045d2a6336d2263602ee";

std::vector<std::uint8_t> file_bytes(const std::filesystem::path& p) {
  auto b = read_file(p);
  REQUIRE(b);
  return *b;
}

// The catalog and its packs by URL, with Range; `offline` answers nothing, `status` an HTTP error, and
// `on_range` runs on the worker before each range is served.
class CatalogHttp final : public HttpClient {
 public:
  bool offline = false;
  int status = 0;
  std::function<void(std::int64_t first)> on_range;
  std::vector<std::int64_t> offsets;

  HttpResponse get(const std::string& url, const std::optional<ByteRange>& range) override {
    HttpResponse r;
    if (offline) return r;
    if (status != 0) {
      r.status = status;
      return r;
    }
    if (url == kCatalogUrl) {
      r.status = 200;
      r.body = file_bytes(web_catalog_dir() / "catalog.json");
      return r;
    }
    const std::string name = url.substr(url.rfind('/') + 1);
    const auto body = file_bytes(web_catalog_dir() / "packs" / name);
    const std::int64_t first = range ? range->first : 0;
    {
      std::lock_guard lock(mu_);
      offsets.push_back(first);
    }
    if (on_range) on_range(first);
    const std::int64_t last = std::min<std::int64_t>(range ? range->last : static_cast<std::int64_t>(body.size()) - 1,
                                                     static_cast<std::int64_t>(body.size()) - 1);
    r.status = 206;
    r.content_range = "bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(body.size());
    r.body.assign(body.begin() + first, body.begin() + last + 1);
    return r;
  }

 private:
  std::mutex mu_;
};

struct Service {
  UiQueue ui;
  std::vector<PackEvent> events;
  CatalogHttp* http = nullptr;
  std::unique_ptr<EngineHost> host;
  std::unique_ptr<PackService> service;

  Service(const std::filesystem::path& dir, bool with_http = true) {
    host = std::make_unique<EngineHost>([this](EngineHost::Task t) { ui.post(std::move(t)); },
                                        [](std::shared_ptr<const Snapshot>) {}, std::make_unique<storage::Storage>(dir));
    std::unique_ptr<HttpClient> client;
    if (with_http) {
      auto owned = std::make_unique<CatalogHttp>();
      http = owned.get();
      client = std::move(owned);
    }
    service = std::make_unique<PackService>(*host, std::move(client), kCatalogUrl,
                                            [this](PackService::Task t) { ui.post(std::move(t)); },
                                            [this](const PackEvent& e) { events.push_back(e); });
  }
  ~Service() {
    service->stop_downloads();
    host.reset();
    service.reset();
  }
  Service(const Service&) = delete;
  Service& operator=(const Service&) = delete;

  // The last event of `kind` once one satisfying `done` arrived.
  const PackEvent& wait(PackEvent::Kind kind, const std::function<bool(const PackEvent&)>& done = {}) {
    const PackEvent* found = nullptr;
    REQUIRE(ui.pump_until([&] {
      for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (it->kind == kind && (!done || done(*it))) {
          found = &*it;
          return true;
        }
      }
      return false;
    }));
    return *found;
  }
  const CatalogEntry& entry_a() {
    const auto& c = wait(PackEvent::Kind::Catalog, [](const PackEvent& e) { return e.status != CatalogStatus::Loading; });
    for (const auto& e : c.entries) {
      if (e.digest == kSampleA) return e;
    }
    FAIL("sample A not in the catalog");
    return c.entries.front();
  }
};

bool phase_is(const PackEvent& e, DownloadPhase p) { return e.download.phase == p; }

}  // namespace

TEST_CASE("pack service: the catalog is ready from the network, then read offline from the cache") {
  TempDir dir;
  {
    Service s(dir.path());
    s.service->load_catalog();
    const auto& ready = s.wait(PackEvent::Kind::Catalog, [](const PackEvent& e) { return e.status == CatalogStatus::Ready; });
    CHECK(s.events.front().status == CatalogStatus::Loading);  // reported before the worker answers
    CHECK(ready.entries.size() == 2);
    CHECK(ready.entries[0].url == "https://cdn.test/catalog/packs/" + std::string(kSampleA) + ".tlpack");
    s.http->offline = true;
    s.events.clear();
    s.service->load_catalog();
    const auto& offline = s.wait(PackEvent::Kind::Catalog, [](const PackEvent& e) { return e.status == CatalogStatus::Offline; });
    CHECK(offline.entries.size() == 2);  // the last catalog seen on this device
    s.http->offline = false;
    s.http->status = 503;
    s.events.clear();
    s.service->load_catalog();
    CHECK(s.wait(PackEvent::Kind::Catalog, [](const PackEvent& e) { return e.status == CatalogStatus::Unavailable; }).entries.size() == 2);
  }
  Service none(dir.path(), false);  // no libcurl: unavailable, imports and installed packs still work
  none.service->load_catalog();
  CHECK(none.wait(PackEvent::Kind::Catalog, [](const PackEvent& e) { return e.status != CatalogStatus::Loading; }).status ==
        CatalogStatus::Unavailable);
}

TEST_CASE("pack service: a paused download keeps its bytes, resumes from the committed offset (also after a restart) and installs verified") {
  TempDir dir;
  CatalogEntry entry;
  {
    Service s(dir.path());
    s.service->load_catalog();
    entry = s.entry_a();
    // Pause as the first range arrives: the job stops after that chunk.
    s.http->on_range = [&](std::int64_t first) {
      if (first == 0) s.service->pause(kSampleA);
    };
    s.service->download(entry, "Mercado A · 1h");
    const auto& stopped = s.wait(PackEvent::Kind::Download, [](const PackEvent& e) { return phase_is(e, DownloadPhase::Failed); });
    CHECK(stopped.download.reason == "aborted");
    CHECK(stopped.download.received == kChunkBytes);
    const auto& lib = s.wait(PackEvent::Kind::Library, [](const PackEvent& e) { return !e.partial.empty(); });
    CHECK(lib.partial.at(kSampleA) == kChunkBytes);
    CHECK(lib.packs.empty());
  }
  Service s(dir.path());  // the next launch
  s.service->download(entry, "Mercado A · 1h");
  s.wait(PackEvent::Kind::Download, [](const PackEvent& e) { return phase_is(e, DownloadPhase::Installed); });
  REQUIRE_FALSE(s.http->offsets.empty());
  CHECK(s.http->offsets.front() == kChunkBytes);  // only the missing bytes were asked for
  const auto& lib = s.wait(PackEvent::Kind::Library, [](const PackEvent& e) { return !e.packs.empty(); });
  CHECK(lib.packs[0].digest == kSampleA);
  CHECK(lib.packs[0].label == "Mercado A · 1h");
  CHECK(lib.packs[0].origin == "catalog");
  CHECK(lib.partial.empty());
  // Every event in between reported progress, then the check before the install.
  bool verifying = false;
  for (const auto& e : s.events) verifying = verifying || (e.kind == PackEvent::Kind::Download && phase_is(e, DownloadPhase::Verifying));
  CHECK(verifying);

  s.service->remove(kSampleA);
  CHECK(s.wait(PackEvent::Kind::Library, [](const PackEvent& e) { return e.packs.empty(); }).packs.empty());
}

TEST_CASE("pack service: CSV validation reports the engine's findings; an import installs a private pack") {
  TempDir dir;
  Service s(dir.path(), false);
  s.service->validate_csv("timestamp,open\n1,2\n", testing::hourly_meta());
  const auto& bad = s.wait(PackEvent::Kind::CsvReport);
  const auto report = json::parse(bad.report_json);
  REQUIRE(report);
  CHECK_FALSE((*report)["ok"].as_bool(true));
  s.service->import_csv(testing::make_csv(300), testing::hourly_meta(), "BTC/USDT · 1h");
  const auto& done = s.wait(PackEvent::Kind::CsvImported);
  CHECK(done.ok);
  const auto& lib = s.wait(PackEvent::Kind::Library, [](const PackEvent& e) { return !e.packs.empty(); });
  CHECK(lib.packs[0].label == "BTC/USDT · 1h");
  CHECK(lib.packs[0].is_private);
}
