// Windowed playthroughs of the strategy builder, the what-if lab and the strategy missions on the Vulkan
// renderer (lavapipe under Xvfb on Linux, MoltenVK on macOS), scripted with injected input
// (tests/support/strategy_flows.hpp): every template, the lab's sliders, the Academy lab, m13–m17 to the
// debrief. Frames go to $TRADELAB_CAPTURE_DIR as strategy-*.png; the builder and the Academy lab are
// compared with their reference images (1280x800, CRT off).
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "support/academy_flows.hpp"
#include "support/png.hpp"
#include "support/reference.hpp"
#include "support/strategy_flows.hpp"
#include "support/window_size.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

std::filesystem::path capture_dir() {
  const char* dir = std::getenv("TRADELAB_CAPTURE_DIR");
  std::filesystem::path p = dir != nullptr && *dir != '\0' ? dir : "captures";
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

struct Session {
  RavenSession raven;
  App app{builtin_catalog(), [] { return 3u; }};
  std::unique_ptr<Driver> d;
  std::unique_ptr<Terminal> t;
  Session() {
    REQUIRE(app.build(1280, 800));
    app.vm().set_crt(CrtSettings{false, false, false, false});
    d = std::make_unique<Driver>(app);
    d->frames(4);
    t = std::make_unique<Terminal>(app, *d);
  }
  testing::StrategyShot shot() {
    return [this](const std::string& name) {
      d->frames(3);
      int w = 0, h = 0;
      std::uint8_t* rgba = Rwindow_capture(d->window(), &w, &h);
      REQUIRE(rgba != nullptr);
      CHECK(testing::write_png((capture_dir() / ("strategy-" + name + ".png")).string(), rgba, w, h));
      Rpixels_free(rgba);
    };
  }
  // The window reduced and compared with its reference (only in a window of the references' size).
  void reference(const std::string& name) {
    d->frames(3);
    if (!testing::window_has_size(d->window(), 1280, 800)) return;
    Rwindow_inject_mouse_move(d->window(), 1, 1);  // no hover
    d->frames(2);
    int w = 0, h = 0;
    std::uint8_t* rgba = Rwindow_capture(d->window(), &w, &h);
    REQUIRE(rgba != nullptr);
    const auto r = testing::check_reference(TRADELAB_REFERENCE_DIR, capture_dir().string(), name, rgba, w, h);
    Rpixels_free(rgba);
    MESSAGE(r.message);
#if defined(TRADELAB_HAVE_ZLIB)
    const char* update = std::getenv("TRADELAB_UPDATE_REFERENCES");
    if (update == nullptr || *update != '1') {
      REQUIRE_MESSAGE(r.compared, r.message);
      CHECK_MESSAGE(r.diff.share <= 0.02, r.message);
    }
#endif
  }
};

}  // namespace

TEST_CASE("windowed builder: every template analysed and drawn; the iron condor sent and filled") {
  Session s;
  testing::flow_each_template(s.app, *s.d, *s.t, s.shot());
  s.reference("builder-condor-m13");
}

TEST_CASE("windowed lab: zero shift equals the chain; price, time and IV sliders; reset") {
  Session s;
  testing::flow_lab_sliders(s.app, *s.d, *s.t, s.shot());
}

TEST_CASE("windowed Academy lab: from the menu, priced at the model, a slider and a perpetual leg") {
  Session s;
  REQUIRE(s.d->click(names::builder::kMenuLab));
  REQUIRE(s.d->until([&] { return s.app.vm().academy_lab().result().has_value(); }));
  s.reference("academy-lab");
  REQUIRE(s.d->click(names::builder::kScreenBack));
  testing::flow_academy_lab(s.app, *s.d, s.shot());
}

TEST_CASE("windowed strategy missions: m13–m17 played with a builder structure to the debrief") {
  const std::pair<const char*, std::size_t> plays[] = {{"m13-call-spread", 0},   {"m14-covered-call", 2}, {"m15-protective-put", 3},
                                                       {"m16-range-condor", 6}, {"m17-before-expiry", 1}};
  for (const auto& [mission, tpl] : plays) {
    const std::string id = mission;
    INFO(id);
    Session s;
    testing::play_strategy_mission(s.app, *s.d, *s.t, mission, tpl, s.shot());
  }
}

namespace {

// Accent-green or attention-amber pixels inside the named widget of a frame (window points scaled to pixels).
int accent_pixels(Driver& d, std::string_view widget, const std::uint8_t* rgba, int width, int height) {
  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(d.widget(widget), &x, &y, &w, &h);
  int ww = 0, wh = 0;
  Rwindow_get_size(d.window(), &ww, &wh);
  const float sx = static_cast<float>(width) / static_cast<float>(std::max(1, ww));
  const float sy = static_cast<float>(height) / static_cast<float>(std::max(1, wh));
  int n = 0;
  for (int py = static_cast<int>(y * sy); py < std::min(height, static_cast<int>((y + h) * sy)); ++py) {
    for (int px = static_cast<int>(x * sx); px < std::min(width, static_cast<int>((x + w) * sx)); ++px) {
      const std::uint8_t* p = rgba + (static_cast<std::ptrdiff_t>(py) * width + px) * 4;
      const bool accent = std::abs(p[0] - 0x39) < 40 && std::abs(p[1] - 0xFF) < 40 && std::abs(p[2] - 0x88) < 40;
      const bool attention = std::abs(p[0] - 0xFF) < 40 && std::abs(p[1] - 0xC8) < 40 && std::abs(p[2] - 0x57) < 40;
      n += accent || attention ? 1 : 0;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("windowed tutorial art: m13-m17's first card drawn like the web's, PT-BR and en") {
  for (const bool en : {false, true}) {
    for (const char* mission : {"m13-call-spread", "m14-covered-call", "m15-protective-put", "m16-range-condor", "m17-before-expiry"}) {
      const std::string label = std::string(mission) + (en ? " en" : " pt-BR");
      INFO(label);
      Session s;
      if (en) s.app.vm().toggle_locale();
      s.app.vm().select(testing::mission_index(mission));
      s.d->frames(2);
      REQUIRE(s.d->click(names::kMenuStart));
      REQUIRE(s.d->until([&] { return s.app.vm().screen() == Screen::Tutorial; }));
      s.d->frames(4);
      REQUIRE(Rwidget_get_visibility(s.d->widget(names::kTutArt)) == R_VISIBLE);
      int w = 0, h = 0;
      std::uint8_t* rgba = Rwindow_capture(s.d->window(), &w, &h);
      REQUIRE(rgba != nullptr);
      CHECK(testing::write_png((capture_dir() / ("strategy-tutorial-" + std::string(mission) + (en ? "-en" : "-pt") + ".png")).string(), rgba, w, h));
      CHECK(accent_pixels(*s.d, names::kTutArt, rgba, w, h) > 50);  // the payoff curve (the roll: its arrow) is drawn
      Rpixels_free(rgba);
    }
  }
}

TEST_CASE("windowed references: m16's tutorial card art and a lesson's strategy builder (1280x800)") {
  {
    Session s;
    s.app.vm().select(testing::mission_index("m16-range-condor"));
    s.d->frames(2);
    REQUIRE(s.d->click(names::kMenuStart));
    REQUIRE(s.d->until([&] { return s.app.vm().screen() == Screen::Tutorial; }));
    s.reference("tutorial-condor");
  }
  Session s;
  s.app.vm().open_lesson("strategies-shapes", Screen::Menu);
  s.d->frames(2);
  testing::walk_to_step(s.app, *s.d, "view", false);
  REQUIRE(s.d->click(std::string(names::kLessonBuilderTemplatePrefix) + "6"));  // iron condor
  REQUIRE(s.d->until([&] { return s.app.vm().lesson_builder().result().has_value(); }));
  Rwidget_scroll_into_view(s.d->widget(names::kLessonBuilderPayoff));
  s.reference("lesson-builder-condor");
}

// add-practice-and-advanced-strategies W2-D4: m18's first card (the gamma-scalp drawing) and the
// curriculum map with its branch stages, against their reference images.
TEST_CASE("windowed references: m18's tutorial card art and the curriculum's branches (1280x800)") {
  {
    Session s;
    s.app.vm().select(testing::mission_index("m18-gamma-scalp"));
    s.d->frames(2);
    REQUIRE(s.d->click(names::kMenuStart));
    REQUIRE(s.d->until([&] { return s.app.vm().screen() == Screen::Tutorial; }));
    s.reference("tutorial-scalp");
  }
  Session s;
  REQUIRE(s.d->click(names::kMenuCurriculum));
  REQUIRE(s.app.vm().screen() == Screen::Curriculum);
  s.reference("curriculum-branches");
}
