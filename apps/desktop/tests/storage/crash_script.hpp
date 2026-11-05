// The OFF02 session, shared by the crash child (host commands) and the test (expected digests).
#pragma once

#include <cstdint>
#include <vector>

#include "model/engine_host.hpp"
#include "model/storage/actions.hpp"

namespace tradelab::desktop::testing {

constexpr const char* kCrashMission = "m01-false-breakout";
constexpr std::uint32_t kCrashSeed = 9001;

// Action i of the script: buy, advance, sell, advance 2, ... (fills on most decision steps).
inline storage::Action scripted_action(int i) {
  switch (i % 4) {
    case 0: return storage::actions::market(Side::Buy, "2");
    case 2: return storage::actions::market(Side::Sell, "1");
    case 3: return storage::actions::advance(2);
    default: return storage::actions::advance(1);
  }
}

inline void send_scripted(EngineHost& host, int i) {
  switch (i % 4) {
    case 0: host.market(Side::Buy, "2"); break;
    case 2: host.market(Side::Sell, "1"); break;
    case 3: host.advance(2); break;
    default: host.advance(1); break;
  }
}

inline std::vector<storage::Action> crash_script(int n) {
  std::vector<storage::Action> out;
  for (int i = 0; i < n; ++i) out.push_back(scripted_action(i));
  return out;
}

}  // namespace tradelab::desktop::testing
