// Wave 3 of add-practice-and-advanced-strategies on Raven's null platform, driven by injected input on
// the real engine (tests/support/practice_flows.hpp): a drill session from the keyboard, the daily
// challenge to its debrief and the coach review over it, guess rounds, and a mistake retried from the
// debrief, avoided then repeated. Every shown control of the new screens has a screen-reader name.
#include <doctest/doctest.h>

#include <raven/raven_accessibility.h>
#include <raven/raven_shell.h>
#include <raven/raven_widget.h>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/practice_flows.hpp"
#include "support/text_dump.hpp"
#include "view/app.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

struct RavenSession {
  RavenSession() { REQUIRE(init_headless()); }
  ~RavenSession() { Rshutdown(); }
};

struct Session {
  RavenSession raven;
  App app{builtin_catalog(), [] { return 3u; }};
  std::unique_ptr<Driver> d;
  std::unique_ptr<Terminal> t;
  Session() {
    REQUIRE(app.build(1280, 800));
    d = std::make_unique<Driver>(app);
    d->frames(2);
    t = std::make_unique<Terminal>(app, *d);
  }
};

bool control(RWidgetType t) {
  switch (t) {
    case R_WIDGET_BUTTON:
    case R_WIDGET_CHECKBOX:
    case R_WIDGET_TEXT_INPUT:
    case R_WIDGET_COMBO_BOX:
    case R_WIDGET_LIST_VIEW:
    case R_WIDGET_TABLE:
    case R_WIDGET_CUSTOM: return true;
    default: return false;
  }
}

void unnamed(Rid root, std::vector<std::string>& out) {
  if (Rwidget_get_visibility(root) == R_COLLAPSED) return;
  if (control(Rwidget_get_type(root))) {
    const char* name = Rwidget_get_accessible_name(root);
    if (name == nullptr || *name == '\0') out.push_back(Rwidget_get_name(root) != nullptr ? Rwidget_get_name(root) : "<unnamed>");
  }
  for (int i = 0; i < Rwidget_get_child_count(root); ++i) unnamed(Rwidget_get_child(root, i), out);
}

void check_names(Session& s, Screen screen) {
  s.d->frames(2);
  std::vector<std::string> missing;
  unnamed(s.app.view().screen_root(screen), missing);
  INFO("screen " << static_cast<int>(screen));
  for (const auto& m : missing) MESSAGE("no accessible name: " << m);
  CHECK(missing.empty());
}

}  // namespace

TEST_CASE("headless practice: a drill session from the keyboard, a wrong answer explained, the session kept") {
  Session s;
  testing::flow_drills(s.app, *s.d);
  check_names(s, Screen::Drills);
  const auto& history = s.app.vm().progress().drill_history;
  REQUIRE(history.size() == 1);
  CHECK(history[0].items == 6);
  CHECK(history[0].first_try == 5);
  CHECK(Rwidget_get_visibility(s.d->widget(names::kDrillsEndBox)) == R_VISIBLE);
  // The end screen's lesson link opens the first item's lesson and returns to the drills.
  testing::press(*s.d, names::kDrillsLesson);
  CHECK(s.app.vm().screen() == Screen::Lesson);
}

TEST_CASE("headless practice: the daily challenge to its debrief, then the coach review over it") {
  Session s;
  testing::flow_daily(s.app, *s.d, *s.t);
  const practice::DailyLink today = s.app.vm().daily_today();
  CHECK(s.app.vm().snapshot()->mission == today.mission);
  CHECK(s.app.vm().snapshot()->seed == today.seed);
  CHECK(s.app.vm().progress().daily_challenges.at(today.date).attempts == 1);
  check_names(s, Screen::Debrief);
  testing::flow_coach(s.app, *s.d);
  s.app.vm().back_to_menu();
  s.d->frames(2);
  testing::press(*s.d, names::kMenuDaily);
  check_names(s, Screen::Daily);
  CHECK(Rtable_get_row_count(s.d->widget(names::kDailyHistory)) == 1);
}

TEST_CASE("headless practice: guess rounds, a long with levels and a no trade, scored and kept") {
  Session s;
  testing::flow_guess(s.app, *s.d, 0, 70, 1.5, 3.0);
  check_names(s, Screen::Guess);
  const GuessState& g = s.app.vm().practice().guess;
  REQUIRE(g.outcome);
  CHECK(*g.outcome->score.side_ok);
  CHECK(*g.outcome->score.distance_ok);  // 1,5 ATR
  CHECK(*g.outcome->score.reward_ok);    // 2R
  CHECK(g.outcome->score.consistency == 1000);
  testing::flow_guess(s.app, *s.d, 2, 50, 0, 0);
  REQUIRE(g.outcome);
  CHECK(g.outcome->score.plan == 4000);
  CHECK(g.outcome->score.calibration == 2500);
  const auto& rounds = s.app.vm().progress().guess_history;
  REQUIRE(rounds.size() == 2);
  CHECK(rounds[1].decision == "none");
  CHECK(testing::dump_texts(s.d->widget(names::kGuessReveal)).find("25") != std::string::npos);
}

TEST_CASE("headless practice: a mistake retried from the debrief, repeated, then avoided") {
  Session s;
  testing::flow_chase(s.app, *s.d, *s.t);
  const std::size_t runs = s.app.vm().progress().runs.size();
  testing::flow_retry(s.app, *s.d, *s.t, true);  // the same chase again
  REQUIRE(s.app.vm().practice().mistake);
  CHECK(s.app.vm().practice().mistake->outcome == "repeated");
  CHECK(Rwidget_get_visibility(s.d->widget(names::kDebriefMistakePanel)) == R_VISIBLE);
  check_names(s, Screen::Debrief);
  // The retry is an assisted attempt: never a completion, points or statistics.
  REQUIRE(s.app.vm().progress().runs.size() == runs + 1);
  CHECK(s.app.vm().progress().runs.back().assisted);
  testing::flow_retry(s.app, *s.d, *s.t, false);  // from the retry's own debrief, waiting it out
  CHECK(s.app.vm().practice().mistake->outcome == "avoided");
}
