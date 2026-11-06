// Fault injection for the crash-recovery test (OFF02). The app never installs a hook, so fault() is a
// no-op there; the test child process (tests/storage/crash_child.cpp) installs one that reads
// TRADELAB_FAULT=<point>:<n> and kills the process at the n-th time the point is reached.
//
// The points of one command, in order (commit-before-engine):
//   before-commit  the action is built, nothing written
//   mid-commit     the action row is written inside the open transaction, COMMIT not yet run
//   after-commit   the transaction committed, the engine has not seen the action
//   after-apply    the engine applied it, the result is not acknowledged to the UI yet
#pragma once

#include <optional>
#include <string_view>

namespace tradelab::desktop::storage {

enum class FaultPoint : unsigned char { BeforeCommit, MidCommit, AfterCommit, AfterApply };

using FaultHook = void (*)(FaultPoint);

void set_fault_hook(FaultHook hook);
void fault(FaultPoint point);

std::optional<FaultPoint> fault_point_from(std::string_view name);
std::string_view fault_point_name(FaultPoint point);

}  // namespace tradelab::desktop::storage
