// README screenshots of the desktop game (docs/screenshots/desktop): real gameplay at 1280x800 in
// English on fixed seeds, CRT off, each capture reduced to 1280 px wide (a Retina capture is twice
// that) and saved as NN-name.png in $TRADELAB_SCREENSHOT_DIR. Without that variable the case does
// nothing, so CI never writes them. README "Screenshots".
#include <doctest/doctest.h>

#include <raven/raven_resources.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "support/academy_flows.hpp"
#include "support/playtest_run.hpp"
#include "support/png.hpp"
#include "support/practice_flows.hpp"
#include "support/strategy_flows.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/game/builder_widgets.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
namespace gn = tradelab::desktop::names::game;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::RavenSession;
using tradelab::desktop::testing::Run;
using tradelab::desktop::testing::Terminal;

namespace {

std::filesystem::path shot_dir() {
  const char* dir = std::getenv("TRADELAB_SCREENSHOT_DIR");
  return dir != nullptr && *dir != '\0' ? std::filesystem::path(dir) : std::filesystem::path();
}

// `rgba` (w x h) reduced by an integer factor (box average) so the image is at most 1280 px wide.
std::vector<std::uint8_t> reduce(const std::uint8_t* rgba, int w, int h, int& out_w, int& out_h) {
  const int f = std::max(1, w / 1280);
  out_w = w / f;
  out_h = h / f;
  std::vector<std::uint8_t> out(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4);
  for (int y = 0; y < out_h; ++y) {
    for (int x = 0; x < out_w; ++x) {
      for (int c = 0; c < 4; ++c) {
        int sum = 0;
        for (int dy = 0; dy < f; ++dy) {
          for (int dx = 0; dx < f; ++dx) sum += rgba[((static_cast<std::size_t>(y) * f + dy) * w + x * f + dx) * 4 + c];
        }
        out[(static_cast<std::size_t>(y) * out_w + x) * 4 + c] = static_cast<std::uint8_t>(sum / (f * f));
      }
    }
  }
  return out;
}

// One app on a fixed seed, English, CRT off.
struct Session {
  RavenSession raven;
  App app;
  std::unique_ptr<Driver> d;
  explicit Session(std::uint32_t seed) : app(builtin_catalog(), [seed] { return seed; }) {
    REQUIRE(app.build(1280, 800));
    app.vm().set_crt(CrtSettings{false, false, false, false});
    d = std::make_unique<Driver>(app);
    d->frames(4);
    app.vm().set_locale(Locale::En);
    d->frames(4);
  }
  void shot(const std::string& name) {
    d->frames(6);
    int w = 0;
    int h = 0;
    std::uint8_t* rgba = Rwindow_capture(app.view().window(), &w, &h);
    REQUIRE(rgba != nullptr);
    int ow = 0;
    int oh = 0;
    const auto small = reduce(rgba, w, h, ow, oh);
    Rpixels_free(rgba);
    CHECK(testing::write_png((shot_dir() / (name + ".png")).string(), small.data(), ow, oh));
  }
};

void menu_briefing_terminal_debrief() {
  Session s(7);
  s.shot("01-menu");
  Run run(s.app, *s.d, "screenshots-m01", 1280, 800);
  s.app.vm().select(0);
  s.d->frames(2);
  REQUIRE(s.d->click(names::kMenuStart));
  REQUIRE(s.d->to_briefing());
  s.shot("02-briefing");
  REQUIRE(s.d->click(names::kBriefGo));
  REQUIRE(s.app.vm().screen() == Screen::Game);
  run.t().skip_question();
  run.advance(4);
  run.order(true, "2", 4, 1, "range breakout with volume");
  run.advance(2);
  s.shot("03-terminal");
  run.play_to_debrief();
  s.shot("07-debrief");
}

void options_chain() {
  Session s(5);
  Run run(s.app, *s.d, "screenshots-m05", 1280, 800);
  run.t().open(4);
  run.t().skip_question();
  run.advance(3);
  run.option(2, 0);  // write a call
  run.advance(2);
  run.t().skip_question();
  REQUIRE(s.d->click(gn::kTabOptions));
  s.shot("04-options");
}

void builder_and_lab() {
  Session s(3);
  Terminal t(s.app, *s.d);
  t.open(testing::mission_index("m16-range-condor"));
  t.skip_question();
  testing::pick_template(s.app, *s.d, 6);  // iron condor
  Rwidget_scroll_into_view(s.d->widget(names::builder::kPayoff));
  s.shot("05-strategy-builder");
  s.app.vm().back_to_menu();
  s.d->frames(4);
  REQUIRE(s.d->click(names::builder::kMenuLab));
  s.shot("06-options-lab");
}

void academy_curriculum_glossary() {
  Session s(1);
  s.app.vm().open_lesson("futures-margin", Screen::Menu);
  s.app.vm().lesson_next();  // the demonstration: a chart with its labelled levels
  s.d->frames(4);
  s.shot("08-academy-lesson");
  s.app.vm().back_to_menu();
  s.d->frames(4);
  REQUIRE(s.d->click(names::kMenuCurriculum));
  s.shot("09-curriculum");
  s.app.vm().back_to_menu();
  s.d->frames(4);
  REQUIRE(s.d->click(names::kMenuGlossary));
  s.d->type_into(names::kGlossarySearch, "liquidation");
  s.d->focus(names::kGlossaryList);
  s.d->key(R_KEY_DOWN);
  s.shot("10-glossary");
}

// The practice formats on 2026-11-09 (the drills, the daily challenge and the guess rounds follow the
// local date): the first drill answered wrong, the day's challenge, the first guess round's reveal and
// the coach's first moment of m24 chased (38 bars waited, then 40 bought with no plan or stop).
void practice_formats() {
  Session s(3);
  const std::int64_t noon = practice::day_start("2026-11-09") + 12 * 3'600'000LL;
  s.app.vm().set_clock([noon] { return noon; });
  AppViewModel& vm = s.app.vm();
  testing::press(*s.d, names::kMenuDrills);
  testing::press(*s.d, names::kDrillsStart);
  s.d->type_into(names::kDrillsInput, "2.6");
  s.d->key(R_KEY_ENTER);
  s.shot("12-drills");
  vm.back_to_menu();
  s.d->frames(4);
  testing::press(*s.d, names::kMenuDaily);
  s.shot("13-daily");
  vm.back_to_menu();
  s.d->frames(4);
  testing::press(*s.d, names::kMenuGuess);
  testing::press(*s.d, names::kGuessStart);
  const GuessState& g = vm.practice().guess;
  REQUIRE(s.d->until([&] { return g.phase == GuessState::Phase::Commit; }));
  vm.guess_set_p(13);  // 70%
  s.d->type_into(names::kGuessInv, "89.08");
  s.d->type_into(names::kGuessTarget, "93.58");
  testing::press(*s.d, names::kGuessCommit);
  REQUIRE(s.d->until([&] { return g.phase == GuessState::Phase::Reveal; }));
  Rwidget_scroll_into_view(s.d->widget(names::kGuessReveal));
  s.shot("14-guess");
  vm.back_to_menu();
  s.d->frames(4);
  Terminal t(s.app, *s.d);
  testing::flow_chase(s.app, *s.d, t);
  testing::press(*s.d, names::kDebriefOpenReview);
  REQUIRE(s.d->until([&] { return vm.practice().coach.review.has_value(); }));
  s.shot("15-coach-review");
}

}  // namespace

TEST_CASE("screenshots: README images of the desktop game (only with TRADELAB_SCREENSHOT_DIR)") {
  if (shot_dir().empty()) return;
  std::filesystem::create_directories(shot_dir());
  menu_briefing_terminal_debrief();
  options_chain();
  builder_and_lab();
  academy_curriculum_glossary();
  practice_formats();
}
