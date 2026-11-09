// Port of apps/web/src/lib/game/coachReview.ts (names kept; web parity fixture coach.json): coach mode and
// mistake replay (add-practice-and-advanced-strategies D17, D18). The engine's tl_coach_review gives the
// moments of a finished run, each read from what was visible at its step; this module turns them into
// texts (numbers from the moment's evidence only), cuts the run's state to a moment's bar so the chart
// draws no future, and, after a mistake retry, says whether the mistake was repeated or avoided.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/game_state.hpp"
#include "model/i18n.hpp"
#include "model/json.hpp"

namespace tradelab::desktop::practice {

// One moment of tl_coach_review: its step, code, tone (warn | good), the evidence as the engine gives
// it (decimal strings and integers, optional keys) and the lesson and glossary ids it links.
struct CoachMoment {
  int step = 0;
  std::string code;
  std::string tone;
  json::Value evidence;
  std::string lesson;
  std::string glossary;
};

// The review: moments ordered by step; `error` set ("run_active", "unsupported", ...) when there is none.
struct CoachReview {
  std::vector<CoachMoment> moments;
  std::string error;
};
CoachMoment coach_moment_from(const json::Value& v);
CoachReview coach_review_from(const json::Value& v);

// The i18n key of a moment's text: one per code, with a variant where the evidence changes the sentence.
std::string moment_key(const CoachMoment& m);
// The text parameters: the step and the evidence formatted for the locale.
Params moment_params(const CoachMoment& m, Locale locale);
// A moment's text in the catalog's locale.
std::string moment_text(const CoachMoment& m, const Catalog& catalog);

// The run's state as of a moment's bar: bars, indicators, patterns, events and news up to it, and none
// of the final orders or position (which that bar did not know).
GameState state_at(const GameState& s, int step);

// A warning moment can be retried from its step (D18).
inline bool retryable(const CoachMoment& m) { return m.tone == "warn"; }
// A debrief finding is offered "Retry from here" when it is a warning at a decision step.
inline bool finding_retryable(std::string_view tone, int step, int warmup_bars) { return tone == "warn" && step >= warmup_bars; }

// The steps after a retried moment where the same code counts as repeated.
inline constexpr int kRepeatWindow = 5;

struct FindingRef {
  std::string code;
  int step = 0;
};
// Repeated when the retry has the same code (a coach moment, or a finding naming it) at a step in
// [step, step + 5]; avoided otherwise.
std::string mistake_outcome(const std::string& code, int step, const std::vector<CoachMoment>& moments,
                            const std::vector<FindingRef>& findings);
// The i18n key naming a retried moment (a coach code or a debrief finding code), never what came after.
std::string mistake_name(const std::string& code);

}  // namespace tradelab::desktop::practice
