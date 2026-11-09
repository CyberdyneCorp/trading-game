// Scripted flows of wave 3 of add-practice-and-advanced-strategies (the practice formats), driven by
// injected input on the real engine, by keyboard where a player would use it: a drill session with a
// wrong first answer, the daily challenge, a guess round, the coach review of a finished run and a mistake
// retried from the debrief. Shared by the headless tests and the windowed playtest.
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include <functional>
#include <string>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/shell_flows.hpp"
#include "support/strategy_flows.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/practice_names.hpp"

namespace tradelab::desktop::testing {

using PracticeShot = std::function<void(const std::string& label, std::string_view widget)>;

// A focused button activated with Enter, as a keyboard player does.
inline void press(Driver& d, std::string_view name) {
  Rwidget_scroll_into_view(d.widget(name));
  d.focus(name);
  d.key(R_KEY_ENTER);
  d.frames(2);
}

inline void shoot(const PracticeShot& shot, const std::string& label, std::string_view widget) {
  if (shot) shot(label, widget);
}

// Six items from the menu: the first answered wrong (its worked explanation shown), the rest right.
inline void flow_drills(App& app, Driver& d, const PracticeShot& shot = {}) {
  AppViewModel& vm = app.vm();
  press(d, names::kMenuDrills);
  REQUIRE(d.until([&] { return vm.screen() == Screen::Drills; }));
  shoot(shot, "w3-drills-intro", names::kDrillsIntro);
  press(d, names::kDrillsStart);
  const DrillsState& s = vm.practice().drills;
  REQUIRE(s.phase == DrillsState::Phase::Asking);
  for (std::size_t k = 0; k < s.items.size(); ++k) {
    const practice::DrillItem item = s.items[k];
    const bool wrong = k == 0;
    if (item.choice) {
      const int pick = wrong ? 1 - static_cast<int>(item.answer) : static_cast<int>(item.answer);
      press(d, names::kDrillsChoice[static_cast<std::size_t>(pick)]);
    } else {
      d.type_into(names::kDrillsInput, fmt::number(item.answer + (wrong ? 1 : 0), vm.locale(), item.digits));
      d.key(R_KEY_ENTER);  // the field's Enter checks the answer
      d.frames(2);
    }
    REQUIRE(s.results[k].has_value());
    CHECK((*s.results[k] == practice::ItemResult::Wrong) == wrong);
    if (k == 0) shoot(shot, "w3-drills-wrong", names::kDrillsExplain);
    press(d, names::kDrillsNext);
  }
  REQUIRE(s.phase == DrillsState::Phase::Done);
  shoot(shot, "w3-drills-end", names::kDrillsResults);
}

// Today's challenge from the menu, played through its briefing to the debrief with no trade.
inline void flow_daily(App& app, Driver& d, Terminal& t, const PracticeShot& shot = {}) {
  AppViewModel& vm = app.vm();
  press(d, names::kMenuDaily);
  REQUIRE(d.until([&] { return vm.screen() == Screen::Daily; }));
  shoot(shot, "w3-daily", names::kDailyToday);
  press(d, names::kDailyPlay);
  REQUIRE(d.to_briefing());
  REQUIRE(d.click(names::kBriefGo));
  REQUIRE(vm.screen() == Screen::Game);
  REQUIRE(vm.run_daily());
  CHECK(Rwidget_get_visibility(d.widget(names::kGameDailyBanner)) == R_VISIBLE);
  shoot(shot, "w3-daily-terminal", names::kGameDailyBanner);
  play_to_debrief(app, d, t);
  CHECK(Rwidget_get_visibility(d.widget(names::kDebriefDaily)) == R_VISIBLE);
  shoot(shot, "w3-daily-debrief", names::kDebriefDaily);
}

// A round from the menu: `decision` 0 long, 1 short, 2 no trade at probability `p`; a trade's levels at
// `inv_atr` ATR on the losing side and `target_atr` on the winning side of the close.
inline void flow_guess(App& app, Driver& d, int decision, int p, double inv_atr, double target_atr, const PracticeShot& shot = {}) {
  AppViewModel& vm = app.vm();
  if (vm.screen() != Screen::Guess) press(d, names::kMenuGuess);
  REQUIRE(d.until([&] { return vm.screen() == Screen::Guess; }));
  press(d, vm.practice().guess.phase == GuessState::Phase::Reveal ? names::kGuessNext : names::kGuessStart);
  const GuessState& g = vm.practice().guess;
  REQUIRE(d.until([&] { return g.phase == GuessState::Phase::Commit; }));
  shoot(shot, "w3-guess-commit", names::kGuessClose);
  d.choose(names::kGuessDecision, decision);
  d.focus(names::kGuessP);  // 19 probabilities: from the first, as many arrows as needed
  for (int i = 0; i < 20; ++i) d.key(R_KEY_UP);
  for (int i = 0; i < p / 5 - 1; ++i) d.key(R_KEY_DOWN);
  REQUIRE(g.p == p);
  if (decision != 2) {
    const double close = g.at_decision->bars.back().close;
    const double atr = g.at_decision->indicators.atr14.back().value_or(1);
    const double dir = decision == 0 ? 1 : -1;
    const double tick = g.at_decision->contract.tick;
    d.type_into(names::kGuessInv, fmt::price_input_text(close - dir * inv_atr * atr, tick, vm.locale()));
    d.type_into(names::kGuessTarget, fmt::price_input_text(close + dir * target_atr * atr, tick, vm.locale()));
  }
  press(d, names::kGuessCommit);
  REQUIRE(d.until([&] { return g.phase == GuessState::Phase::Reveal; }));
  shoot(shot, "w3-guess-reveal", names::kGuessReveal);
}

// From a debrief: every coach moment, the end screen, then Esc back to the debrief.
inline int flow_coach(App& app, Driver& d, const PracticeShot& shot = {}) {
  AppViewModel& vm = app.vm();
  press(d, names::kDebriefOpenReview);
  REQUIRE(vm.screen() == Screen::CoachReview);
  REQUIRE(d.until([&] { return vm.practice().coach.review.has_value(); }));
  const int n = static_cast<int>(vm.practice().coach.review->moments.size());
  for (int i = 0; i < n; ++i) {
    shoot(shot, "w3-review-" + std::to_string(i + 1), names::kReviewText);
    d.focus(names::kReviewClose);  // off the chart: the arrows move between moments
    d.key(R_KEY_RIGHT);
  }
  shoot(shot, "w3-review-end", names::kReviewStatus);
  d.key(R_KEY_ESCAPE);
  REQUIRE(vm.screen() == Screen::Debrief);
  return n;
}

// m24 chased as wave 2's reckless player does: 38 bars waited, then 40 bought with no plan or stop.
inline void flow_chase(App& app, Driver& d, Terminal& t) {
  t.open(mission_index("m24-fast-market"));
  for (int i = 0; i < 38; ++i) t.advance();
  REQUIRE(d.click(names::kGameBuy));
  d.choose(names::game::kSizeBy, 1);
  t.type(names::kGameQty, "40");
  t.submit_with_plan();
  play_to_debrief(app, d, t);
}

// The debrief's first retryable finding retried from the keyboard: the terminal names the moment; with
// `again` the same 40 are bought at once (the mistake repeated), otherwise the attempt waits it out.
inline void flow_retry(App& app, Driver& d, Terminal& t, bool again, const PracticeShot& shot = {}) {
  AppViewModel& vm = app.vm();
  REQUIRE(Rwidget_get_visibility(d.widget(names::kDebriefRetryBox)) == R_VISIBLE);
  d.choose(names::kDebriefRetryPick, 0);
  press(d, names::kDebriefRetry);
  REQUIRE(d.until([&] { return vm.screen() == Screen::Game; }));
  REQUIRE(vm.run_assisted());
  CHECK(vm.run_assisted()->kind == "mistake");
  CHECK(Rwidget_get_visibility(d.widget(names::kGameMistakeBanner)) == R_VISIBLE);
  shoot(shot, again ? "w3-retry-terminal-again" : "w3-retry-terminal", names::kGameMistakeBanner);
  if (again) {
    t.skip_question();
    REQUIRE(d.click(names::kGameBuy));
    d.choose(names::game::kSizeBy, 1);
    t.type(names::kGameQty, "40");
    t.submit_with_plan();
  }
  play_to_debrief(app, d, t);
  REQUIRE(d.until([&] { return vm.practice().mistake.has_value(); }));
  shoot(shot, again ? "w3-retry-report-again" : "w3-retry-report", names::kDebriefMistake);
}

}  // namespace tradelab::desktop::testing
