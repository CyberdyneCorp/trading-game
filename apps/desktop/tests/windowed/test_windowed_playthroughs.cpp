// Windowed playthroughs of tasks 1.15 and 1.17 on the real renderer: the chart with 5,000 bars measured
// with RFrameStats, a language switch in the middle of a run (the digest does not move), OFF01 (no
// network: the update check fails quietly and a mission plays to its debrief and is saved), and
// reference images compared with a tolerance (tests/windowed/reference, support/reference.hpp).
#include <doctest/doctest.h>

#include <raven/raven_core.h>
#include <raven/raven_resources.h>
#include <raven/raven_window.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "model/packs/http.hpp"
#include "model/storage/storage.hpp"
#include "model/update/update_service.hpp"
#include "support/driver.hpp"
#include "support/fake_engine.hpp"
#include "support/game_flows.hpp"
#include "support/png.hpp"
#include "support/reference.hpp"
#include "support/storage_fixtures.hpp"
#include "support/window_size.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
namespace gn = tradelab::desktop::names::game;
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

std::filesystem::path capture_dir() {
  const char* dir = std::getenv("TRADELAB_CAPTURE_DIR");
  std::filesystem::path p = dir != nullptr && *dir != '\0' ? dir : "captures";
  std::filesystem::create_directories(p);
  return p;
}

// A terminal state with `n` closed bars and the three default indicator lines, every bar drawn.
GameState long_market(int n) {
  GameState g = testing::base_state(n - 1);
  g.total_bars = n + 20;
  g.last_decision_step = n + 18;
  for (int i = 0; i < n; ++i) {
    const double p = 100 + 8 * std::sin(i / 40.0) + (i % 7) * 0.3;
    g.bars[static_cast<std::size_t>(i)] = {p, p + 1.2, p - 1.1, p + ((i % 3) - 1) * 0.4, 40.0 + (i % 11), true, false};
    g.indicators.sma20.emplace_back(p);
    g.indicators.ema50.emplace_back(p - 0.5);
    g.indicators.rsi14.emplace_back(50 + 20 * std::sin(i / 15.0));
  }
  g.account.mark = g.bars.back().close;
  return g;
}

double average(const std::vector<double>& v) {
  double sum = 0;
  for (const double x : v) sum += x;
  return v.empty() ? 0 : sum / static_cast<double>(v.size());
}

constexpr double kFrameBudgetMs = 1000.0 / 60;

bool software_renderer() {
  const char* icd = std::getenv("VK_ICD_FILENAMES");
  return icd != nullptr && std::string(icd).find("lvp") != std::string::npos;
}

}  // namespace

// Design "Chart": 5,000 bars at 60 fps, measured from RFrameStats. Each frame moves the crosshair, so the
// chart's scene is rebuilt and painted every frame. The 60 fps budget applies to the app's CPU-side work:
// the chart's display list and layout + paint (which includes it) must each fit in a frame everywhere.
// The render time (Vulkan submit and present) is reported, never asserted: it depends on the device
// (lavapipe renders on the CPU: about 43 ms on CI's Linux job) and on presentation (with vsync, a
// hosted macOS runner reports about one refresh interval, 16.4 ms), not on this code.
TEST_CASE("windowed: the chart draws 5,000 bars within a 60 fps frame (RFrameStats)") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  t.open(0);
  t.skip_question();
  auto snap = std::make_shared<Snapshot>(*testing::snapshot_of(long_market(5000), CommandResult{CommandKind::Advance, true, "none"}));
  snap->results.clear();
  app.vm().on_snapshot(snap);
  game::GameViewModel& vm = app.vm().game();
  REQUIRE(vm.state()->bars.size() == 5000);
  while (vm.chart.zoom() < 5000) vm.chart.wheel(*vm.state(), -1);  // every bar on screen
  d.frames(4);

  float x = 0, y = 0, w = 0, h = 0;
  Rwidget_get_rect(d.widget(names::kGameChart), &x, &y, &w, &h);
  REQUIRE(w > 400);
  std::vector<double> app_ms, frame_ms;
  for (int i = 0; i < 120; ++i) {
    Rwindow_inject_mouse_move(d.window(), x + 20 + static_cast<float>(i % 100) * (w - 120) / 100, y + h / 3);
    d.frames(1);
    RFrameStats s{};
    Rwindow_get_frame_stats(d.window(), &s);
    if (i < 20) continue;  // warm-up: caches, pipelines
    app_ms.push_back(s.layout_ms + s.paint_ms);
    frame_ms.push_back(s.layout_ms + s.paint_ms + s.render_ms);
  }
  // The chart's own work per frame: the display list of 5,000 bars (what paint_ms includes for the chart).
  std::vector<double> build_ms;
  for (int i = 0; i < 30; ++i) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto scene = vm.chart_scene(w, h);
    build_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    REQUIRE_FALSE(scene.empty);
    REQUIRE(scene.lines.size() > 3 * 4900);  // the three indicator lines across every bar
  }
  const double build_avg = average(build_ms);
  const double app_avg = average(app_ms);
  const double frame_avg = average(frame_ms);
  std::ofstream(capture_dir() / "chart-5000-bars.txt")
      << "bars 5000, chart " << w << "x" << h << ", 100 frames\n"
      << "chart display list avg " << build_avg << " ms\n"
      << "layout+paint avg " << app_avg << " ms, max " << *std::max_element(app_ms.begin(), app_ms.end()) << " ms\n"
      << "layout+paint+render avg " << frame_avg << " ms" << (software_renderer() ? " (software renderer)" : "")
      << " (reported only)\n";
  MESSAGE("5,000 bars: display list " << build_avg << " ms, layout+paint " << app_avg << " ms (budget " << kFrameBudgetMs
                                      << " ms); with render " << frame_avg << " ms (reported only)");
  CHECK(build_avg < kFrameBudgetMs);
  CHECK(app_avg < kFrameBudgetMs);
}

namespace {

// Plays mission 01 (seed 4): a long typed in the locale's way, five bars, optionally a switch to English
// through the menu (the run is left and resumed), another long typed with the new locale's decimal mark,
// five more bars. Returns the digest of the run.
std::string play_with_switch(bool switch_language) {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 4u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  t.open(0);
  t.skip_question();
  d.choose(gn::kSizeBy, 1);  // quantity
  d.type_into(names::kGameQty, "2,5");
  REQUIRE(d.click(names::kGameBuy));
  t.submit_with_plan();
  for (int i = 0; i < 5; ++i) t.advance();
  std::string qty = "1,5";
  if (switch_language) {
    const std::string before = app.vm().snapshot()->digest;
    REQUIRE(d.click(names::kGameMenu));
    REQUIRE(app.vm().screen() == Screen::Menu);
    REQUIRE(d.click(names::kMenuLanguage));
    CHECK(app.vm().locale() == Locale::En);
    REQUIRE(d.click(names::kMenuResume));
    REQUIRE(d.until([&] { return app.vm().screen() == Screen::Game; }));
    REQUIRE(d.idle());
    CHECK(app.vm().snapshot()->digest == before);  // texts change, the run does not
    CHECK(d.text(names::kGameAccount).starts_with("Equity 10,"));
    CHECK(d.text(names::kGameAccount).find("LONG 2.5") != std::string::npos);
    qty = "1.5";  // English decimal point
    t.skip_question();
    d.frames(2);
    int w = 0, h = 0;
    if (std::uint8_t* rgba = Rwindow_capture(d.window(), &w, &h)) {
      testing::write_png((capture_dir() / "20-locale-switched.png").string(), rgba, w, h);
      Rpixels_free(rgba);
    }
  }
  d.choose(gn::kSizeBy, 1);
  d.type_into(names::kGameQty, qty);
  REQUIRE(d.click(names::kGameBuy));  // adds to the long: an entry, so it asks for its plan
  t.submit_with_plan();
  for (int i = 0; i < 5; ++i) t.advance();
  CHECK(app.vm().game().state()->position.qty == doctest::Approx(4.0));
  return app.vm().snapshot()->digest;
}

}  // namespace

// tasks 1.17 "locale switch mid-run (digest unchanged)".
TEST_CASE("windowed: switching to English in the middle of a run leaves the run's digest as without it") {
  const std::string switched = play_with_switch(true);
  const std::string plain = play_with_switch(false);
  CHECK_FALSE(switched.empty());
  CHECK(switched == plain);
}

// OFF01 (no network): every network use fails (the update manifest's host refuses connections), the
// check ends quietly with no offer, and a mission plays from the menu to its debrief and is saved.
TEST_CASE("windowed: OFF01, offline, the update check fails quietly and a mission plays to its saved debrief") {
  testing::TempDir dir;
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; }, std::make_unique<storage::Storage>(dir.path()));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  update::UpdateState last;
  std::unique_ptr<update::UpdateService> updates;
  if (auto http = packs::CurlHttpClient::load()) {
    update::Config config;
    config.manifest_url = "https://127.0.0.1:9/desktop-manifest.json";  // the discard port: nothing listens
    config.public_key = *update::public_key_from_hex(std::string(64, 'a'));
    config.current_version = "0.1.0";
    updates = std::make_unique<update::UpdateService>(
        std::move(http), config, dir.path() / "updates", update::Target{update::TargetKind::File, dir.path() / "TradeLab.AppImage"},
        [](update::UpdateService::Task task) { ui::post(std::move(task)); },
        [&](const update::UpdateState& s) {
          last = s;
          app.vm().on_update(s);
        });
    app.vm().attach_updates(updates.get());
    updates->check();
    REQUIRE(d.until([&] { return last.status != update::UpdateState::Status::Off && last.status != update::UpdateState::Status::Checking; }));
    CHECK((last.status == update::UpdateState::Status::Unavailable || last.status == update::UpdateState::Status::Failed));
  } else {
    MESSAGE("no libcurl on this machine: the client is offline by construction");
  }
  d.frames(2);
  CHECK_FALSE(Rwidget_get_visibility(d.widget(names::kMenuUpdateRow)) == R_VISIBLE);
  Terminal t(app, d);
  t.open(0);
  while (app.vm().screen() == Screen::Game) {
    t.skip_question();
    d.focus(names::kGameAdvance);
    d.key(R_KEY_SPACE, R_MOD_SHIFT);
    REQUIRE(d.idle());
  }
  REQUIRE(app.vm().screen() == Screen::Debrief);
  CHECK(app.vm().snapshot()->durable);  // every action and the ended run's header were committed
  CHECK(std::filesystem::file_size(dir.path() / "tradelab.db") > 0);
}

namespace {

// The capture of the window, reduced and compared with its reference. Only a window of the references'
// size (1280x800) is compared or written: another size lays the screen out differently.
void reference(Driver& d, const std::string& name, bool reference_size) {
  d.frames(3);
  if (!reference_size) return;
  int w = 0, h = 0;
  std::uint8_t* rgba = Rwindow_capture(d.window(), &w, &h);
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

}  // namespace

// tasks 1.17 "tolerance-checked reference images": the menu, a briefing and the terminal after a fixed
// script (seed 1, CRT off so only the interface is compared), the same references on Linux and macOS, in
// a 1280x800 window (support/window_size.hpp: skipped on a screen too small for it).
TEST_CASE("windowed: the menu, a briefing and the terminal match their reference images") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  app.vm().set_crt(CrtSettings{false, false, false, false});
  Driver d(app);
  d.frames(4);
  const bool reference_size = testing::window_has_size(d.window(), 1280, 800);
  reference(d, "menu", reference_size);
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.to_briefing());  // past the first-start tutorial
  reference(d, "briefing-m01", reference_size);
  REQUIRE(d.click(names::kBriefGo));
  Terminal t(app, d);
  t.skip_question();
  for (int i = 0; i < 10; ++i) t.advance();
  d.focus(names::kGameAdvance);
  Rwindow_inject_mouse_move(d.window(), 1, 1);  // no hover readout
  reference(d, "terminal-m01", reference_size);
}
