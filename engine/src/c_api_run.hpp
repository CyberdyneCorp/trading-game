#pragma once

// The run handle behind the C ABI, shared by c_api.cpp (missions, commands, state) and
// c_api_pack.cpp (packs and CSV import).

#include <string>

#include "tradelab/c_api.h"
#include "tradelab/pack.hpp"
#include "tradelab/state.hpp"

struct tl_run {  // NOLINT(readability-identifier-naming): C ABI type
  tradelab::Scenario scenario;
  tradelab::RunState state;
  std::uint64_t next_seq = 1;
  std::string buffer;
  bool from_pack = false;
  tradelab::PackInfo pack;  // reveal metadata is only written by tl_debrief
};

namespace tradelab::detail {

// Restores a hex checkpoint into a freshly created run; false when damaged or for another scenario.
bool apply_checkpoint(tl_run& run, const char* checkpoint_hex);

}  // namespace tradelab::detail
