// Immutable views of the engine state handed from the simulation worker to the UI thread.
// Amounts stay engine decimal strings; bars carry doubles for drawing only.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/game_state.hpp"
#include "model/json.hpp"

namespace tradelab::desktop {

// One locale's entry of a pack's mission texts (tl_pack_validate "pack.texts").
struct PackText {
  std::string locale;  // "pt-BR" or "en"
  std::string title;
  std::string briefing;
};

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

// Watch: the replay viewer opened a run file (add-run-sharing-and-journal-stats D6).
enum class CommandKind : std::uint8_t { Start, Advance, Market, Game, Resume, StartPack, Rewind, Watch };

// Report-only engine calls (design D4 of add-academy-and-strategies): never logged, never change a run.
// WhatIf is run-less (tl_option_whatif, the Academy lab); RunWhatIf (tl_run_option_whatif) and
// ComboPreview (tl_combo_preview) read the run in play.
enum class AnalysisKind : std::uint8_t { WhatIf, RunWhatIf, ComboPreview };

// Terminal commands (CommandKind::Game), one per engine call of the C ABI (model/game_command.hpp).
enum class GameCommandKind : std::uint8_t {
  None,
  Order,
  TrailingRate,
  Option,
  Cancel,
  Move,
  Leverage,
  Close,
  Margin,
  Plan,
  Answer,
  Lesson,
  Advice,
  LinkOco  // policy v18: two working orders linked one-cancels-the-other
};

struct CommandResult {
  CommandResult() = default;
  CommandResult(CommandKind k, bool ok, std::string why) : kind(k), accepted(ok), reason(std::move(why)) {}

  CommandKind kind = CommandKind::Start;
  bool accepted = true;
  std::string reason;  // engine reason code ("none" when accepted)
  GameCommandKind game = GameCommandKind::None;
  int order_id = 0;
  std::string warning;  // accepted with a warning (e.g. "stop_beyond_liquidation"), else ""
};

struct Debrief {
  std::string final_equity;
  std::string net_pnl;
  std::string buy_and_hold_pnl;
  int score_total = -1;  // -1: no score
  std::string lesson;
  int entries = 0;
  int advisor_points_earned = 0;  // settles the device's advisor balance (web points.ts)
  json::Value raw;     // the whole tl_debrief document (debrief screen: findings, trades, Greeks...)
  json::Value reveal;  // tl_mission_reveal of a built-in mission (regimes, releases); null otherwise
};

// Engine start options of a run (tl_run_create_ex): advisor allowance and intel perks (engine policy
// v11), and for a campaign mission its capital and the curriculum's leverage gate. Zero / empty = the
// mission's own; default options start the same run as tl_run_create.
struct RunOptions {
  int advice_allowance = 0;
  int news_lead = 0;
  bool calendar = false;
  std::string starting_balance;
  int leverage_cap = 0;

  bool operator==(const RunOptions&) const = default;
};

// Links stored on a new run's record (the web's RunRecord.campaign / .assisted), as their JSON
// objects: {"id","index"} for a campaign mission, {"from","kind","step"} for an assisted attempt.
// Empty = none.
struct RunLinks {
  std::string campaign;
  std::string assisted;
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
  // The full terminal state (orders, options, indicators, news, advisor...), shared and immutable.
  std::shared_ptr<const GameState> game;
  json::Value state;  // the whole tl_state document the fields above were read from

  // Storage (model/storage): the snapshot answering a resume carries its notice ("engine_changed",
  // "policy_changed", "digest_mismatch", "pack_missing", "damaged"; "" = none) and detail.
  std::string resume_notice;
  std::string resume_detail;
  // A schema-v2 pack's own mission texts (mission.json, checked by the engine; one per locale), shown
  // in place of the generic pack texts. Empty for missions and packs without them.
  std::vector<PackText> pack_texts;
  // The stored run this state belongs to ("" without storage) and whether it is an assisted attempt.
  std::string run_id;
  bool assisted = false;
  bool durable = true;  // false once a storage write failed: play goes on but may not resume
  // The run's logged actions so far (the web's session log), shared and immutable: the strategy
  // builder rebuilds its combos from their tags (combos.hpp). Null before a run.
  std::shared_ptr<const std::vector<std::string>> log;
  // The replay viewer (design D6): a run rebuilt from a run file, never stored. `watch_bar` bars
  // played of the `watch_bars` its log advances; `watch_done` once the whole log is applied.
  bool watching = false;
  int watch_bar = 0;
  int watch_bars = 0;
  bool watch_done = false;

  [[nodiscard]] bool active() const { return status == "active"; }
};

}  // namespace tradelab::desktop
