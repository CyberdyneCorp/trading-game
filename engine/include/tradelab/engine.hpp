#pragma once

// Public engine API: start a run, apply player commands at a decision point, advance one bar.

#include <cstdint>

#include "tradelab/scenario.hpp"
#include "tradelab/sha256.hpp"
#include "tradelab/state.hpp"

namespace tradelab {

constexpr std::uint32_t kExecutionPolicyVersion = 1;

enum class CommandType : std::uint8_t {
  PlaceOrder = 0,
  CancelOrder = 1,
  SetLeverage = 2,
  ClosePosition = 3,
};

struct Command {
  std::uint64_t seq = 0;            // strictly increasing per run
  std::uint32_t decision_step = 0;  // must equal the run's current step
  std::uint64_t client_id = 0;      // idempotency key
  CommandType type = CommandType::PlaceOrder;
  OrderType order_type = OrderType::Market;
  OrderSide side = OrderSide::Buy;
  Qty qty;
  Price price;
  bool reduce_only = false;
  std::uint32_t order_id = 0;  // CancelOrder
  std::int32_t leverage = 1;   // SetLeverage
};

// Live account figures at the current decision point (all derived, never stored).
struct AccountView {
  Price mark;                 // last close
  Money unrealized;
  Money equity;               // wallet + unrealized
  Money position_margin;
  Money order_margin;         // reserved by resting orders that would increase exposure
  Money available;            // wallet - position margin - order margin, floored at 0
  Money notional;             // |position| at mark
  bool has_liquidation = false;
  Price liquidation_price;
  Rate next_funding_rate;     // published rate for the next funding boundary
  std::uint32_t next_funding_step = 0;
};

RunState start_run(const Scenario& sc, std::uint64_t seed);
CommandResult apply_command(const Scenario& sc, RunState& st, const Command& cmd);
void advance(const Scenario& sc, RunState& st);

AccountView account_view(const Scenario& sc, const RunState& st);
std::uint32_t last_decision_step(const Scenario& sc);

Digest state_digest(const RunState& st);
Digest events_digest(const RunState& st);

}  // namespace tradelab
