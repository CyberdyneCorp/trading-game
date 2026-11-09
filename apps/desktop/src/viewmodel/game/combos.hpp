// Combos (port of the web's combos.ts, add-academy-and-strategies design D3): the builder's legs are
// ordinary logged orders tagged with a combo id. This module rebuilds the combos from the action log,
// follows their legs through the engine's events (filled, cancelled, incomplete) and groups the
// debrief's trades by combo. Replay ignores the tags. Parity fixtures: combos.json.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "model/json.hpp"
#include "viewmodel/game/strategies.hpp"

namespace tradelab::desktop::game {

// One submission of a combo (add-practice-and-advanced-strategies D3): its opening, a roll or an
// adjustment, placed at one decision step.
enum class SubmissionKind : std::uint8_t { Open, Roll, Adjust };
std::string_view submission_kind_code(SubmissionKind k);  // open | roll | adjust

struct ComboSubmission {
  SubmissionKind kind = SubmissionKind::Open;
  std::string n;  // the roll's or adjustment's number ("1", ...); "" for the opening
  int step = 0;
  std::vector<OrderLeg> legs;
  bool withdrawn = false;  // the legs were cancelled at placement (a leg was refused)
};

struct ComboRecord {
  std::string id;
  std::string strategy;  // a template code, its short version ("short_straddle", ...) or "custom"
  int step = 0;          // decision step the opening legs were placed at; they execute at the next open
  std::vector<OrderLeg> legs;  // the opening legs in submission order; series 0 = the perpetual
  bool withdrawn = false;      // the opening legs were cancelled at placement (a leg was refused)
  // The opening, then every roll and adjustment, in log order (one journal and debrief row). Empty in a
  // record built by hand: the opening alone.
  std::vector<ComboSubmission> submissions;
};

// The next roll or adjustment number of a combo ("1", "2", ...).
std::string next_submission_number(const ComboRecord& rec, SubmissionKind kind);

// The next free combo id ("c1", "c2", ...).
std::string next_combo_id(const std::vector<ComboRecord>& combos);

// Combos from an action log (web Action JSON, one per entry): tagged actions grouped by tag, each at the
// step it was placed (warm-up + the advances before it).
std::vector<ComboRecord> combos_from_log(const std::vector<std::string>& log, int start_step);

enum class LegState : std::uint8_t { Working, Filled, Cancelled };
std::string_view leg_state_code(LegState s);  // working | filled | cancelled

struct LegStatus {
  OrderLeg leg;
  std::optional<int> order_id;
  LegState state = LegState::Working;
  std::string reason;              // "" while none
  std::optional<std::string> price;  // fill price (premium per unit for options)
  std::optional<int> fill_step;
};

enum class ComboState : std::uint8_t { Working, Filled, Withdrawn, NotExecuted, Incomplete };
std::string_view combo_state_code(ComboState s);  // working | filled | withdrawn | not_executed | incomplete

struct ComboStatus {
  ComboRecord record;
  ComboState state = ComboState::Working;  // the latest submission's state
  std::vector<LegStatus> legs;             // the latest submission's legs
  std::vector<LegStatus> all_legs;         // every submission's legs, in order
};

// Order ids of a submission's legs: the last run of consecutive orders accepted at its step whose sides
// and quantities match the legs in order.
std::vector<std::optional<int>> combo_order_ids(const std::vector<EngineEvent>& events, int step, const std::vector<OrderLeg>& legs);
std::vector<std::optional<int>> combo_order_ids(const std::vector<EngineEvent>& events, const ComboRecord& rec);
ComboStatus combo_status(const std::vector<EngineEvent>& events, const ComboRecord& rec);

// The combo's live holdings (what a roll or an adjustment starts from): the filled legs of every
// submission netted per series (signed contracts, > 0 long), zero nets dropped.
std::vector<HeldLeg> combo_holdings(const ComboStatus& status);
// The order ids of the latest submission's legs still working ("Cancel working legs").
std::vector<int> working_legs(const ComboStatus& status);
// Option premium cash of the filled legs: credit > 0, debit < 0.
double net_premium(const ComboStatus& status, double multiplier);
// The legs a player still carries after an incomplete combo: the filled ones.
std::vector<LegStatus> open_legs(const ComboStatus& status);
// The contracts a combo's plan headline names: its first option leg's, else the first leg's.
std::string combo_contracts(const std::vector<OrderLeg>& orders);

// A debrief trade as combos need it (tl_debrief "trades").
struct ComboTrade {
  int n = 0;
  int series = 0;
  std::string side;  // long | short
  int entry_step = 0;
  double net = 0;
  std::optional<int> r_bp;
};
std::vector<ComboTrade> combo_trades_from(const json::Value& trades);

struct ComboTrades {
  ComboStatus status;
  std::vector<ComboTrade> trades;
  double net = 0;
  std::optional<double> r;  // the combo's net over its planned risk; nullopt without a plan
};

// The combo's R: one plan per leg splits the plan's risk evenly, so a leg's planned risk is its net over
// its R and the combo's risk is that share times the legs.
std::optional<double> combo_r(const std::vector<ComboTrade>& trades, double net);

struct GroupedTrades {
  std::vector<ComboTrades> combos;
  std::vector<ComboTrade> rest;
};
GroupedTrades group_trades(const std::vector<ComboTrade>& trades, const std::vector<ComboStatus>& statuses);

}  // namespace tradelab::desktop::game
