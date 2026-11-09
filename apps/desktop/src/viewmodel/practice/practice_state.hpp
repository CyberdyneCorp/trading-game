// State of the practice formats kept by AppViewModel (add-practice-and-advanced-strategies D15-D19): the
// drill session, the guess round, the coach review of the finished run and a mistake retry's report.
// Plain data; the rules are the ports in viewmodel/practice and the flow in viewmodel/practice_flow.cpp.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "model/json.hpp"
#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/practice/coach_review.hpp"
#include "viewmodel/practice/daily.hpp"
#include "viewmodel/practice/drills.hpp"
#include "viewmodel/practice/guess_round.hpp"

namespace tradelab::desktop {

// DrillScreen.svelte: intro, the items one by one, the end screen.
struct DrillsState {
  enum class Phase : std::uint8_t { Intro, Asking, Done };
  enum class Feedback : std::uint8_t { None, Right, Wrong, Invalid };
  Phase phase = Phase::Intro;
  std::vector<practice::DrillItem> items;
  std::vector<std::optional<practice::ItemResult>> results;
  int index = 0;
  std::string typed;
  Feedback feedback = Feedback::None;
  std::int64_t started_ms = 0;  // the soft clock's start (shown only with Settings::drill_timer)
  int shown_seconds = -1;       // the clock's last shown value (a new second redraws)
  bool time_up = false;
  int lesson = 0;  // the end screen's lesson row picked
};

// GuessScreen.svelte: a round of a round market, from its own engine run (AnalysisKind::Round).
struct GuessOutcome {
  practice::GuessScore score;
  std::string hit;  // invalidation | target | both | neither
  bool higher = false;
  std::string from;  // the decision close and the close 12 bars later (engine decimals)
  std::string to;
};

struct GuessState {
  enum class Phase : std::uint8_t { Intro, Loading, Commit, Reveal, Error };
  Phase phase = Phase::Intro;
  std::optional<practice::RoundPick> pick;
  json::Value at_decision_raw;  // tl_state at the decision: its decimal strings feed the score
  std::shared_ptr<const GameState> at_decision;
  std::shared_ptr<const GameState> revealed;
  int decision = 0;  // 0 long, 1 short, 2 no trade
  int p = 50;
  std::string invalidation;
  std::string target;
  bool problem = false;  // a trade without both levels
  std::optional<GuessOutcome> outcome;
  game::ChartViewModel chart;
  std::uint64_t request = 0;  // the reply that still counts
};

// CoachReview.svelte: the finished run's moments, loaded once per run.
struct CoachReviewState {
  bool loading = false;
  std::optional<practice::CoachReview> review;
  std::string run_id;  // the run the review belongs to
  int index = 0;       // moments.size() = the end screen
  game::ChartViewModel chart;
};

// MistakeReport.svelte: a mistake retry against its original attempt (D18), at the retry's debrief.
struct MistakeResult {
  std::optional<std::int64_t> score;
  std::string pnl;  // an engine decimal
};
struct MistakeReport {
  std::string code;
  int step = 0;
  std::string outcome;  // repeated | avoided
  std::optional<MistakeResult> original;  // none when its summary is gone
  MistakeResult retry;
};

struct PracticeState {
  DrillsState drills;
  GuessState guess;
  CoachReviewState coach;
  std::optional<MistakeReport> mistake;
  std::optional<practice::DailyLink> run_daily;      // the daily challenge the run in play attempts
  std::optional<practice::DailyLink> pending_daily;  // a daily start on its way to the engine
  int retry_pick = 0;  // the debrief's retryable finding picked (D18)
};

}  // namespace tradelab::desktop
