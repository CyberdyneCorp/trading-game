#pragma once

// Public engine API: start a run, apply player commands at a decision point, advance one bar.

#include <cstdint>

#include "tradelab/scenario.hpp"
#include "tradelab/sha256.hpp"
#include "tradelab/state.hpp"

namespace tradelab {

// Engine release, checked against a pack's engineCompatibility range.
inline constexpr const char* kEngineVersion = "0.1.0";

constexpr std::uint32_t kExecutionPolicyVersion = 12;  // v2 brackets/OCO, trailing; v3 options; v4 plans/answers; v5 generator v2; v6 fill-before-orphan-cancel event order; v7 reduce-only market remainder keeps working, m05 jump before first expiry; v8 smoothed/capped generator IV, term-structure floor; v9 funding uses the last closed bar's segment; v10 news priced on arrival (opening gap), per-seed mission schedule; v11 start options (advice allowance, perks) and RequestAdvice; v12 campaign start options (starting balance, leverage cap)

// Calibration questions (spec mission-scoring "Calibration"): asked every kQuestionEvery steps from
// the first decision step while the resolving bar exists; resolved kQuestionHorizon bars later.
constexpr std::uint32_t kQuestionEvery = 15;
constexpr std::uint32_t kQuestionHorizon = 5;

enum class CommandType : std::uint8_t {
  PlaceOrder = 0,
  CancelOrder = 1,
  SetLeverage = 2,
  ClosePosition = 3,
  PlaceOptionOrder = 4,  // order_type Market or Limit; price = premium limit; series required
  AdjustMargin = 5,      // add (> 0) or remove (< 0) isolated margin on the open perpetual position
  RecordPlan = 6,        // structured plan for the next exposure-increasing order
  AnswerQuestion = 7,    // calibration answer for the question due at this step
  RequestAdvice = 8,     // advisor read of the visible market; uses one unit of the allowance
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
  Price attach_stop;           // PlaceOrder bracket: protective stop for the filled quantity
  Price attach_tp;             // PlaceOrder bracket: take-profit for the filled quantity
  Price trail_distance;        // TrailingStop
  std::uint32_t series = 0;    // PlaceOptionOrder
  Money margin_delta;          // AdjustMargin
  PlanRecord plan;             // RecordPlan (step is set by the engine)
  std::uint32_t p_bp = 0;      // AnswerQuestion: probability in basis points
  bool skipped = false;        // AnswerQuestion skipped
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
  Money option_long_value;    // long options marked at bid
  Money option_short_value;   // short options marked at ask (liability)
  Money option_initial_margin;
  Money option_maintenance_margin;
};

RunState start_run(const Scenario& sc, std::uint64_t seed, const RunOptions& options = {});

// The run's own rules: the mission's, as changed by the start options (campaign capital, leverage gate).
Money run_initial_balance(const Scenario& sc, const RunState& st);
std::int32_t run_leverage_cap(const Scenario& sc, const RunState& st);
CommandResult apply_command(const Scenario& sc, RunState& st, const Command& cmd);
void advance(const Scenario& sc, RunState& st);

AccountView account_view(const Scenario& sc, const RunState& st);

// True when a calibration question is due at the current step and has not been answered yet.
bool question_due(const Scenario& sc, const RunState& st);
std::uint32_t last_decision_step(const Scenario& sc);

Digest state_digest(const RunState& st);
Digest events_digest(const RunState& st);

}  // namespace tradelab
