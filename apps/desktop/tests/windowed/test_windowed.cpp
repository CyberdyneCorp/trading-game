// Windowed scripted playthrough (design "Testing", the desktop counterpart of the web's Playwright
// suites): the real Vulkan renderer (Mesa lavapipe under Xvfb on Linux, MoltenVK on macOS), input
// injected with Rwindow_inject_*, frames read back with Rwindow_capture and saved as PNG in
// $TRADELAB_CAPTURE_DIR (CI uploads them as artifacts). Animations are off and the animation clock
// steps a fixed 1/60 s per frame so captures are reproducible.
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/png.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using tradelab::desktop::testing::Driver;

namespace {

std::filesystem::path capture_dir() {
  const char* dir = std::getenv("TRADELAB_CAPTURE_DIR");
  std::filesystem::path p = dir != nullptr && *dir != '\0' ? dir : "captures";
  std::filesystem::create_directories(p);
  return p;
}

struct Pixel {
  std::uint8_t r, g, b;
};

// Counts pixels close to a colour (the candle colours of the terminal-ui palette).
int count_near(const std::uint8_t* rgba, int width, int height, Pixel c) {
  int n = 0;
  for (int i = 0; i < width * height; ++i) {
    const std::uint8_t* p = rgba + static_cast<std::ptrdiff_t>(i) * 4;
    if (std::abs(p[0] - c.r) < 24 && std::abs(p[1] - c.g) < 24 && std::abs(p[2] - c.b) < 24) ++n;
  }
  return n;
}

struct Capture {
  int width = 0;
  int height = 0;
  int up_pixels = 0;
  int down_pixels = 0;
  bool saved = false;
};

Capture capture(Rid window, const std::string& name) {
  Capture c;
  std::uint8_t* rgba = Rwindow_capture(window, &c.width, &c.height);
  if (rgba == nullptr) return c;
  c.up_pixels = count_near(rgba, c.width, c.height, {0x39, 0xFF, 0x88});
  c.down_pixels = count_near(rgba, c.width, c.height, {0xFF, 0x5F, 0x7A});
  c.saved = testing::write_png((capture_dir() / (name + ".png")).string(), rgba, c.width, c.height);
  Rpixels_free(rgba);
  return c;
}

struct RavenSession {
  RavenSession() {
    std::string detail;
    REQUIRE_MESSAGE(init_vulkan(detail), "no Vulkan driver: " << detail);
    Rset_animations_enabled(R_FALSE);
    Rset_animation_time_step(1.0 / 60.0);
  }
  ~RavenSession() { Rshutdown(); }
};

}  // namespace

TEST_CASE("windowed: mission 01 to the debrief on the Vulkan renderer, with captures") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  const Rid window = app.view().window();
  Driver d(app);
  d.frames(4);

  const Capture menu = capture(window, "01-menu");
  REQUIRE(menu.saved);
  CHECK(menu.width >= 1024);
  CHECK(menu.up_pixels > 0);  // the green TRADELAB title is on screen

  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.to_briefing());
  CHECK(capture(window, "02-briefing").saved);

  REQUIRE(d.click(names::kBriefGo));
  testing::Terminal t(app, d);
  t.skip_question();
  d.choose(names::game::kSizeBy, 1);  // quantity
  d.type_into(names::kGameQty, "2,5");
  REQUIRE(d.click(names::kGameBuy));
  t.submit_with_plan();
  for (int i = 0; i < 25; ++i) t.advance();
  const Capture game = capture(window, "03-terminal");
  REQUIRE(game.saved);
  CHECK(game.down_pixels > 50);  // red candles drawn by the chart painter
  CHECK(d.text(names::kGameAccount).find("COMPRADO 2,5") != std::string::npos);

  while (app.vm().screen() == Screen::Game) {
    t.skip_question();
    d.focus(names::kGameAdvance);
    d.key(R_KEY_SPACE, R_MOD_SHIFT);  // Shift+Space: five bars (never past a calibration question)
    REQUIRE(d.idle());
  }
  REQUIRE(app.vm().screen() == Screen::Debrief);
  CHECK(capture(window, "04-debrief").saved);
  CHECK(d.text(names::kDebriefTitle) == "Missão concluída");
}

TEST_CASE("windowed: 1024x768 still shows the account line and the ticket") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 3u; });
  REQUIRE(app.build(1024, 768));
  Driver d(app);
  d.frames(2);
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.to_briefing());
  REQUIRE(d.click(names::kBriefGo));
  d.frames(2);
  for (std::string_view name : {names::kGameAccount, names::kGameBuy, names::kGameAdvance, names::kGameChart}) {
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(d.widget(name), &x, &y, &w, &h);
    INFO(name);
    CHECK(w > 0);
    CHECK(y + h <= 768);
  }
  CHECK(capture(app.view().window(), "05-terminal-1024x768").saved);
}
