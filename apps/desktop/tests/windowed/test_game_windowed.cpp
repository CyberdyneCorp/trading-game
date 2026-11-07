// The terminal's main flows on the real Vulkan renderer (lavapipe under Xvfb on Linux, MoltenVK on
// macOS), driven by injected input, with frames saved as PNG in $TRADELAB_CAPTURE_DIR.
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <cstdlib>
#include <filesystem>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/png.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

struct RavenSession {
  RavenSession() {
    std::string detail;
    REQUIRE_MESSAGE(init_vulkan(detail), "no Vulkan driver: " << detail);
    Rset_animations_enabled(R_FALSE);
    Rset_animation_time_step(1.0 / 60.0);
  }
  ~RavenSession() { Rshutdown(); }
};

Terminal::Capture saver(App& app, Driver& d) {
  return [&app, &d](const std::string& name) {
    d.frames(3);
    const char* dir = std::getenv("TRADELAB_CAPTURE_DIR");
    const std::filesystem::path out = dir != nullptr && *dir != '\0' ? dir : "captures";
    std::filesystem::create_directories(out);
    int w = 0;
    int h = 0;
    std::uint8_t* rgba = Rwindow_capture(app.view().window(), &w, &h);
    REQUIRE(rgba != nullptr);
    CHECK(testing::write_png((out / (name + ".png")).string(), rgba, w, h));
    Rpixels_free(rgba);
  };
}

}  // namespace

TEST_CASE("windowed terminal: place, drag the stop, cancel a limit order") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  Terminal t(app, d, saver(app, d));
  testing::flow_place_move_cancel(app, d, t);
}

// The window of the hosted macOS runners (their 1024x768 screen shrinks every window to 1024x645): the
// price pane is a few pixels tall. The drag still lands on its price (Terminal::drag_level_to).
TEST_CASE("windowed terminal: the stop drag lands on its price in the hosted runner's 1024x645 window") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1024, 645));
  Driver d(app);
  d.frames(4);
  Terminal t(app, d);
  testing::flow_place_move_cancel(app, d, t);
}

// Regression: grabbing a level made the chart 16 px shorter in a 1024-wide window (the focused candle's
// text under it wrapped to a second line), so the chart jumped under the pointer.
TEST_CASE("windowed terminal: grabbing a level does not move or resize the chart in a 1024-wide window") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1024, 645));
  Driver d(app);
  d.frames(4);
  Terminal t(app, d);
  testing::flow_grab_keeps_chart(d, t);
}

TEST_CASE("windowed terminal: stop past liquidation confirmed, then liquidation") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 5u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  Terminal t(app, d, saver(app, d));
  testing::flow_liquidation(app, d, t);
}

TEST_CASE("windowed terminal: option buy from the chain") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 7u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  Terminal t(app, d, saver(app, d));
  testing::flow_option_buy(app, d, t);
}
