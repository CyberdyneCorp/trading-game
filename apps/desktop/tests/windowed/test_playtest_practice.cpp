// Playtest of wave 3 of add-practice-and-advanced-strategies (W3-D4) on the Vulkan renderer: the
// practice formats played as a new player would, once in PT-BR at 1280x800 and once in en at 1024x768
// (and the drills and a guess round at 150% text): a drill session with a wrong first answer, the daily
// challenge twice (first and best), ten guess rounds, the coach review of a reckless run (m24 chased)
// and two mistake retries from its debrief, one repeating the mistake and one avoiding it. Each step
// is captured (playtest/w3-<locale>-*.png) with its screen's texts in a report; no text may be cut by
// its panel.
#include <doctest/doctest.h>

#include <raven/raven_widget.h>

#include <memory>
#include <string>

#include "support/playtest_run.hpp"
#include "support/practice_flows.hpp"
#include "support/text_dump.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::RavenSession;
using tradelab::desktop::testing::Run;

namespace {

struct Window {
  Locale locale;
  int width;
  int height;
  float text_scale = 1.0f;
};

struct Player {
  RavenSession raven;
  App app;
  std::unique_ptr<Driver> d;
  std::unique_ptr<Run> run;
  std::unique_ptr<testing::Terminal> t;
  Player(Window w, const std::string& name) : app(builtin_catalog(), [] { return 3u; }) {
    REQUIRE(app.build(w.width, w.height));
    app.vm().set_crt(CrtSettings{false, false, false, false});
    d = std::make_unique<Driver>(app);
    d->frames(4);
    app.vm().set_locale(w.locale);
    app.vm().set_text_scale(w.text_scale);
    d->frames(2);
    run = std::make_unique<Run>(app, *d, name, w.width, w.height);
    t = std::make_unique<testing::Terminal>(app, *d);
    run->report() << name << " " << w.width << "x" << w.height << " text " << w.text_scale << "\n";
  }
  // A capture of the current screen with `widget` scrolled into view, its texts and the clipping check.
  testing::PracticeShot shot() {
    return [this](const std::string& label, std::string_view widget) {
      Rwidget_scroll_into_view(d->widget(widget));
      run->shot(label);
      run->clipping(label);
      run->report() << "---- " << label << "\n" << testing::dump_texts(app.view().screen_root(app.vm().screen()));
    };
  }
  void menu() {
    app.vm().back_to_menu();
    d->frames(3);
  }
};

// A round's facts for recomputing its score by hand: the close and ATR at the decision, the levels, the
// close 12 bars later, and the score's parts in hundredths.
void report_round(Player& p) {
  const GuessState& g = p.app.vm().practice().guess;
  const GuessOutcome& o = *g.outcome;
  p.run->report() << "round " << g.pick->market << " seed " << g.pick->seed << " bar " << g.pick->step << ": close " << o.from
                  << " atr " << g.at_decision_raw["indicators"]["atr14"][static_cast<std::size_t>(g.pick->step)].as_string()
                  << " decision " << g.decision << " p " << g.p << " inv " << g.invalidation << " target " << g.target << " -> "
                  << o.to << (o.higher ? " higher" : " not higher") << " first " << o.hit << " | calibration " << o.score.calibration
                  << " plan " << o.score.plan << " consistency " << o.score.consistency << " total " << o.score.total << "\n";
}

void play(Window w, const std::string& name) {
  Player p(w, name);
  const auto shot = p.shot();
  testing::flow_drills(p.app, *p.d, shot);
  p.menu();
  // The daily challenge twice: the first finished attempt is the day's record, the best beside it.
  testing::flow_daily(p.app, *p.d, *p.t, shot);
  p.menu();
  testing::flow_daily(p.app, *p.d, *p.t);
  p.menu();
  testing::press(*p.d, names::kMenuDaily);
  shot("w3-daily-history", names::kDailyHistory);
  const auto& day = p.app.vm().progress().daily_challenges.at(p.app.vm().daily_today().date);
  CHECK(day.attempts == 2);
  p.menu();
  // Ten rounds: sensible trades, a no trade, and overconfident or inconsistent ones.
  struct Pick {
    int decision, p;
    double inv, target;
  };
  const Pick picks[] = {{0, 70, 1.5, 3}, {1, 30, 1.5, 3}, {2, 50, 0, 0}, {0, 95, 0.2, 0.2}, {1, 80, 4, 1},
                        {0, 60, 1, 2},   {2, 80, 0, 0},   {1, 45, 2, 3}, {0, 55, 0.5, 0.75}, {1, 20, 1, 1.5}};
  for (int i = 0; i < 10; ++i) {
    const Pick& k = picks[i];
    testing::flow_guess(p.app, *p.d, k.decision, k.p, k.inv, k.target, i == 0 ? shot : testing::PracticeShot{});
    report_round(p);
  }
  shot("w3-guess-history", names::kGuessRounds);
  p.menu();
  // A reckless run reviewed by the coach, then retried from its debrief twice.
  testing::flow_chase(p.app, *p.d, *p.t);
  shot("w3-chase-debrief", names::kDebriefRetryBox);
  testing::flow_coach(p.app, *p.d, shot);
  testing::flow_retry(p.app, *p.d, *p.t, true, shot);
  testing::flow_retry(p.app, *p.d, *p.t, false, shot);
  p.run->finish();
}

}  // namespace

TEST_CASE("playtest practice: drills, daily twice, ten guess rounds, coach review and retries in PT-BR at 1280x800") {
  play({Locale::PtBR, 1280, 800}, "w3-pt");
}

TEST_CASE("playtest practice: drills, daily twice, ten guess rounds, coach review and retries in en at 1024x768") {
  play({Locale::En, 1024, 768}, "w3-en");
}

TEST_CASE("playtest practice: the drills and a guess round at 150% text in a 1024x768 window") {
  Player p({Locale::PtBR, 1024, 768, 1.5f}, "w3-large");
  const auto shot = p.shot();
  testing::flow_drills(p.app, *p.d, shot);
  p.menu();
  testing::flow_guess(p.app, *p.d, 0, 70, 1.5, 3, shot);
  p.run->finish();
}
