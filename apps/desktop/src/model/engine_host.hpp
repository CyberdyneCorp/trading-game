// Simulation worker (design "Threading"): one std::thread owns the EngineRun and every engine call.
// Commands go through a mutex-guarded FIFO queue; after each command (and while a batch advance runs)
// the worker publishes an immutable Snapshot. Snapshots are coalesced: while one delivery is pending
// on the UI thread, newer snapshots replace it instead of queueing more tasks, so at most one hand-off
// per UI frame. The hand-off itself is the `Post` function (Rtask_post in the app, a test queue in
// tests), which keeps this layer free of Raven.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "model/engine_run.hpp"
#include "model/snapshot.hpp"

namespace tradelab::desktop {

// What viewmodels may ask of the engine (EngineHost in the app, fakes in viewmodel tests).
class EngineCommands {
 public:
  virtual ~EngineCommands() = default;
  virtual void start(const std::string& mission, std::uint32_t seed) = 0;
  virtual void advance(int steps) = 0;
  virtual void market(Side side, const std::string& qty) = 0;
  // Terminal commands and start options; the defaults keep simple fakes compiling.
  virtual void game(const GameCommand& /*command*/) {}
  virtual void start_with(const std::string& mission, std::uint32_t seed, const RunOptions& /*options*/) {
    start(mission, seed);
  }
};

class EngineHost final : public EngineCommands {
 public:
  using Task = std::function<void()>;
  // Runs a task on the UI thread later; must be callable from the worker thread.
  using Post = std::function<void(Task)>;
  using Sink = std::function<void(std::shared_ptr<const Snapshot>)>;

  EngineHost(Post post, Sink sink);
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
  std::uint64_t seq_ = 0;

  std::thread worker_;
};

}  // namespace tradelab::desktop
