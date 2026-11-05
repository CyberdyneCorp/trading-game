// Immutable views of the engine state handed from the simulation worker to the UI thread.
// Amounts stay engine decimal strings; bars carry doubles for drawing only.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/json.hpp"

namespace tradelab::desktop {

struct MissionInfo {
  std::string id;
  int total_bars = 0;
  int warmup_bars = 0;
  int leverage_cap = 0;
  bool allow_short = true;
};

struct Bar {
  double open = 0;
  double high = 0;
  double low = 0;
  double close = 0;
  double volume = 0;
};

enum class CommandKind : std::uint8_t { Start, Advance, Market };

struct CommandResult {
  CommandKind kind = CommandKind::Start;
  bool accepted = true;
  std::string reason;  // engine reason code ("none" when accepted)
};

struct Debrief {
  std::string final_equity;
  std::string net_pnl;
  std::string buy_and_hold_pnl;
  int score_total = -1;  // -1: no score
  std::string lesson;
  int entries = 0;
  json::Value raw;     // the whole tl_debrief document (debrief screen: findings, trades, Greeks...)
  json::Value reveal;  // tl_mission_reveal of a built-in mission (regimes, releases); null otherwise
};

// Engine start options of a run (tl_run_create_ex): a campaign mission carries its capital and the
// curriculum's leverage gate. Empty / 0 = the mission's own.
struct StartOptions {
  std::string starting_balance;
  int leverage_cap = 0;
};

struct Snapshot {
  std::uint64_t seq = 0;  // increases with every published snapshot of a host
  std::string mission;
  std::string alias;
  std::string status;  // "active", "finished", "bankrupt"
  std::uint32_t seed = 0;
  int step = 0;
  int warmup_bars = 0;
  int total_bars = 0;
  int last_decision_step = 0;
  int step_seconds = 0;
  std::string initial_balance;
  int leverage = 1;
  int leverage_cap = 1;
  bool allow_short = true;
  std::string tick_size;
  std::string lot_size;
  std::string min_qty;
  std::vector<Bar> bars;

  std::string equity;
  std::string wallet;
  std::string available;
  std::string unrealized;
  std::string mark;
  std::optional<std::string> liquidation_price;
  std::string position_qty;  // signed: > 0 long, < 0 short
  std::string position_entry;
  std::string digest;

  // Commands acknowledged since the previous delivered snapshot (merged when snapshots coalesce).
  std::vector<CommandResult> results;
  int progress_done = 0;  // batch advance: steps done of progress_total
  int progress_total = 0;
  std::optional<Debrief> debrief;
  json::Value state;  // the whole tl_state document the fields above were read from

  [[nodiscard]] bool active() const { return status == "active"; }
};

}  // namespace tradelab::desktop
