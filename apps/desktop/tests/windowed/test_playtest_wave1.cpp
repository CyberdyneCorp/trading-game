// Playtest of wave 1 of add-practice-and-advanced-strategies on the Vulkan renderer: time in force and
// OCO links, the planned trailing stop, and a calendar on m17 with the heatmap, an adjustment, a roll
// and the portfolio, in PT-BR at 1280x800 and in en at 1024x768. Each run leaves captures and a text
// report in $TRADELAB_CAPTURE_DIR/playtest (the ticket's, the orders panel's, the builder's and the
// portfolio's texts at each step); no text may be cut by its panel.
#include <doctest/doctest.h>

#include <raven/raven_widget.h>

#include <string>

#include "support/playtest_run.hpp"
#include "support/wave1_flows.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::RavenSession;
using tradelab::desktop::testing::Run;

namespace {

struct Window {
  Locale locale;
  int width;
  int height;
};

constexpr Window kWindows[] = {{Locale::PtBR, 1280, 800}, {Locale::En, 1024, 768}};

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
    d->frames(2);
    run = std::make_unique<Run>(app, *d, name + "-" + std::to_string(w.width), w.width, w.height);
    t = std::make_unique<testing::Terminal>(app, *d);
  }
  testing::Wave1Shot shot() {
    return [this](const std::string& label) {
      run->shot(label);
      run->report() << "---- " << label << "\n" << testing::dump_texts(d->widget(names::game::kModal)) << testing::dump_texts(d->widget("game-content"));
    };
  }
};

}  // namespace

TEST_CASE("playtest wave 1: time in force and an OCO link (pt-BR 1280x800, en 1024x768)") {
  for (const Window& w : kWindows) {
    Player p(w, "w1-tif-oco");
    testing::flow_tif_oco(p.app, *p.d, *p.t, p.shot());
  }
}

TEST_CASE("playtest wave 1: the planned trailing stop (pt-BR 1280x800, en 1024x768)") {
  for (const Window& w : kWindows) {
    Player p(w, "w1-trail");
    testing::flow_planned_trail(p.app, *p.d, *p.t, p.shot());
  }
}

TEST_CASE("playtest wave 1: a calendar with the heatmap, adjusted, rolled, and the portfolio (pt-BR 1280x800, en 1024x768)") {
  for (const Window& w : kWindows) {
    Player p(w, "w1-calendar");
    testing::flow_calendar(p.app, *p.d, *p.t, p.shot());
  }
}
