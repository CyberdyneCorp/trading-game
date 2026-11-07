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

struct ComboRecord {
  std::string id;
  std::string strategy;  // a template code, "short_straddle", "short_strangle" or "custom"
  int step = 0;          // decision step the legs were placed at; they execute at the next open
  std::vector<OrderLeg> legs;  // in submission order; series 0 = the perpetual
  bool withdrawn = false;      // the legs were cancelled at placement (a leg was refused)
};

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
  ComboState state = ComboState::Working;
  std::vector<LegStatus> legs;
};

// Order ids of a combo's legs: the last run of consecutive orders accepted at the combo's step whose
// sides and quantities match the legs in order.
std::vector<std::optional<int>> combo_order_ids(const std::vector<EngineEvent>& events, const ComboRecord& rec);
ComboStatus combo_status(const std::vector<EngineEvent>& events, const ComboRecord& rec);
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
