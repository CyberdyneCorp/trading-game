#include "model/storage/fault.hpp"

#include <array>
#include <atomic>

namespace tradelab::desktop::storage {

namespace {

std::atomic<FaultHook> g_hook{nullptr};

constexpr std::array<std::string_view, 4> kNames = {"before-commit", "mid-commit", "after-commit", "after-apply"};

}  // namespace

void set_fault_hook(FaultHook hook) { g_hook.store(hook); }

void fault(FaultPoint point) {
  if (FaultHook hook = g_hook.load(); hook != nullptr) hook(point);
}

std::optional<FaultPoint> fault_point_from(std::string_view name) {
  for (std::size_t i = 0; i < kNames.size(); ++i) {
    if (kNames[i] == name) return static_cast<FaultPoint>(i);
  }
  return std::nullopt;
}

std::string_view fault_point_name(FaultPoint point) { return kNames.at(static_cast<std::size_t>(point)); }

}  // namespace tradelab::desktop::storage
