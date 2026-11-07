// The strategy builder, the what-if lab and the strategy missions on Raven's null platform, driven by
// injected input (tests/support/strategy_flows.hpp): every template analysed by the engine and the
// condor filled, the lab's sliders, the Academy lab from the menu, and m13–m17 played to the debrief.
#include <doctest/doctest.h>

#include <raven/raven_shell.h>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/strategy_flows.hpp"
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

TEST_CASE("headless builder: every template analysed by the engine; the iron condor sent as tagged legs and filled") {
  Session s;
  testing::flow_each_template(s.app, *s.d, *s.t);
}

TEST_CASE("headless lab: zero shift equals the chain; price, time and IV sliders move the engine's numbers") {
  Session s;
  testing::flow_lab_sliders(s.app, *s.d, *s.t);
}

TEST_CASE("headless Academy lab: from the menu, priced at the model, a slider and a perpetual leg") {
  Session s;
  testing::flow_academy_lab(s.app, *s.d);
}

TEST_CASE("headless strategy missions: m13–m17 each played with a builder structure to the debrief") {
  const std::pair<const char*, std::size_t> plays[] = {{"m13-call-spread", 0},    {"m14-covered-call", 2}, {"m15-protective-put", 3},
                                                       {"m16-range-condor", 6},  {"m17-before-expiry", 1}};
  for (const auto& [mission, tpl] : plays) {
    INFO(mission);
    Session s;
    testing::play_strategy_mission(s.app, *s.d, *s.t, mission, tpl);
  }
}
