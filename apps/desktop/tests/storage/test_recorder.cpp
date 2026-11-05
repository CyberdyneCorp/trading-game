#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "model/engine_host.hpp"
#include "model/json.hpp"
#include "model/storage/actions.hpp"
#include "model/storage/run_recorder.hpp"
#include "support/storage_fixtures.hpp"
#include "support/ui_queue.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::storage;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::UiQueue;

namespace {

constexpr const char* kM01 = "m01-false-breakout";
constexpr std::uint32_t kSeed = 4242;

// A scripted session: buys, sells and single advances, `n` actions long.
std::vector<Action> script(int n) {
  std::vector<Action> out;
  for (int i = 0; static_cast<int>(out.size()) < n; ++i) {
    if (i % 4 == 0) out.push_back(actions::market(Side::Buy, "1"));
    else if (i % 4 == 2) out.push_back(actions::market(Side::Sell, "1"));
    else out.push_back(actions::advance(1));
  }
  return out;
}

// Plays `n` scripted actions through a recorder, like the host does; returns the final digest.
std::string play(RunRecorder& rec, EngineRun& run, const std::vector<Action>& acts, std::size_t from, std::size_t to) {
  for (std::size_t i = from; i < to; ++i) {
    rec.perform(run, acts[i]);
    rec.settle(run, run.snapshot());
  }
  return run.snapshot().digest;
}

// The digest after the first `n` actions of the script, computed without storage.
std::string expected_digest(const std::vector<Action>& acts, std::size_t n) {
  auto run = EngineRun::create(kM01, kSeed);
  for (std::size_t i = 0; i < n; ++i) apply_action(*run, acts[i]);
  return run->snapshot().digest;
}

}  // namespace

TEST_CASE("storage actions: the web's Action JSON shapes") {
  CHECK(actions::advance(5).json == R"({"k":"adv","n":5})");
  CHECK(actions::market(Side::Sell, "0.5").json ==
        R"({"k":"order","type":"market","side":"sell","qty":"0.5","price":null,"ro":false})");
  OrderAction o;
  o.type = "limit";
  o.qty = "1";
  o.price = "95.26";
  o.stop = "90";
  o.take_profit = "110";
  CHECK(actions::order(o).json ==
        R"({"k":"order","type":"limit","side":"buy","qty":"1","price":"95.26","ro":false,"stop":"90","tp":"110"})");
  CHECK(actions::cancel(3).json == R"({"k":"cancel","id":3})");
  CHECK(actions::move(3, "99.5").json == R"({"k":"move","id":3,"price":"99.5"})");
  CHECK(actions::answer(6500, false).json == R"({"k":"ans","pBp":6500,"skipped":false})");
  CHECK(actions::close().json == R"({"k":"close"})");
  PlanAction p;
  p.invalidation = "92";
  p.confidence = 60;
  CHECK(actions::plan(p).json ==
        R"({"k":"plan","side":"buy","skipped":false,"option":false,"lose":false,"inv":"92","target":null,"conf":60,"risk":null})");
}

TEST_CASE("storage actions: a logged market order is the engine's market order (same digest)") {
  auto direct = EngineRun::create(kM01, kSeed);
  auto logged = EngineRun::create(kM01, kSeed);
  const CommandResult a = direct->place_market(Side::Buy, "1");
  const ApplyResult b = apply_action(*logged, actions::market(Side::Buy, "1"));
  CHECK(a.accepted);
  CHECK(b.accepted);
  CHECK(b.reason == a.reason);
  direct->advance(3);
  apply_action(*logged, actions::advance(3));
  CHECK(direct->snapshot().digest == logged->snapshot().digest);
  CHECK_FALSE(apply_action(*logged, Action{R"({"k":"teleport"})"}).known);
  CHECK_FALSE(apply_action(*logged, Action{"not json"}).known);
}

TEST_CASE("storage recorder: resume = latest checkpoint + tail, same digest as an uninterrupted run") {
  TempDir dir;
  const auto acts = script(45);
  std::string digest;
  {
    Storage storage(dir.path());
    RunRecorder rec(storage);
    auto run = rec.start(kM01, kSeed);
    REQUIRE(run);
    digest = play(rec, *run, acts, 0, acts.size());
    CHECK(storage.store().checkpoint(rec.run_id())->actions == 40);
  }
  Storage storage(dir.path());
  RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed);
  REQUIRE(resumed->run);
  CHECK(resumed->from_checkpoint == 40);
  CHECK(resumed->actions == 45);
  CHECK(resumed->notice.kind == ResumeNoticeKind::None);
  CHECK(resumed->run->snapshot().digest == digest);
  CHECK(digest == expected_digest(acts, acts.size()));

  // Play on after the resume: the log continues at seq 45 with the right client ids.
  const auto more = script(50);
  const std::string after = play(rec, *resumed->run, more, 45, 50);
  CHECK(after == expected_digest(more, 50));
  CHECK(storage.store().active_run()->actions.size() == 50);
}

TEST_CASE("storage recorder: a checkpoint from another engine version is ignored (full replay)") {
  TempDir dir;
  const auto acts = script(25);
  {
    Storage storage(dir.path());
    RunRecorder rec(storage);
    auto run = rec.start(kM01, kSeed);
    play(rec, *run, acts, 0, acts.size());
    storage.store().db().exec("UPDATE checkpoints SET engine = '0.0.1-old'");
  }
  Storage storage(dir.path());
  RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed);
  REQUIRE(resumed->run);
  CHECK(resumed->from_checkpoint == 0);
  CHECK(resumed->run->snapshot().digest == expected_digest(acts, acts.size()));
  CHECK(resumed->notice.kind == ResumeNoticeKind::None);  // the run itself is from this engine
}

TEST_CASE("storage recorder: a damaged checkpoint falls back to the whole log") {
  TempDir dir;
  const auto acts = script(21);
  {
    Storage storage(dir.path());
    RunRecorder rec(storage);
    auto run = rec.start(kM01, kSeed);
    play(rec, *run, acts, 0, acts.size());
    storage.store().db().exec("UPDATE checkpoints SET hex = substr(hex, 1, 40)");
  }
  Storage storage(dir.path());
  RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed->run);
  CHECK(resumed->from_checkpoint == 0);
  CHECK(resumed->run->snapshot().digest == expected_digest(acts, acts.size()));
}

TEST_CASE("storage recorder: engine pinning - a run from another engine or policy resumes here, with the notice") {
  TempDir dir;
  const auto acts = script(6);
  {
    Storage storage(dir.path());
    RunRecorder rec(storage);
    auto run = rec.start(kM01, kSeed);
    play(rec, *run, acts, 0, acts.size());
  }
  {
    Storage storage(dir.path());
    storage.store().db().exec("UPDATE runs SET engine = '0.9.0'");
    RunRecorder rec(storage);
    auto resumed = rec.resume();
    REQUIRE(resumed->run);
    CHECK(resumed->notice.kind == ResumeNoticeKind::EngineChanged);
    CHECK(resumed->notice.detail == "0.9.0");
    CHECK(resumed->notice.i18n_key() == "engine.pinMissing");  // the web's "resumed on the current engine"
    CHECK(resumed->run->snapshot().digest == expected_digest(acts, acts.size()));
  }
  Storage storage(dir.path());
  storage.store().db().exec("UPDATE runs SET engine = '" + current_engine_version().engine +
                            "', execution_policy = execution_policy - 1");
  RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed->run);
  CHECK(resumed->notice.kind == ResumeNoticeKind::PolicyChanged);
}

TEST_CASE("storage recorder: a recorded digest the replay does not reach is reported") {
  TempDir dir;
  {
    Storage storage(dir.path());
    RunRecorder rec(storage);
    auto run = rec.start(kM01, kSeed);
    play(rec, *run, script(3), 0, 3);
    storage.store().db().exec("UPDATE runs SET digest = 'deadbeefcafe'");
  }
  Storage storage(dir.path());
  RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed->run);
  CHECK(resumed->notice.kind == ResumeNoticeKind::DigestMismatch);
  CHECK(resumed->notice.detail == "deadbeef");
}

TEST_CASE("storage recorder: no saved run, and a run whose mission is unknown") {
  TempDir dir;
  Storage storage(dir.path());
  RunRecorder rec(storage);
  CHECK_FALSE(rec.resume());
  CHECK_FALSE(rec.start("m99-nope", 1));
  auto run = rec.start(kM01, 1);
  storage.store().db().exec("UPDATE runs SET mission_id = 'm99-nope'");
  auto resumed = rec.resume();
  REQUIRE(resumed);
  CHECK_FALSE(resumed->run);
  CHECK(resumed->notice.kind == ResumeNoticeKind::Damaged);
}

TEST_CASE("storage host: commands are committed before they are acknowledged, and a new host resumes the run") {
  TempDir dir;
  std::string digest;
  {
    UiQueue ui;
    std::shared_ptr<const Snapshot> last;
    EngineHost host([&](EngineHost::Task t) { ui.post(std::move(t)); },
                    [&](std::shared_ptr<const Snapshot> s) { last = std::move(s); },
                    std::make_unique<Storage>(dir.path()));
    host.start(kM01, kSeed);
    host.market(Side::Buy, "1");
    host.advance(5);
    host.market(Side::Sell, "1");
    host.wait_idle();
    ui.drain();
    REQUIRE(last);
    CHECK(last->durable);
    digest = last->digest;
    RunStore reader(dir.db());  // a second connection (the UI's) sees every acknowledged action
    const auto run = reader.active_run();
    REQUIRE(run);
    CHECK(run->actions.size() == 3);
    CHECK(run->actions[1] == R"({"k":"adv","n":5})");
    CHECK(run->record.digest == digest);
  }
  UiQueue ui;
  std::vector<std::shared_ptr<const Snapshot>> got;
  EngineHost host([&](EngineHost::Task t) { ui.post(std::move(t)); },
                  [&](std::shared_ptr<const Snapshot> s) { got.push_back(std::move(s)); },
                  std::make_unique<Storage>(dir.path()));
  host.resume();
  host.wait_idle();
  ui.drain();
  REQUIRE_FALSE(got.empty());
  const auto& s = got.back();
  REQUIRE(s->results.size() == 1);
  CHECK(s->results[0].kind == CommandKind::Resume);
  CHECK(s->results[0].accepted);
  CHECK(s->digest == digest);
  CHECK(s->resume_notice.empty());
}

TEST_CASE("storage host: resume without storage or without a saved run is refused, not a crash") {
  UiQueue ui;
  std::shared_ptr<const Snapshot> last;
  EngineHost bare([&](EngineHost::Task t) { ui.post(std::move(t)); },
                  [&](std::shared_ptr<const Snapshot> s) { last = std::move(s); });
  bare.resume();
  bare.wait_idle();
  ui.drain();
  REQUIRE(last);
  CHECK_FALSE(last->results.at(0).accepted);
  CHECK(last->results.at(0).reason == "no_storage");

  TempDir dir;
  EngineHost host([&](EngineHost::Task t) { ui.post(std::move(t)); },
                  [&](std::shared_ptr<const Snapshot> s) { last = std::move(s); }, std::make_unique<Storage>(dir.path()));
  host.resume();
  host.wait_idle();
  ui.drain();
  CHECK(last->results.at(0).reason == "no_saved_run");
}

// Terminal commands (CommandKind::Game) and the run's start options go through the log like the market
// order: a resumed run reaches the same digest (plan, leverage, bracket order and advice replayed in
// order) and the run keeps its perks.
TEST_CASE("storage host: terminal commands and start options are logged and replay to the same state") {
  TempDir dir;
  std::string digest;
  RunOptions options;
  options.advice_allowance = 2;
  options.calendar = true;
  {
    UiQueue ui;
    std::shared_ptr<const Snapshot> last;
    EngineHost host([&](EngineHost::Task t) { ui.post(std::move(t)); },
                    [&](std::shared_ptr<const Snapshot> s) { last = std::move(s); },
                    std::make_unique<Storage>(dir.path()));
    host.start_with(kM01, kSeed, options);
    host.game(GameCommand::leverage(3));
    host.game(GameCommand::plan(true, false, false, false, "95.5", "110", 70, "50"));
    host.game(GameCommand::order(OrderType::Market, true, "1.5", "", false, "95.5", "110"));
    host.advance(3);
    host.game(GameCommand::advice());
    host.wait_idle();
    ui.drain();
    REQUIRE(last);
    digest = last->digest;
    REQUIRE(last->game);
    CHECK(last->game->position.qty == doctest::Approx(1.5));
    RunStore reader(dir.db());
    const auto run = reader.active_run();
    REQUIRE(run);
    REQUIRE(run->actions.size() == 5);
    CHECK(run->actions[0] == R"({"k":"lev","lev":3})");
    CHECK(run->actions[1].find(R"("k":"plan")") != std::string::npos);
    CHECK(run->actions[2].find(R"("stop":"95.5")") != std::string::npos);
    CHECK(run->actions[4] == R"({"k":"advice"})");
    CHECK(run->record.options == options);
  }
  UiQueue ui;
  std::shared_ptr<const Snapshot> got;
  EngineHost host([&](EngineHost::Task t) { ui.post(std::move(t)); },
                  [&](std::shared_ptr<const Snapshot> s) { got = std::move(s); }, std::make_unique<Storage>(dir.path()));
  host.resume();
  host.wait_idle();
  ui.drain();
  REQUIRE(got);
  CHECK(got->results.at(0).accepted);
  CHECK(got->digest == digest);
  REQUIRE(got->game);
  CHECK(got->game->position.qty == doctest::Approx(1.5));
}
