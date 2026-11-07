// Simulation worker (design "Threading"): one std::thread owns the EngineRun and every engine call.
// Commands go through a mutex-guarded FIFO queue; after each command (and while a batch advance runs)
// the worker publishes an immutable Snapshot. Snapshots are coalesced: while one delivery is pending
// on the UI thread, newer snapshots replace it instead of queueing more tasks, so at most one hand-off
// per UI frame. The hand-off itself is the `Post` function (Rtask_post in the app, a test queue in
// tests), which keeps this layer free of Raven.
//
// Persistence (model/storage, optional: tests may run without it): every command that changes the run is
// a storage::Action committed to SQLite BEFORE the engine applies it, and the acknowledgement is published
// only after the run header (and every 20 actions a checkpoint) is written. New commands follow the same
// path: build the Action, recorder_->perform(), settle(), publish.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "model/engine_run.hpp"
#include "model/snapshot.hpp"
#include "model/storage/actions.hpp"
#include "model/storage/storage.hpp"

namespace tradelab::desktop {

namespace storage {
class RunRecorder;
}  // namespace storage

// What viewmodels may ask of the engine (EngineHost in the app, fakes in viewmodel tests).
class EngineCommands {
 public:
  virtual ~EngineCommands() = default;
  virtual void start(const std::string& mission, std::uint32_t seed) = 0;
  virtual void advance(int steps) = 0;
  virtual void market(Side side, const std::string& qty) = 0;
  // Terminal commands and start options (perks, campaign capital, leverage gate); the defaults keep
  // simple fakes compiling.
  virtual void game(const GameCommand& /*command*/) {}
  virtual void start_with(const std::string& mission, std::uint32_t seed, const RunOptions& /*options*/) {
    start(mission, seed);
  }
  // Storage (no-ops where there is none): the active saved run, or a new run on an installed pack.
  virtual void resume() {}
  virtual void start_pack(const std::string& /*digest*/, std::uint32_t /*seed*/) {}
  // A run that carries links on its stored record (campaign mission, assisted attempt).
  virtual void start_linked(const std::string& mission, std::uint32_t seed, const RunOptions& options,
                            const RunLinks& /*links*/) {
    start_with(mission, seed, options);
  }
  // Assisted practice: a new attempt rebuilt from the current run's own actions before the decision at
  // `to_step` (same market and start options, without the advisor); acknowledged as CommandKind::Rewind.
  virtual void rewind(int /*to_step*/, const RunLinks& /*links*/) {}
  // A report-only engine call (what-if valuation, combo dry run): `reply` gets the engine's JSON on the
  // UI thread, after the commands sent before it. An engine that cannot answer replies
  // {"error":"unsupported"} (the web's feature detection: the lab and the builder's analysis hide).
  using AnalysisReply = std::function<void(const std::string&)>;
  virtual void analyze(AnalysisKind /*kind*/, const std::string& /*request*/, AnalysisReply reply) {
    reply(R"({"error":"unsupported"})");
  }
  // The replay viewer (add-run-sharing-and-journal-stats D6): rebuilds the run of a verified, canonical
  // run file without storing anything; while it is watched, advance() plays its log bar by bar and
  // every other command is refused ("replay"). Acknowledged as CommandKind::Watch.
  virtual void watch(const std::string& /*file_text*/) {}
};

class EngineHost final : public EngineCommands {
 public:
  using Task = std::function<void()>;
  // Runs a task on the UI thread later; must be callable from the worker thread.
  using Post = std::function<void(Task)>;
  using Sink = std::function<void(std::shared_ptr<const Snapshot>)>;

  using StorageJob = std::function<void(storage::Storage&)>;

  // With `storage`, runs are persisted and resumable (it is used on the worker only from then on).
  EngineHost(Post post, Sink sink, std::unique_ptr<storage::Storage> storage = nullptr);
  ~EngineHost() override;
  EngineHost(const EngineHost&) = delete;
  EngineHost& operator=(const EngineHost&) = delete;

  // Read once at construction, before the worker exists (tl_missions uses a shared static buffer).
  [[nodiscard]] const std::vector<MissionInfo>& missions() const { return missions_; }

  void start(const std::string& mission, std::uint32_t seed) override;
  void advance(int steps) override;
  void market(Side side, const std::string& qty) override;
  void game(const GameCommand& command) override;
  void start_with(const std::string& mission, std::uint32_t seed, const RunOptions& options) override;
  void resume() override;
  void start_pack(const std::string& digest, std::uint32_t seed) override;
  void start_linked(const std::string& mission, std::uint32_t seed, const RunOptions& options,
                    const RunLinks& links) override;
  void rewind(int to_step, const RunLinks& links) override;
  void analyze(AnalysisKind kind, const std::string& request, AnalysisReply reply) override;
  void watch(const std::string& file_text) override;
  // Runs `job` on the worker, in queue order, with the storage (pack install/import, catalog downloads:
  // the engine's run-less calls must stay on this thread). Not run when the host has no storage.
  void post_storage_job(StorageJob job);

  // Blocks until every queued command has run (tests and shutdown only; the UI never waits).
  void wait_idle();
  [[nodiscard]] std::thread::id worker_id() const { return worker_.get_id(); }

 private:
  struct Command {
    CommandKind kind = CommandKind::Start;
    std::string mission;
    std::uint32_t seed = 0;
    int steps = 0;
    Side side = Side::Buy;
    std::string qty;
    GameCommand game;
    RunOptions options;
    std::string digest;
    RunLinks links;
    StorageJob job;
    AnalysisKind analysis = AnalysisKind::WhatIf;
    AnalysisReply reply;  // set: a report-only analysis of `qty` (the request JSON), not a run command
  };

  // What the worker knows of the run in play, to rebuild it (assisted rewind).
  struct Current {
    std::string mission;
    std::uint32_t seed = 0;
    RunOptions options;
    std::string pack_digest;
    bool assisted = false;
    std::vector<std::string> log;  // the run's actions as logged
  };

  // Shared with posted tasks so a task that runs after the host is gone does nothing.
  struct Mailbox {
    std::mutex mu;
    std::shared_ptr<const Snapshot> pending;
    bool posted = false;
    bool closed = false;
    Sink sink;
  };

  void enqueue(Command c);
  void worker_loop();
  void execute(const Command& c);
  void run_start(const Command& c);
  void run_advance(int steps);
  void run_market(const Command& c);
  void run_game(const Command& c);
  void run_resume();
  void run_start_pack(const Command& c);
  void run_rewind(const Command& c);
  void run_analysis(const Command& c);
  void run_watch(const Command& c);
  void run_watch_steps(int steps);
  bool watch_step();
  void stamp_watch(Snapshot& s) const;
  std::unique_ptr<EngineRun> open_run(const Current& next, const RunLinks& links, std::string* reason);
  storage::ApplyResult perform(const storage::Action& a);
  void stamp(Snapshot& s);
  void settle(Snapshot& s);
  void publish_refusal(CommandKind kind, const std::string& reason);
  [[nodiscard]] bool delivery_pending() const;
  void publish(Snapshot s);
  static void deliver(const std::shared_ptr<Mailbox>& box);

  std::vector<MissionInfo> missions_;
  Post post_;
  std::shared_ptr<Mailbox> mailbox_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::condition_variable idle_cv_;
  std::deque<Command> queue_;
  bool busy_ = false;
  bool quit_ = false;

  // Worker-only state.
  std::unique_ptr<EngineRun> run_;
  std::unique_ptr<storage::Storage> storage_;
  std::unique_ptr<storage::RunRecorder> recorder_;
  Current current_;
  // The watched run file's log and where the viewer is in it (an advance of n bars plays one by one).
  struct Watching {
    std::vector<std::string> actions;
    std::size_t index = 0;
    int left = 0;  // bars left of the advance at `index` once it started
    int bar = 0;
    int bars = 0;
    [[nodiscard]] bool done() const { return index >= actions.size() && left == 0; }
  };
  std::optional<Watching> watching_;
  std::shared_ptr<const std::vector<std::string>> shared_log_;  // copy of current_.log for snapshots
  std::uint64_t seq_ = 0;

  std::thread worker_;
};

}  // namespace tradelab::desktop
