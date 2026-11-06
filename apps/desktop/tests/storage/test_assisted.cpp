// Assisted practice in the model (web assisted.ts, session.rewind): the rewind prefix of a log, and
// EngineHost::rewind building a new stored attempt flagged assisted from the run's own actions.
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "model/engine_host.hpp"
#include "model/engine_run.hpp"
#include "model/storage/actions.hpp"
#include "model/storage/storage.hpp"
#include "support/storage_fixtures.hpp"
#include "support/ui_queue.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::UiQueue;

namespace {

std::vector<std::string> web_log() {
  return {R"({"k":"lesson","q":0})", R"({"k":"adv","n":3})", R"({"k":"order","id":1})",
          R"({"k":"adv","n":10})",  R"({"k":"close"})",        R"({"k":"adv","n":1})"};
}

// A host over `storage` (or none) whose snapshots land in `last` through the UI queue.
struct Host {
  UiQueue ui;
  std::shared_ptr<const Snapshot> last;
  std::unique_ptr<EngineHost> host;
  explicit Host(std::unique_ptr<storage::Storage> storage = nullptr) {
    host = std::make_unique<EngineHost>([this](EngineHost::Task t) { ui.post(std::move(t)); },
                                        [this](std::shared_ptr<const Snapshot> s) { last = std::move(s); },
                                        std::move(storage));
  }
  // Runs `command`, then waits for its acknowledgement of `kind`.
  const Snapshot& run(const std::function<void()>& command, CommandKind kind) {
    last.reset();
    command();
    REQUIRE(ui.pump_until([&] {
      if (!last) return false;
      for (const auto& r : last->results) {
        if (r.kind == kind) return true;
      }
      return false;
    }));
    return *last;
  }
};

}  // namespace

TEST_CASE("rewind prefix: the web's vectors") {
  const auto log = web_log();
  using V = std::vector<std::string>;
  CHECK(storage::rewind_prefix(log, 60, 68) == V{log[0], log[1], log[2], R"({"k":"adv","n":5})"});
  CHECK(storage::rewind_prefix(log, 60, 63) == V{log[0], log[1]});
  CHECK(storage::rewind_prefix(log, 60, 73) == V{log[0], log[1], log[2], log[3]});
  // To the first decision: the briefing's lesson answers stay, nothing done at that decision does.
  CHECK(storage::rewind_prefix(log, 60, 60) == V{log[0]});
  const V first = {R"({"k":"lesson","q":0})", R"({"k":"lev","lev":3})", R"({"k":"lesson","q":1})", R"({"k":"adv","n":2})"};
  CHECK(storage::rewind_prefix(first, 60, 60) == V{first[0], first[2]});
}

TEST_CASE("engine host: a rewind is a new stored attempt flagged assisted, replaying the actions before the decision") {
  TempDir dir;
  Host h(std::make_unique<storage::Storage>(dir.path()));
  const Snapshot& started = h.run([&] { h.host->start_with("m01-false-breakout", 3, RunOptions{3, 0, false, "", 0}); }, CommandKind::Start);
  const std::string original = started.run_id;
  const int warmup = started.warmup_bars;
  CHECK_FALSE(original.empty());
  CHECK_FALSE(started.assisted);
  h.run([&] { h.host->advance(3); }, CommandKind::Advance);
  h.run([&] { h.host->market(Side::Buy, "1"); }, CommandKind::Market);
  h.run([&] { h.host->advance(10); }, CommandKind::Advance);

  const RunLinks links{{}, R"({"from":")" + original + R"(","kind":"rewind","step":)" + std::to_string(warmup + 5) + "}"};
  const Snapshot& rewound = h.run([&] { h.host->rewind(warmup + 5, links); }, CommandKind::Rewind);
  CHECK(rewound.results.back().accepted);
  CHECK(rewound.step == warmup + 5);
  CHECK(rewound.assisted);
  CHECK(rewound.run_id != original);
  CHECK(rewound.position_qty != "0.00000000");  // the order placed at the 4th decision is kept

  // Same state as a fresh run given the same prefix (advice allowance 0: without the advisor).
  auto fresh = EngineRun::create("m01-false-breakout", 3, RunOptions{0, 0, false, "", 0});
  fresh->advance(3);
  fresh->place_market(Side::Buy, "1");
  fresh->advance(2);
  CHECK(fresh->snapshot().digest == rewound.digest);

  h.host->wait_idle();
  h.host.reset();  // the worker's connection is closed: read the file
  storage::RunStore store(dir.db());
  const auto attempt = store.run(rewound.run_id);
  REQUIRE(attempt);
  CHECK(attempt->assisted == std::optional<std::string>(links.assisted));
  CHECK(attempt->options.advice_allowance == 0);
  CHECK(store.run(original)->status == "left");  // the original attempt keeps its record and log
  CHECK(store.active_run()->actions.size() == 3);  // adv 3, order, adv 2
}

TEST_CASE("engine host: rewind outside the range, or with no run, is refused") {
  Host h;
  h.run([&] { h.host->rewind(70, {}); }, CommandKind::Rewind);
  CHECK_FALSE(h.last->results.back().accepted);
  const Snapshot& s = h.run([&] { h.host->start("m01-false-breakout", 3); }, CommandKind::Start);
  const int warmup = s.warmup_bars;
  h.run([&] { h.host->rewind(warmup, {}); }, CommandKind::Rewind);  // nothing before the first decision
  CHECK(h.last->results.back().reason == "invalid_step");
}

TEST_CASE("engine host: rewind works without storage too (session only)") {
  Host h;
  const Snapshot& s = h.run([&] { h.host->start("m02-funding-squeeze", 9); }, CommandKind::Start);
  const int warmup = s.warmup_bars;
  h.run([&] { h.host->advance(4); }, CommandKind::Advance);
  const Snapshot& r = h.run([&] { h.host->rewind(warmup + 1, {}); }, CommandKind::Rewind);
  CHECK(r.results.back().accepted);
  CHECK(r.step == warmup + 1);
  CHECK(r.assisted);
  CHECK(r.run_id.empty());
}

TEST_CASE("engine host: a linked start stores its campaign and assisted links; a resume keeps the flag") {
  TempDir dir;
  {
    Host h(std::make_unique<storage::Storage>(dir.path()));
    const RunLinks links{R"({"id":"camp-1","index":0})", R"({"from":"run-x","kind":"seed"})"};
    const Snapshot& s = h.run([&] { h.host->start_linked("m01-false-breakout", 4, {}, links); }, CommandKind::Start);
    CHECK(s.assisted);
    h.run([&] { h.host->advance(2); }, CommandKind::Advance);
    h.host->wait_idle();
    h.host.reset();
    storage::RunStore store(dir.db());
    const auto active = store.active_run();
    REQUIRE(active);
    CHECK(active->record.campaign == std::optional<std::string>(links.campaign));
    CHECK(active->record.assisted == std::optional<std::string>(links.assisted));
  }
  Host again(std::make_unique<storage::Storage>(dir.path()));
  const Snapshot& resumed = again.run([&] { again.host->resume(); }, CommandKind::Resume);
  CHECK(resumed.results.back().accepted);
  CHECK(resumed.assisted);
  CHECK_FALSE(resumed.run_id.empty());
}
