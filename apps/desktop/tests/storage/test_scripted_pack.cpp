// Scripted session through the app's model stack (EngineHost + storage), the desktop counterpart of the
// web's import-and-practice e2e: import a CSV as a private pack, play it, quit, reopen, resume at the same
// state, finish it to the debrief. Also: a pack run whose file is gone does not resume.
#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "model/engine_host.hpp"
#include "model/packs/pack_library.hpp"
#include "model/storage/storage.hpp"
#include "support/storage_fixtures.hpp"
#include "support/ui_queue.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::hourly_meta;
using tradelab::desktop::testing::make_csv;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::UiQueue;

namespace {

struct App {
  UiQueue ui;
  std::shared_ptr<const Snapshot> last;
  EngineHost host;

  explicit App(const std::filesystem::path& dir)
      : host([this](EngineHost::Task t) { ui.post(std::move(t)); },
             [this](std::shared_ptr<const Snapshot> s) { last = std::move(s); },
             std::make_unique<storage::Storage>(dir)) {}

  Snapshot settle() {
    host.wait_idle();
    ui.drain();
    REQUIRE(last);
    return *last;
  }
};

// The import runs on the worker (the engine's run-less calls stay on that thread); the result comes back
// to the "UI" through the same post function the app uses.
packs::CsvImportResult import_csv(App& app, const std::string& csv) {
  packs::CsvImportResult result;
  bool done = false;
  app.host.post_storage_job([&](storage::Storage& s) {
    auto r = s.packs().import_csv(csv, hourly_meta(3), "BTC/USDT 1h");
    app.ui.post([&result, &done, r] {
      result = r;
      done = true;
    });
  });
  app.host.wait_idle();
  app.ui.drain();
  REQUIRE(done);
  return result;
}

}  // namespace

TEST_CASE("scripted: import a CSV pack, play it, reload, resume at the same state and finish it") {
  TempDir dir;
  std::string digest;
  std::string pack_digest;
  int step = 0;
  {
    App app(dir.path());
    const packs::CsvImportResult imported = import_csv(app, make_csv(400));
    REQUIRE(imported.ok);
    pack_digest = imported.digest;

    app.host.start_pack(pack_digest, 77);
    const Snapshot started = app.settle();
    REQUIRE(started.results.size() == 1);
    CHECK(started.results[0].kind == CommandKind::StartPack);
    CHECK(started.results[0].accepted);
    CHECK(started.active());
    CHECK(started.leverage_cap == 3);

    app.host.market(Side::Buy, "1");
    app.host.advance(10);
    app.host.market(Side::Sell, "1");
    app.host.advance(3);
    const Snapshot played = app.settle();
    CHECK(played.durable);
    digest = played.digest;
    step = played.step;
    CHECK(step == started.step + 13);
  }  // quit

  App app(dir.path());
  app.host.resume();
  const Snapshot resumed = app.settle();
  REQUIRE_FALSE(resumed.results.empty());
  CHECK(resumed.results.back().kind == CommandKind::Resume);
  CHECK(resumed.results.back().accepted);
  CHECK(resumed.resume_notice.empty());
  CHECK(resumed.digest == digest);
  CHECK(resumed.step == step);

  app.host.advance(100000);  // to the end of the market
  const Snapshot ended = app.settle();
  CHECK_FALSE(ended.active());
  REQUIRE(ended.debrief);

  storage::RunStore reader(dir.db());
  const auto run = reader.active_run();
  REQUIRE(run);
  CHECK(run->record.pack_digest == pack_digest);
  CHECK(run->record.status == ended.status);
  CHECK(reader.finished_missions().empty());  // practice on a pack is not a mission
}

TEST_CASE("scripted: a pack run whose pack file was deleted does not resume (pack.missing)") {
  TempDir dir;
  std::string pack_digest;
  {
    App app(dir.path());
    const packs::CsvImportResult imported = import_csv(app, make_csv(200));
    REQUIRE(imported.ok);
    pack_digest = imported.digest;
    app.host.start_pack(pack_digest, 1);
    app.host.advance(2);
    app.settle();
  }
  std::filesystem::remove(dir.path() / "packs" / (pack_digest + ".tlpack"));
  App app(dir.path());
  app.host.resume();
  const Snapshot s = app.settle();
  REQUIRE_FALSE(s.results.empty());
  CHECK_FALSE(s.results.back().accepted);
  CHECK(s.resume_notice == "pack_missing");
  CHECK(s.resume_detail == pack_digest);

  app.host.start_pack(pack_digest, 1);
  CHECK(app.settle().results.back().reason == "pack_missing");
}
