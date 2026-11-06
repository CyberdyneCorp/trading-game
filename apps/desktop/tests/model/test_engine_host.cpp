#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "model/engine_host.hpp"
#include "support/ui_queue.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::UiQueue;

namespace {

constexpr const char* kM01 = "m01-false-breakout";

struct Harness {
  UiQueue ui;
  std::vector<std::shared_ptr<const Snapshot>> received;
  std::vector<std::thread::id> sink_threads;
  EngineHost host{[this](EngineHost::Task t) { ui.post(std::move(t)); },
                  [this](std::shared_ptr<const Snapshot> s) {
                    sink_threads.push_back(std::this_thread::get_id());
                    received.push_back(std::move(s));
                  }};

  std::shared_ptr<const Snapshot> settle() {
    host.wait_idle();
    ui.drain();
    return received.empty() ? nullptr : received.back();
  }
};

}  // namespace

TEST_CASE("engine host: the twelve built-in missions") {
  Harness h;
  REQUIRE(h.host.missions().size() == 12);
  CHECK(h.host.missions().front().id == kM01);
  CHECK(h.host.missions().back().id == "m12-capstone");
}

TEST_CASE("engine host: start publishes the first decision step") {
  Harness h;
  h.host.start(kM01, 1);
  auto s = h.settle();
  REQUIRE(s);
  CHECK(s->mission == kM01);
  CHECK(s->status == "active");
  CHECK(s->step == s->warmup_bars);
  CHECK(s->initial_balance == "10000.00000000");
  CHECK(s->bars.size() == static_cast<std::size_t>(s->step) + 1);  // bars 0..step, never a future bar
  REQUIRE(s->results.size() == 1);
  CHECK(s->results[0].accepted);
}

TEST_CASE("engine host: unknown mission is refused, not a crash") {
  Harness h;
  h.host.start("m99-nope", 1);
  auto s = h.settle();
  REQUIRE(s);
  REQUIRE(s->results.size() == 1);
  CHECK_FALSE(s->results[0].accepted);
  CHECK(s->results[0].reason == "unknown_mission");
}

TEST_CASE("engine host: a 50-step batch equals 50 single advances (state digest)") {
  Harness batch;
  batch.host.start(kM01, 7);
  batch.host.market(Side::Buy, "1");
  batch.host.advance(50);
  auto a = batch.settle();

  Harness single;
  single.host.start(kM01, 7);
  single.host.market(Side::Buy, "1");
  for (int i = 0; i < 50; ++i) single.host.advance(1);
  auto b = single.settle();

  REQUIRE(a);
  REQUIRE(b);
  CHECK(a->step == b->step);
  CHECK(a->digest == b->digest);
  CHECK(a->equity == b->equity);
  CHECK(a->progress_done == 50);
  CHECK(a->progress_total == 50);
}

TEST_CASE("engine host: snapshots reach the sink on the UI thread only, never on the worker") {
  Harness h;
  h.host.start(kM01, 3);
  h.host.advance(30);
  h.settle();
  REQUIRE_FALSE(h.sink_threads.empty());
  for (auto id : h.sink_threads) {
    CHECK(id == std::this_thread::get_id());
    CHECK(id != h.host.worker_id());
  }
}

TEST_CASE("engine host: snapshots coalesce while the UI is busy; acknowledgements are kept") {
  Harness h;
  h.host.start(kM01, 3);
  h.host.market(Side::Buy, "1");
  h.host.advance(20);
  h.host.market(Side::Sell, "1");
  h.host.wait_idle();  // the UI thread did not run a frame meanwhile
  CHECK(h.ui.posted() == 1);  // one hand-off for everything
  h.ui.drain();
  REQUIRE(h.received.size() == 1);
  const auto& s = *h.received.back();
  REQUIRE(s.results.size() == 4);
  CHECK(s.results[0].kind == CommandKind::Start);
  CHECK(s.results[1].kind == CommandKind::Market);
  CHECK(s.results[2].kind == CommandKind::Advance);
  CHECK(s.results[3].kind == CommandKind::Market);
  CHECK(s.step == s.warmup_bars + 20);
  CHECK(s.seq > 2);
}

TEST_CASE("engine host: queue order is preserved under concurrent enqueue") {
  Harness h;
  h.host.start(kM01, 11);
  h.settle();
  // Two producers each send buy/sell pairs; the engine sees every command exactly once and in FIFO
  // order per producer, so the position is flat again at the end and every order was acknowledged.
  std::atomic<bool> go{false};
  auto producer = [&] {
    while (!go) std::this_thread::yield();
    for (int i = 0; i < 25; ++i) {
      h.host.market(Side::Buy, "1");
      h.host.market(Side::Sell, "1");
    }
  };
  std::thread p1(producer);
  std::thread p2(producer);
  go = true;
  p1.join();
  p2.join();
  h.host.wait_idle();
  h.ui.drain();
  std::size_t markets = 0;
  for (const auto& s : h.received) {
    for (const auto& r : s->results) markets += r.kind == CommandKind::Market ? 1 : 0;
  }
  CHECK(markets == 100);
  CHECK(std::stod(h.received.back()->position_qty) == 0.0);
}

TEST_CASE("engine host: m01 runs to the end and carries the debrief") {
  Harness h;
  h.host.start(kM01, 1);
  h.host.advance(500);
  auto s = h.settle();
  REQUIRE(s);
  CHECK(s->status == "finished");
  REQUIRE(s->debrief);
  CHECK_FALSE(s->debrief->final_equity.empty());
  CHECK(s->debrief->score_total >= 0);
  CHECK(s->progress_done < 500);  // stopped early when the run ended
  // Regression: a batch that ended the run while deliveries were coalescing was never acknowledged,
  // so the viewmodel kept waiting forever.
  REQUIRE_FALSE(s->results.empty());
  CHECK(s->results.back().kind == CommandKind::Advance);
}
