// OFF02 (spec desktop-client "Killed during write"): a child process plays a session through the real
// EngineHost + SQLite and is killed with SIGKILL at each fault point of one command; the restarted client
// must resume at the last committed command, with the expected state digest and no duplicated fill, and
// finish the session exactly like an uninterrupted run.
#include <doctest/doctest.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <string>
#include <vector>

#include "model/json.hpp"
#include "model/storage/fault.hpp"
#include "model/storage/run_recorder.hpp"
#include "storage/crash_script.hpp"
#include "support/storage_fixtures.hpp"

extern char** environ;  // NOLINT(readability-redundant-declaration): POSIX

using namespace tradelab::desktop;
using namespace tradelab::desktop::storage;
using tradelab::desktop::testing::crash_script;
using tradelab::desktop::testing::kCrashMission;
using tradelab::desktop::testing::kCrashSeed;
using tradelab::desktop::testing::TempDir;

namespace {

constexpr int kActions = 30;
constexpr int kKillAt = 23;  // the 23rd action: after the checkpoint at 20, so resume uses checkpoint + tail

struct Outcome {
  bool killed = false;
  int exit_code = -1;
};

Outcome run_child(const std::string& dir, const std::string& fault) {
  std::vector<std::string> env_text;
  for (char** e = environ; *e != nullptr; ++e) {
    if (std::string(*e).rfind("TRADELAB_FAULT=", 0) != 0) env_text.emplace_back(*e);
  }
  if (!fault.empty()) env_text.push_back("TRADELAB_FAULT=" + fault);
  std::vector<char*> envp;
  for (auto& s : env_text) envp.push_back(s.data());
  envp.push_back(nullptr);
  std::string exe = TRADELAB_CRASH_CHILD;
  std::string dir_arg = dir;
  std::string count = std::to_string(kActions);
  std::vector<char*> argv = {exe.data(), dir_arg.data(), count.data(), nullptr};
  pid_t pid = 0;
  REQUIRE(posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), envp.data()) == 0);
  int status = 0;
  REQUIRE(waitpid(pid, &status, 0) == pid);
  Outcome o;
  o.killed = WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
  if (WIFEXITED(status)) o.exit_code = WEXITSTATUS(status);
  return o;
}

struct State {
  std::string digest;
  std::int64_t fills = 0;
};

State state_of(const EngineRun& run) {
  const auto s = json::parse(run.state_json());
  return State{(*s)["digest"].as_string(), (*s)["totals"]["fills"].as_int()};
}

State expected_after(int n) {
  auto run = EngineRun::create(kCrashMission, kCrashSeed);
  for (const Action& a : crash_script(n)) apply_action(*run, a);
  return state_of(*run);
}

// Resumes in this process, checks the state, then plays the rest of the session.
void check_resume(const std::string& dir, int committed) {
  Storage storage(dir);
  RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed);
  REQUIRE(resumed->run);
  CHECK(resumed->actions == committed);
  CHECK(resumed->from_checkpoint == 20);
  CHECK(resumed->notice.kind == ResumeNoticeKind::None);
  const State got = state_of(*resumed->run);
  const State want = expected_after(committed);
  REQUIRE(want.fills > 0);  // the script really trades
  CHECK(got.digest == want.digest);
  CHECK(got.fills == want.fills);  // the killed command's fill is there once or not at all

  const auto script = crash_script(kActions);
  for (int i = committed; i < kActions; ++i) {
    rec.perform(*resumed->run, script[static_cast<std::size_t>(i)]);
    rec.settle(*resumed->run, resumed->run->snapshot());
  }
  const State end = state_of(*resumed->run);
  const State full = expected_after(kActions);
  CHECK(end.digest == full.digest);
  CHECK(end.fills == full.fills);
}

}  // namespace

TEST_CASE("OFF02: the uninterrupted child session resumes to the same state") {
  TempDir dir;
  const Outcome o = run_child(dir.path().string(), "");
  REQUIRE_FALSE(o.killed);
  REQUIRE(o.exit_code == 0);
  Storage storage(dir.path());
  RunRecorder rec(storage);
  auto resumed = rec.resume();
  REQUIRE(resumed->run);
  CHECK(resumed->actions == kActions);
  CHECK(state_of(*resumed->run).digest == expected_after(kActions).digest);
}

TEST_CASE("OFF02: killed at each point of a command, the restart resumes from the last committed command") {
  struct Case {
    FaultPoint point;
    int committed;  // actions in the log after the kill
  };
  const Case cases[] = {
      {FaultPoint::BeforeCommit, kKillAt - 1},  // nothing written: the command was never accepted
      {FaultPoint::MidCommit, kKillAt - 1},     // transaction open, never committed: rolled back
      {FaultPoint::AfterCommit, kKillAt},       // committed, not applied: replayed once
      {FaultPoint::AfterApply, kKillAt},        // applied, not acknowledged: replayed once
  };
  for (const Case& c : cases) {
    const std::string name(fault_point_name(c.point));
    CAPTURE(name);
    TempDir dir;
    const Outcome o = run_child(dir.path().string(), name + ":" + std::to_string(kKillAt));
    REQUIRE(o.killed);
    check_resume(dir.path().string(), c.committed);
  }
}
