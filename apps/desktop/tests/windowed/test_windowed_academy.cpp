// Windowed Academy on the Vulkan renderer (lavapipe under Xvfb on Linux, MoltenVK on macOS; tasks D3,
// D6, D7, D8 of add-academy-and-strategies): every lesson finished with the mouse (each question wrong
// first), the glossary and a term's definition on focus, the guided first trade (start, keys, skip,
// replay from the help and the briefing). Frames go to $TRADELAB_CAPTURE_DIR/academy as PNG; the payoff
// diagram and the coach outline are checked for drawn pixels.
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "support/academy_flows.hpp"
#include "support/png.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;

namespace {

std::filesystem::path capture_dir() {
  const char* dir = std::getenv("TRADELAB_CAPTURE_DIR");
  std::filesystem::path p = dir != nullptr && *dir != '\0' ? dir : "captures";
  p /= "academy";
  std::filesystem::create_directories(p);
  return p;
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

bool near(const std::uint8_t* p, int r, int g, int b) { return std::abs(p[0] - r) < 40 && std::abs(p[1] - g) < 40 && std::abs(p[2] - b) < 40; }

// Pixels of colour (r, g, b) inside the named widget of the last frame (window points scaled to pixels).
int pixels_in(Driver& d, std::string_view widget, const std::uint8_t* rgba, int width, int height, int r, int g, int b) {
  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(d.widget(widget), &x, &y, &w, &h);
  int ww = 0, wh = 0;
  Rwindow_get_size(d.window(), &ww, &wh);
  const float sx = ww > 0 ? static_cast<float>(width) / static_cast<float>(ww) : 1.0f;
  const float sy = wh > 0 ? static_cast<float>(height) / static_cast<float>(wh) : 1.0f;
  int n = 0;
  for (int py = static_cast<int>(y * sy); py < std::min(height, static_cast<int>((y + h) * sy)); ++py) {
    for (int px = static_cast<int>(x * sx); px < std::min(width, static_cast<int>((x + w) * sx)); ++px) {
      n += near(rgba + (static_cast<std::ptrdiff_t>(py) * width + px) * 4, r, g, b) ? 1 : 0;
    }
  }
  return n;
}

// Saves the frame; returns the accent-green pixels inside `widget` (0 without one).
int capture(Driver& d, const std::string& name, std::string_view widget = {}) {
  d.frames(3);
  int width = 0;
  int height = 0;
  std::uint8_t* rgba = Rwindow_capture(d.window(), &width, &height);
  if (rgba == nullptr) return -1;
  CHECK(testing::write_png((capture_dir() / (name + ".png")).string(), rgba, width, height));
  const int n = widget.empty() ? 0 : pixels_in(d, widget, rgba, width, height, 0x39, 0xFF, 0x88);
  Rpixels_free(rgba);
  return n;
}

}  // namespace

TEST_CASE("windowed academy: every lesson finished with the mouse, wrong then right, with its widgets drawn") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  app.vm().set_crt(CrtSettings{false, false, false, false});  // pixel checks without the CRT tint
  Driver d(app);
  d.frames(4);
  int payoff_steps = 0;
  testing::flow_every_lesson(app, d, true, false, [&](const std::string& name) {
    const LessonStep* s = app.vm().shell().lesson.step();
    const bool payoff = app.vm().screen() == Screen::Lesson && s != nullptr && std::holds_alternative<PayoffWidget>(s->widget);
    if (!payoff) {
      capture(d, name);
      return;
    }
    Rwidget_scroll_into_view(d.widget(names::kLessonPayoff));
    CHECK(capture(d, name, names::kLessonPayoff) > 50);  // the expiry curve is painted
    ++payoff_steps;
  });
  CHECK(payoff_steps > 0);
}

TEST_CASE("windowed academy: glossary search, a term opened, and a lesson term's definition on focus") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  testing::flow_glossary(app, d, [&](const std::string& name) { capture(d, name); });
}

TEST_CASE("windowed academy: the guided first trade on the real terminal: start, keys, skip, replay") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  app.vm().set_crt(CrtSettings{false, false, false, false});
  Driver d(app);
  d.frames(4);
  testing::flow_coach(app, d, [&](const std::string& name) {
    CHECK(capture(d, "coach-" + name, names::kGameCoachOutline) > 20);  // the target's outline is drawn
  });
}
