#pragma once

// Mutable run state. Everything here is covered by the state digest.

#include <cstdint>
#include <deque>
#include <vector>

#include "tradelab/fixed.hpp"
#include "tradelab/prng.hpp"
#include "tradelab/types.hpp"

namespace tradelab {

struct Order {
  std::uint32_t id = 0;
  std::uint64_t seq = 0;  // command sequence that created it (priority)
  OrderType type = OrderType::Market;
  OrderSide side = OrderSide::Buy;
  Qty qty;
  Qty filled;
  Price price;  // limit price or stop trigger; unused for market
  bool reduce_only = false;
  std::uint32_t placed_step = 0;
  OrderStatus status = OrderStatus::Accepted;
  // Bracket: an entry carrying exits; children are created per fill (reduce-only, one OCO group).
  Price bracket_stop;
  Price bracket_tp;
  std::uint32_t parent_id = 0;  // child of a bracket entry
  std::uint32_t oco_group = 0;  // siblings share it; a fill on one reduces the others
  Price trail_distance;         // TrailingStop: distance from the best close
  std::uint32_t series = 0;     // 0 = perpetual; otherwise an option series id
  Rate trail_rate;              // TrailingStop (policy v16): distance as a rate of the best close; 0 = fixed
  // Policy v18 (design D6, D7 of add-practice-and-advanced-strategies).
  TimeInForce tif = TimeInForce::Gtc;
  std::uint32_t tif_bars = 0;     // N of a bars time in force
  std::uint32_t expire_step = 0;  // last bar it may execute on (bars: placed + N; ioc/fok: placed + 1); 0 = none
  std::uint32_t link_group = 0;   // player-built OCO pair: the lower order id; 0 = not linked (apart from oco_group)

  [[nodiscard]] bool active() const {
    return status == OrderStatus::Accepted || status == OrderStatus::PartiallyFilled ||
           status == OrderStatus::Triggered;
  }
  [[nodiscard]] Qty remaining() const { return Qty::from_raw(qty.raw - filled.raw); }
};

struct Position {
  Qty qty;        // signed: > 0 long, < 0 short
  Price entry;    // average entry price
  Money margin;   // isolated margin
  [[nodiscard]] bool flat() const { return qty.raw == 0; }
  [[nodiscard]] bool is_long() const { return qty.raw > 0; }
};

struct OptionPosition {
  std::uint32_t series = 0;
  Qty qty;              // signed: > 0 long, < 0 short (contracts)
  Price avg_premium;    // average open premium per unit
};

struct Event {
  std::uint64_t seq = 0;
  std::uint32_t step = 0;
  EventType type = EventType::OrderUpdate;
  std::uint32_t order_id = 0;
  OrderStatus status = OrderStatus::Accepted;
  Reason reason = Reason::None;
  std::uint8_t flags = kFlagNone;
  OrderSide side = OrderSide::Buy;
  Qty qty;          // fill quantity / position size
  Price price;      // fill price / trigger
  Price requested;  // requested limit or trigger price (debrief: requested vs executed)
  Money amount;     // fee, funding, realized P&L or margin lost depending on type
  Money pnl;        // realized P&L of a fill (before fees)
  std::uint32_t series = 0;  // option series id (0 = perpetual)
};

struct Metrics {
  Money peak_equity;
  Money max_drawdown;     // largest peak-to-trough equity drop
  std::int64_t peak_leverage_bp = 0;  // notional / equity in basis points
  std::int64_t min_liq_distance_bp = -1;  // closest liquidation distance seen, -1 = never in position
  std::uint32_t liquidations = 0;
  std::uint32_t fills = 0;
  std::uint32_t ambiguous_fills = 0;
  Money turnover;           // sum of fill notionals
};

// Structured plan recorded before an exposure-increasing order (spec gameplay-loop "Plan before
// advance"). Free text never reaches the engine: it is not scored (spec mission-scoring).
struct PlanRecord {
  std::uint32_t step = 0;
  OrderSide side = OrderSide::Buy;
  bool skipped = false;
  bool option = false;
  bool has_lose_scenario = false;
  Price invalidation;  // 0 = none
  Price target;        // 0 = none
  std::uint32_t confidence_pct = 0;
  Money planned_risk;  // 0 = unknown
};

// Calibration answer: probability (basis points) that the close `horizon` bars after `step` ends
// above `threshold` (the close at `step`). Skipped answers are kept but not scored.
struct CalibrationAnswer {
  std::uint32_t step = 0;
  Price threshold;
  std::uint32_t horizon = 0;
  std::uint32_t p_bp = 0;
  bool skipped = false;
};

// What the player did at a decision point, recorded by `advance` for the step it leaves (task 4.2,
// spec gameplay-loop "Waiting is a decision"). A pure function of the commands and the state at that
// step, so it is kept outside the state digest (like the command log) and in the checkpoint.
// DecisionKind (wait, hold, act) is declared in the IDL (idl_types.hpp).
struct DecisionRecord {
  std::uint32_t step = 0;
  DecisionKind kind = DecisionKind::Wait;
};

// Answer to a lesson question (task 4.5): `question` indexes the mission's rubric (lessons.hpp);
// `choice` for multiple choice, `value` for a numeric answer. Skipped answers are kept and score 0.
struct LessonAnswer {
  std::uint32_t step = 0;
  std::uint32_t question = 0;
  std::uint32_t choice = 0;
  Money value;
  bool skipped = false;
};

struct CommandResult {
  bool accepted = false;
  Reason reason = Reason::None;
  std::uint32_t order_id = 0;
  Reason warning = Reason::None;  // e.g. StopBeyondLiquidation on an accepted order
};

// Start options of a run (spec progression-rewards): advice allowance and intel perks; policy v12
// adds the campaign's carried capital and the curriculum's leverage gate (spec curriculum). The
// defaults are a plain run with the mission's own rules, without advisor or perks.
struct RunOptions {
  std::uint32_t advice_allowance = 0;
  std::uint8_t news_lead = 0;  // fast wire: headlines visible this many bars before release
  bool calendar = false;       // economic calendar: upcoming scheduled releases (step only)
  Money starting_balance;      // > 0: campaign capital carried in, replaces the mission's initial balance
  std::int32_t leverage_cap = 0;  // > 0: lowers the mission's leverage cap (curriculum unlocks)
};

// One reason behind a piece of advice. `vote` is its signed weight (+ long, - short); 0 means it
// voted for neither side and pushed the advice toward wait. `ref` points at what it read: the
// pattern's or the headline's step, or the crowded side (+1 longs, -1 shorts) for funding.
struct AdviceReason {
  AdviceReasonCode code = AdviceReasonCode::RegimeRange;
  std::int8_t vote = 0;
  std::int32_t ref = 0;
};

struct Advice {
  std::uint32_t step = 0;
  AdviceStance stance = AdviceStance::Wait;
  std::uint32_t confidence_bp = 5000;
  Price stop;  // 0 when waiting
  Qty size;    // 0 when waiting or below the minimum order
  std::vector<AdviceReason> reasons;
};

struct CommandRecord {
  std::uint64_t client_id = 0;
  CommandResult result;
};

struct RunState {
  std::uint64_t seed = 0;
  std::uint32_t step = 0;  // index of the last closed (visible) bar = current decision step
  RunStatus status = RunStatus::Active;
  std::int32_t leverage = 1;

  Money wallet;  // includes isolated margin; changes only by realized P&L, fees, funding, liquidation
  Position position;
  // deque: matching may append bracket children while holding references to other orders.
  std::deque<Order> orders;
  std::uint32_t next_order_id = 1;
  std::uint64_t next_event_seq = 1;
  std::uint64_t last_command_seq = 0;

  Money realized_pnl;
  Money fees_paid;
  Money funding_net;        // > 0 received
  Money insurance_absorbed;  // shortfall beyond isolated margin absorbed by the simulated fund
  std::vector<OptionPosition> option_positions;
  Money option_cash;      // net option premiums and settlements credited to the wallet (excl. fees)
  Money option_realized;  // realized option P&L (premium differences and settlement vs. average)
  Metrics metrics;

  std::vector<PlanRecord> plans;
  std::vector<CalibrationAnswer> answers;
  std::vector<Event> events;  // full event log of the run
  std::vector<CommandRecord> command_log;  // idempotency: client id -> original outcome
  Pcg32 rng{0, Stream::Slippage};

  RunOptions options;
  std::uint32_t advice_left = 0;  // starts at options.advice_allowance
  std::vector<Advice> advice;     // every accepted RequestAdvice, in order

  std::vector<LessonAnswer> lesson_answers;  // in the state digest only when present (a run without answers hashes as before)
  std::vector<DecisionRecord> decisions;     // one per advanced decision step; not a digest input (derived)
  // Policy v18: the decision step of the last accepted LinkOco (a link emits no event, yet the step's
  // decision is `act`). Checkpointed, not hashed, like the decision records it feeds. -1 = none.
  std::int64_t last_link_step = -1;
};

}  // namespace tradelab
