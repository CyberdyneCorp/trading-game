// Wave 2 of add-practice-and-advanced-strategies on Raven's null platform, driven by injected input on
// the real engine: m18's tutorial art and briefing, and m24 played as a chase (a buy after a run of
// rising closes, no stop) to a debrief that names the FOMO entry with its evidence, reads the objective
// as missed and marks the entry's bar on the chart from the keyboard.
#include <doctest/doctest.h>

#include <raven/raven_controls.h>
#include <raven/raven_shell.h>
#include <raven/raven_widget.h>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/strategy_flows.hpp"
#include "support/text_dump.hpp"
#include "view/app.hpp"
#include "viewmodel/game/game_widgets.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;
namespace gn = tradelab::desktop::names::game;

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

}  // namespace

TEST_CASE("headless wave 2: m18's first tutorial card is drawn and its briefing names the delta limit") {
  Session s;
  s.app.vm().select(testing::mission_index("m18-gamma-scalp"));
  s.d->frames(2);
  REQUIRE(s.d->click(names::kMenuStart));
  REQUIRE(s.d->until([&] { return s.app.vm().screen() == Screen::Tutorial; }));
  CHECK(Rwidget_get_visibility(s.d->widget(names::kTutArt)) == R_VISIBLE);
  CHECK(s.d->text(names::kTutTitle) == "Gamma e hedge");
  REQUIRE(s.d->click(names::kTutSkip));
  REQUIRE(s.d->until([&] { return s.app.vm().screen() == Screen::Briefing; }));
  CHECK(s.d->text(names::kBriefRiskLimits).find("1.500 bp") != std::string::npos);
}

TEST_CASE("headless wave 2: m24 chased without a stop; the debrief names FOMO, misses the objective and marks the bar") {
  Session s;
  s.t->open(testing::mission_index("m24-fast-market"));
  for (int i = 0; i < 38; ++i) s.t->advance();
  REQUIRE(s.d->click(names::kGameBuy));
  s.d->choose(gn::kSizeBy, 1);
  s.t->type(names::kGameQty, "40");
  s.t->submit_with_plan();
  for (int guard = 0; guard < 120 && s.app.vm().screen() == Screen::Game; ++guard) {
    s.t->skip_question();
    s.d->focus(names::kGameAdvance);
    s.d->key(R_KEY_SPACE, R_MOD_SHIFT);
    REQUIRE(s.d->idle());
  }
  REQUIRE(s.app.vm().screen() == Screen::Debrief);
  const std::string findings = testing::dump_texts(s.d->widget(names::kDebriefFindings));
  CHECK(findings.find("Medo de ficar de fora (FOMO): a entrada da barra 98") != std::string::npos);
  CHECK(findings.find("sem stop") != std::string::npos);
  CHECK(testing::dump_texts(s.d->widget(names::kDebriefObjectives)).find("não cumprido") != std::string::npos);
  const Rid moments = s.d->widget(names::kDebriefMoments);
  REQUIRE(Rlist_view_get_item_count(moments) >= 2);
  s.d->focus(names::kDebriefMoments);
  s.d->key(R_KEY_DOWN);
  s.d->frames(2);
  REQUIRE(s.app.vm().debrief_moment().has_value());
  CHECK(*s.app.vm().debrief_moment() == 98);
  CHECK(Rwidget_get_visibility(s.d->widget(names::kDebriefEpisodesPanel)) == R_VISIBLE);
}
