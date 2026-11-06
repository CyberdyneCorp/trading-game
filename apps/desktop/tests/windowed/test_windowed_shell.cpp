// Windowed shell playthroughs on the Vulkan renderer (lavapipe under Xvfb on Linux, MoltenVK on
// macOS), driven by injected input like the web's Playwright suites: a campaign mission from the menu
// through the briefing to its debrief, the curriculum map, an Academy lesson, the journal, and the CRT
// effect on and off. Frames are saved as PNG in $TRADELAB_CAPTURE_DIR.
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/png.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

std::filesystem::path capture_dir() {
  const char* dir = std::getenv("TRADELAB_CAPTURE_DIR");
  std::filesystem::path p = dir != nullptr && *dir != '\0' ? dir : "captures";
  std::filesystem::create_directories(p);
  return p;
}

struct Frame {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
  bool saved = false;
  [[nodiscard]] int near(std::uint8_t r, std::uint8_t g, std::uint8_t b) const {
    int n = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
      if (std::abs(rgba[i] - r) < 24 && std::abs(rgba[i + 1] - g) < 24 && std::abs(rgba[i + 2] - b) < 24) ++n;
    }
    return n;
  }
};

Frame capture(Driver& d, Rid window, const std::string& name) {
  d.frames(3);
  Frame f;
  std::uint8_t* rgba = Rwindow_capture(window, &f.width, &f.height);
  if (rgba == nullptr) return f;
  f.rgba.assign(rgba, rgba + static_cast<std::ptrdiff_t>(f.width) * f.height * 4);
  f.saved = testing::write_png((capture_dir() / (name + ".png")).string(), rgba, f.width, f.height);
  Rpixels_free(rgba);
  return f;
}

// Share of differing pixels between two frames of the same size.
double changed(const Frame& a, const Frame& b) {
  if (a.rgba.size() != b.rgba.size() || a.rgba.empty()) return 1.0;
  std::size_t n = 0;
  for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
    n += (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) ? 1 : 0;
  }
  return static_cast<double>(n) / static_cast<double>(a.rgba.size() / 4);
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

// Shift+Space (five bars, never past a calibration question) until the run ends; questions skipped.
void play_to_debrief(Driver& d, App& app, Terminal& t) {
  for (int guard = 0; guard < 100 && app.vm().screen() == Screen::Game; ++guard) {
    t.skip_question();
    d.focus(names::kGameAdvance);
    d.key(R_KEY_SPACE, R_MOD_SHIFT);
    REQUIRE(d.idle());
  }
}

}  // namespace

TEST_CASE("windowed shell: a campaign mission from the menu to its debrief, then the next one") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 7u; });
  REQUIRE(app.build(1280, 800));
  const Rid window = app.view().window();
  Driver d(app);
  app.vm().set_reduce_motion(false);
  d.frames(4);
  REQUIRE(d.click(names::kCampaignStart));
  REQUIRE(d.to_briefing());
  CHECK(d.text(names::kBriefMode).find("Campanha") != std::string::npos);
  CHECK(d.text(names::kBriefLocked).find("Bloqueado nesta missão da campanha") != std::string::npos);
  CHECK(capture(d, window, "shell-01-campaign-briefing").saved);
  REQUIRE(d.click(names::kBriefGo));
  Terminal t(app, d);
  t.skip_question();
  d.choose(names::game::kSizeBy, 1);  // by quantity
  d.type_into(names::kGameQty, "1,5");
  REQUIRE(d.click(names::kGameBuy));
  t.submit_with_plan();
  play_to_debrief(d, app, t);
  REQUIRE(app.vm().screen() == Screen::Debrief);
  const Frame debrief = capture(d, window, "shell-02-campaign-debrief");
  CHECK(debrief.saved);
  CHECK(debrief.near(0x39, 0xFF, 0x88) > 0);  // phosphor titles
  CHECK(Rwidget_get_visibility(d.widget(names::kDebriefCampaignPanel)) == R_VISIBLE);
  REQUIRE(d.click(names::kDebriefCampaignNext));
  REQUIRE(d.to_briefing());
  CHECK(app.vm().snapshot()->mission == "m01-false-breakout");
  CHECK(d.text(names::kBriefMode).find("2 de 17") != std::string::npos);
  REQUIRE(d.click(names::kBriefBack));
  CHECK(capture(d, window, "shell-03-menu-campaign").saved);
  CHECK(d.text(names::kMenuDetailDone).empty() == false);
}

TEST_CASE("windowed shell: curriculum map, an Academy lesson with its chart, the journal") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  const Rid window = app.view().window();
  Driver d(app);
  d.frames(4);
  REQUIRE(d.click(names::kMenuCurriculum));
  CHECK(capture(d, window, "shell-04-curriculum").saved);
  REQUIRE(d.click(names::kCurriculumLessonStart));
  REQUIRE(d.click(names::kLessonNext));  // anatomy -> interval: the market X window
  const Frame chart = capture(d, window, "shell-05-lesson-chart");
  CHECK(chart.near(0x39, 0xFF, 0x88) > 200);  // up candles
  CHECK(chart.near(0xFF, 0x5F, 0x7A) > 100);  // down candles
  for (int guard = 0; guard < 40 && !app.vm().shell().lesson.finished(); ++guard) {
    const LessonStep* s = app.vm().shell().lesson.step();
    if (s->is_question()) REQUIRE(d.click(names::kLessonChoices[static_cast<std::size_t>(s->question->answer)]));
    REQUIRE(d.click(names::kLessonNext));
  }
  CHECK(capture(d, window, "shell-06-lesson-done").saved);
  REQUIRE(d.click(names::kLessonFinish));
  REQUIRE(d.click(names::kCurriculumBack));
  REQUIRE(d.click(names::kMenuJournal));
  CHECK(capture(d, window, "shell-07-journal").saved);
  CHECK(Rwidget_get_visibility(d.widget(names::kJournalEmpty)) == R_VISIBLE);
}

TEST_CASE("windowed shell: CRT effect on and off, settings and the colorblind palette") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  const Rid window = app.view().window();
  Driver d(app);
  app.vm().set_reduce_motion(false);
  d.frames(4);
  REQUIRE(d.click(names::kMenuSettings));
  const Frame on = capture(d, window, "shell-08-settings-crt-on");
  REQUIRE(d.click(names::kSettingsCrt));
  CHECK_FALSE(app.vm().crt_state().overlay);
  const Frame off = capture(d, window, "shell-09-settings-crt-off");
  REQUIRE(on.saved);
  REQUIRE(off.saved);
  if (app.view().shell().look().shader_ready()) {
    CHECK(changed(on, off) > 0.2);  // scanlines and vignette touch most of the window
  } else {
    MESSAGE("the CRT shader was refused by this Vulkan driver: the overlay stays off");
  }
  REQUIRE(d.click(names::kSettingsPalette));
  CHECK(capture(d, window, "shell-10-settings-colorblind").saved);
  CHECK(app.vm().settings().colorblind);
}
