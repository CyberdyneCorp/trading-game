// Terminal commands sent to the engine worker (CommandKind::Game): one per C ABI call the web's
// session.svelte.ts makes (place / trailing rate / option / cancel / move / leverage / close / margin /
// plan / calibration answer / lesson answer / advice). Values are decimal strings already snapped to
// the tick or lot by the viewmodel; the engine validates everything again. The worker turns each into a
// logged storage::Action (storage::actions::from_game) so it is committed and replayable.
#pragma once

#include <cstdint>
#include <string>

#include "model/snapshot.hpp"

namespace tradelab::desktop {

// Engine order type codes (c_api.h).
enum class OrderType : std::uint8_t { Market = 0, Limit = 1, StopMarket = 2, TakeProfit = 3, TrailingStop = 4 };

struct GameCommand {
  GameCommandKind kind = GameCommandKind::None;
  int type = 0;      // OrderType (orders) or 0 market / 1 limit (options)
  bool buy = true;   // side
  std::string qty;
  std::string price;  // limit / trigger price, option premium; "" = none
  bool reduce_only = false;
  std::string stop;   // bracket stop (orders); invalidation (plan)
  std::string tp;     // bracket target (orders); target (plan)
  std::string trail;  // trailing distance (Order) or rate (TrailingRate)
  int id = 0;         // order id (cancel, move), series (option), question (lesson)
  int number = 0;     // leverage, confidence %, probability bp, choice
  std::string amount;  // margin delta, planned risk, numeric lesson answer
  bool skipped = false;
  bool option = false;         // plan for an option order
  bool lose_scenario = false;  // plan wrote an option lose scenario
  // A strategy-builder leg (or the cancel of a withdrawn combo): its combo id ("c1") and structure name
  // ("iron_condor"), logged with the action and ignored by replay (design D3). "" = not a combo leg.
  std::string combo;
  std::string strategy;

  static GameCommand order(OrderType type, bool buy, std::string qty, std::string price, bool reduce_only,
                           std::string stop = {}, std::string tp = {}, std::string trail = {});
  static GameCommand trailing_rate(bool buy, std::string qty, std::string rate);
  static GameCommand option_order(int series, bool buy, bool limit, std::string qty, std::string premium);
  static GameCommand cancel(int order_id);
  static GameCommand move(int order_id, std::string price);
  static GameCommand leverage(int leverage);
  static GameCommand close();
  static GameCommand margin(std::string amount);
  static GameCommand plan(bool buy, bool skipped, bool option, bool lose_scenario, std::string invalidation,
                          std::string target, int confidence, std::string planned_risk);
  static GameCommand answer(int p_bp, bool skipped);
  static GameCommand lesson(int question, int choice, std::string value, bool skipped);
  static GameCommand advice();
  // This command tagged as a leg of `combo` (structure `strategy`).
  [[nodiscard]] GameCommand tagged(std::string combo_id, std::string strategy_name) const;
};

// The engine's command result JSON ({"accepted","reason","orderId","warning"}) as a CommandResult.
CommandResult game_result(GameCommandKind kind, const std::string& result_json);

}  // namespace tradelab::desktop
