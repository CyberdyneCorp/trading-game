// Wave 1 of add-practice-and-advanced-strategies on Raven's null platform, driven by injected input on
// the real engine (tests/support/wave1_flows.hpp): time in force and OCO links, the planned trailing
// stop, and a calendar built, rolled and adjusted with the heatmap and the portfolio.
#include <doctest/doctest.h>

#include <raven/raven_shell.h>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/wave1_flows.hpp"
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

}  // namespace

TEST_CASE("headless wave 1: an IOC limit expires unfilled at the next open; two orders linked OCO after the confirmation") {
  Session s;
  testing::flow_tif_oco(s.app, *s.d, *s.t);
}

TEST_CASE("headless wave 1: a planned trailing stop offered after the fill, placed from the notice") {
  Session s;
  testing::flow_planned_trail(s.app, *s.d, *s.t);
}

TEST_CASE("headless wave 1: a calendar on m17 with the heatmap, rolled, adjusted, and the portfolio") {
  Session s;
  testing::flow_calendar(s.app, *s.d, *s.t);
}
