// Windowed playthroughs of this change's shell flows on the Vulkan renderer (lavapipe under Xvfb on
// Linux, MoltenVK on macOS): the tutorial before the briefing, assisted practice, the debrief chart and
// lesson results, the CSV import, the pack catalog download, and runs resumed by a later launch. Frames
// are saved as PNG in $TRADELAB_CAPTURE_DIR; the debrief chart is checked for drawn candles.
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "support/png.hpp"
#include "model/json.hpp"
#include "support/run_file_flows.hpp"
#include "support/shell_flows.hpp"

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

bool phosphor(const std::uint8_t* p) {
  return std::abs(p[0] - 0x39) < 40 && std::abs(p[1] - 0xFF) < 40 && std::abs(p[2] - 0x88) < 40;
}

// Up-candle (phosphor green) pixels of a frame inside `widget` (window points scaled to frame pixels: a
// Retina frame is larger than the window).
int green_in(Driver& d, std::string_view widget, const std::uint8_t* rgba, int width, int height) {
  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(d.widget(widget), &x, &y, &w, &h);
  int ww = 0, wh = 0;
  Rwindow_get_size(d.window(), &ww, &wh);
  const float sx = ww > 0 ? static_cast<float>(width) / static_cast<float>(ww) : 1.0f;
  const float sy = wh > 0 ? static_cast<float>(height) / static_cast<float>(wh) : 1.0f;
  const int x1 = std::min(width, static_cast<int>((x + w) * sx));
  const int y1 = std::min(height, static_cast<int>((y + h) * sy));
  int green = 0;
  for (int py = static_cast<int>(y * sy); py < y1; ++py) {
    for (int px = static_cast<int>(x * sx); px < x1; ++px) green += phosphor(rgba + (static_cast<std::ptrdiff_t>(py) * width + px) * 4) ? 1 : 0;
  }
  return green;
}

// Saves the frame; returns the count of up-candle pixels inside `widget` (0 without one).
int capture(Driver& d, const std::string& name, std::string_view widget = {}) {
  d.frames(3);
  int width = 0;
  int height = 0;
  std::uint8_t* rgba = Rwindow_capture(d.window(), &width, &height);
  if (rgba == nullptr) return -1;
  CHECK(testing::write_png((capture_dir() / (name + ".png")).string(), rgba, width, height));
  const int green = widget.empty() ? 0 : green_in(d, widget, rgba, width, height);
  Rpixels_free(rgba);
  return green;
}

testing::Shot shooter(Driver& d) {
  return [&d](const std::string& name) { capture(d, name); };
}

}  // namespace

TEST_CASE("windowed flows: the tutorial before the first briefing") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  testing::flow_tutorial(app, d, shooter(d));
}

TEST_CASE("windowed flows: assisted practice from the terminal and the debrief") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 2u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  Terminal t(app, d);
  testing::flow_assisted(app, d, t, shooter(d));
}

TEST_CASE("windowed flows: the debrief chart is drawn, with its candle table and lesson results") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 3u; });
  REQUIRE(app.build(1280, 800));
  // CRT off: its scanlines, bloom and vignette tint every pixel, so the candles' phosphor green is only
  // found when the system asks for reduced motion (which turns the CRT off, as on the hosted macOS
  // runners) and the check depended on that setting.
  app.vm().set_crt(CrtSettings{false, false, false, false});
  Driver d(app);
  d.frames(4);
  Terminal t(app, d);
  testing::flow_debrief_chart_and_lessons(app, d, t, shooter(d));
  Rwidget_scroll_into_view(d.widget(names::kDebriefChart));
  d.frames(3);
  CHECK(capture(d, "flow-debrief-chart-pixels", names::kDebriefChart) > 100);  // up candles painted
}

TEST_CASE("windowed flows: CSV import with a gap, then practice on the pack") {
  testing::TempDir dir;
  const auto csv = testing::write_gap_csv(dir.path());
  RavenSession raven;
  App app(builtin_catalog(), [] { return 4u; }, std::make_unique<storage::Storage>(dir.path() / "data"));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  testing::flow_csv_import(app, d, csv, shooter(d));
}

TEST_CASE("windowed flows: catalog download cut, resumed, verified, installed and played") {
  testing::TempDir dir;
  testing::CatalogServer server;
  RavenSession raven;
  App app(builtin_catalog(), [] { return 5u; }, std::make_unique<storage::Storage>(dir.path()));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  testing::flow_catalog_download(app, d, server, shooter(d));
}

TEST_CASE("windowed flows: progress, settings and a campaign mission come back in the next launch") {
  testing::TempDir dir;
  testing::flow_relaunch_campaign<RavenSession>(dir.path());
}

TEST_CASE("windowed flows: a pack run saved in one launch resumes in the next") {
  testing::TempDir dir;
  const auto csv = testing::write_gap_csv(dir.path());
  testing::flow_relaunch_pack<RavenSession>(dir.path() / "data", csv);
}

TEST_CASE("windowed flows: run files, a tampered file refused, the shared file replayed on the terminal to its debrief") {
  testing::TempDir dir;
  RavenSession raven;
  App app(builtin_catalog(), [] { return 11u; }, std::make_unique<storage::Storage>(dir.path() / "data"));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  const auto index = json::parse(testing::read_text(testing::runfile_path("index.json")));
  std::string digest;
  for (const json::Value& e : (*index)["files"].items()) {
    if (e["name"].as_string() == "m01-finished.tlrun") digest = e["expected"]["digest"].as_string();
  }
  testing::flow_run_files(app, d, digest, shooter(d));
}
