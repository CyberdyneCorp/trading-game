#pragma once

// Public engine API: start a run, apply player commands at a decision point, advance one bar.

#include <cstdint>

#include "tradelab/scenario.hpp"
#include "tradelab/sha256.hpp"
#include "tradelab/state.hpp"

namespace tradelab {

// Engine release, checked against a pack's engineCompatibility range.
inline constexpr const char* kEngineVersion = "0.1.0";

// Version of the C ABI (include/tradelab/c_api.h: function names, signatures and the JSON shapes
// clients read), returned by tl_abi_version. A client drives only a build whose ABI it speaks: the web
// worker refuses to resume a run on a pinned build with another ABI, the desktop a run recorded under
// another one. Bump it when a change breaks a caller of an existing function; additions do not.
// Builds older than tl_abi_version have no number (clients treat them as ABI 0).
constexpr std::uint32_t kAbiVersion = 1;

constexpr std::uint32_t kExecutionPolicyVersion = 17;  // v2 brackets/OCO, trailing; v3 options; v4 plans/answers; v5 generator v2; v6 fill-before-orphan-cancel event order; v7 reduce-only market remainder keeps working, m05 jump before first expiry; v8 smoothed/capped generator IV, term-structure floor; v9 funding uses the last closed bar's segment; v10 news priced on arrival (opening gap), per-seed mission schedule; v11 start options (advice allowance, perks) and RequestAdvice; v12 campaign start options (starting balance, leverage cap); v13 MoveOrder (a working exit moved in place, OCO link kept); v14 an exit that closes the position cancels its own remainder on the same bar; v15 a stop cut short by capacity executes its remainder at the next opens (it is a market order once triggered); v16 capacity served by price then sequence within each phase, percentage trailing stops, maintenance-margin tiers by notional; v17 mark-price bars for margin, liquidation and funding (markFromLastPrice without them), option settlement window in market time (sub-bar TWAP, series_settling), continuity bars (no trading, indicator gap policy)

// Calibration questions (spec mission-scoring "Calibration"): asked every kQuestionEvery steps from
// the first decision step while the resolving bar exists; resolved kQuestionHorizon bars later.
constexpr std::uint32_t kQuestionEvery = 15;
constexpr std::uint32_t kQuestionHorizon = 5;

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
  Price trail_distance;        // TrailingStop: fixed distance from the best close
  Rate trail_rate;             // TrailingStop (policy v16): or a rate of it, e.g. 0.02 = 2% (one of the two)
  std::uint32_t series = 0;    // PlaceOptionOrder
  Money margin_delta;          // AdjustMargin
  PlanRecord plan;             // RecordPlan (step is set by the engine)
  std::uint32_t p_bp = 0;      // AnswerQuestion: probability in basis points
  bool skipped = false;        // AnswerQuestion / AnswerLesson skipped
  std::uint32_t question = 0;  // AnswerLesson: index into the mission's rubric
  std::uint32_t choice = 0;    // AnswerLesson: multiple-choice option
  Money value;                 // AnswerLesson: numeric answer
};

// Live account figures at the current decision point (all derived, never stored).
struct AccountView {
  Price mark;                 // mark close (policy v17), the last close without a mark series
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
// What the player did at the current decision step (recorded by advance in RunState::decisions).
DecisionKind decision_at(const RunState& st);

AccountView account_view(const Scenario& sc, const RunState& st);

// True when a calibration question is due at the current step and has not been answered yet.
bool question_due(const Scenario& sc, const RunState& st);
std::uint32_t last_decision_step(const Scenario& sc);

Digest state_digest(const RunState& st);
Digest events_digest(const RunState& st);

}  // namespace tradelab
