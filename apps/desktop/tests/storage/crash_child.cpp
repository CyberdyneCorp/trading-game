// OFF02 child process: plays a scripted session through the real EngineHost + storage and, when
// TRADELAB_FAULT=<point>:<n> is set, kills itself with SIGKILL the n-th time the fault point is reached
// (counted from the first scripted action). Test builds only: the app never installs a fault hook.
//   tradelab_desktop_crash_child <data dir> <actions>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "model/engine_host.hpp"
#include "model/storage/fault.hpp"
#include "storage/crash_script.hpp"

namespace td = tradelab::desktop;

namespace {

td::storage::FaultPoint g_point = td::storage::FaultPoint::BeforeCommit;
int g_remaining = 0;

void on_fault(td::storage::FaultPoint p) {
  if (p != g_point || --g_remaining > 0) return;
  ::kill(::getpid(), SIGKILL);
  // kill() may return before a multithreaded process is gone (seen on macOS): this worker must not go
  // on to the commit it was stopped in front of.
  for (;;) ::pause();
}

bool install_fault_hook() {
  const char* spec = std::getenv("TRADELAB_FAULT");
  if (spec == nullptr) return true;
  const std::string s(spec);
  const auto colon = s.find(':');
  const auto point = td::storage::fault_point_from(s.substr(0, colon));
  if (!point || colon == std::string::npos) return false;
  g_point = *point;
  g_remaining = std::atoi(s.c_str() + colon + 1);
  td::storage::set_fault_hook(on_fault);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <data dir> <actions>\n", argv[0]);
    return 2;
  }
  const int actions = std::atoi(argv[2]);
  td::EngineHost host([](td::EngineHost::Task) {}, [](std::shared_ptr<const td::Snapshot>) {},
                      std::make_unique<td::storage::Storage>(argv[1]));
  host.start(td::testing::kCrashMission, td::testing::kCrashSeed);
  host.wait_idle();
  if (!install_fault_hook()) return 2;
  for (int i = 0; i < actions; ++i) td::testing::send_scripted(host, i);
  host.wait_idle();
  return 0;
}
