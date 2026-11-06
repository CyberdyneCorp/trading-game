// Logged player actions: the same JSON shapes as the web's `Action` union (apps/web/src/lib/game/
// session.svelte.ts), so a desktop log means what a web log means and can be synced unchanged later.
// Every command that changes a run is one Action: it is committed to the log, then applied with
// apply_action (live and on replay alike, so client ids and the state digest match).
//
//   {"k":"adv","n":N}  {"k":"order","type","side","qty","price","ro","stop","tp","trail","trailRate"}
//   {"k":"opt","series","side","type","qty","premium"}  {"k":"lev","lev"}  {"k":"margin","amount"}
//   {"k":"plan","side","skipped","option","lose","inv","target","conf","risk"}  {"k":"ans","pBp","skipped"}
//   {"k":"lesson","q","choice","value","skipped"}  {"k":"cancel","id"}  {"k":"move","id","price"}
//   {"k":"close"}  {"k":"advice"}
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/engine_run.hpp"
#include "model/game_command.hpp"
#include "model/json.hpp"

namespace tradelab::desktop::storage {

struct Action {
  std::string json;
};

struct OrderAction {
  std::string type = "market";  // market | limit | stop_market | take_profit | trailing_stop
  Side side = Side::Buy;
  std::string qty;
  std::optional<std::string> price;
  bool reduce_only = false;
  std::optional<std::string> stop;
  std::optional<std::string> take_profit;
  std::optional<std::string> trail;
  std::optional<std::string> trail_rate;
};

struct PlanAction {
  Side side = Side::Buy;
  bool skipped = false;
  bool option = false;
  bool lose = false;
  std::optional<std::string> invalidation;
  std::optional<std::string> target;
  int confidence = 0;
  std::optional<std::string> risk;
};

namespace actions {
Action advance(int steps);
Action market(Side side, const std::string& qty);
Action order(const OrderAction& o);
Action option(std::uint32_t series, Side side, bool limit, const std::string& qty, const std::optional<std::string>& premium);
Action leverage(int leverage);
Action margin(const std::string& amount);
Action plan(const PlanAction& p);
Action answer(int p_bp, bool skipped);
Action lesson(std::uint32_t question, std::uint32_t choice, const std::optional<std::string>& value, bool skipped);
Action cancel(std::uint32_t order_id);
Action move(std::uint32_t order_id, const std::string& price);
Action close();
Action advice();
// A terminal command as the web logs it (an empty decimal string = absent).
Action from_game(const GameCommand& c);
}  // namespace actions

struct ApplyResult {
  bool known = true;     // false: not an action this build understands (nothing was applied)
  bool advance = false;  // an "adv" action (no client id, no command result)
  std::string result;    // the engine's command result JSON ({"accepted","reason",...}); empty for adv
  bool accepted = false;
  std::string reason;
};

// Applies one logged action to the run. Non-advance actions take the run's next client id.
ApplyResult apply_action(EngineRun& run, const json::Value& action);
ApplyResult apply_action(EngineRun& run, const Action& action);

// True for {"k":"adv"} (they take no client id).
bool is_advance(const json::Value& action);

// Assisted practice (web assisted.ts rewindPrefix): the prefix of a log that brings a run from
// `start_step` back to the decision at `to_step`, before anything was done there. Commands at earlier
// steps are kept, an advance that crosses `to_step` is shortened to end on it, nothing after it is kept
// except lesson answers (given in the briefing, so every rewind keeps them).
std::vector<std::string> rewind_prefix(const std::vector<std::string>& log, int start_step, int to_step);

}  // namespace tradelab::desktop::storage
