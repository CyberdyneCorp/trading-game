// Windowed references of the practice formats (add-practice-and-advanced-strategies W3-D4) on the Vulkan
// renderer: the first drill of 2026-11-09 answered wrong (its worked explanation), the first guess round
// of that date at its decision, and the coach's first moment of m24 chased on seed 3 (the chart cut at
// bar 98). The device clock is fixed to 2026-11-09 so the date-drawn screens compare; 1280x800, CRT off.
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "support/practice_flows.hpp"
#include "support/reference.hpp"
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
    const std::int64_t noon = practice::day_start("2026-11-09") + 12 * 3'600'000LL;
    app.vm().set_clock([noon] { return noon; });
    d = std::make_unique<Driver>(app);
    d->frames(4);
    t = std::make_unique<Terminal>(app, *d);
  }
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

TEST_CASE("windowed references: a drill's worked explanation, a guess round's decision, a coach moment (1280x800)") {
  {
    Session s;
    testing::flow_drills(s.app, *s.d, [&s](const std::string& label, std::string_view) {
      if (label == "w3-drills-wrong") s.reference("drills-wrong");
    });
  }
  {
    Session s;
    testing::flow_guess(s.app, *s.d, 2, 50, 0, 0, [&s](const std::string& label, std::string_view) {
      if (label == "w3-guess-commit") s.reference("guess-commit");
    });
  }
  Session s;
  testing::flow_chase(s.app, *s.d, *s.t);
  testing::flow_coach(s.app, *s.d, [&s](const std::string& label, std::string_view) {
    if (label == "w3-review-1") s.reference("review-moment");
  });
}
