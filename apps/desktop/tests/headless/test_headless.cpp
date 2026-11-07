// Headless view tests (design "Testing"): Raven's null platform and null backend, no window system and
// no GPU. Every XML view loads, every named widget and action exists and is bound, and injected input
// plays mission 01 from the menu to the debrief.
#include <doctest/doctest.h>

#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "platform/audio_out.hpp"
#include "view/app.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;
namespace gn = tradelab::desktop::names::game;

namespace {

struct RavenSession {
  RavenSession() { REQUIRE(init_headless()); }
  ~RavenSession() { Rshutdown(); }
};

}  // namespace

TEST_CASE("headless: every view loads and binds its widgets and actions") {
  RavenSession raven;
  {
    App app(builtin_catalog(), [] { return 1u; });
    REQUIRE(app.build());
    CHECK(app.view().missing_names().empty());
    for (std::string_view w : app.view().missing_names()) MESSAGE("missing: " << w);

    // Every button fires one of the bound actions.
    for (std::string_view name : names::kWidgets) {
      const Rid w = app.view().find(name);
      if (Rwidget_get_type(w) != R_WIDGET_BUTTON) continue;
      const char* action = Rwidget_get_action(w);
      INFO("button " << name);
      REQUIRE(action != nullptr);
      CHECK(ui::action_bound(action));
    }
    Driver d(app);
    d.frames(2);
    CHECK(d.text(names::kMenuDetailTitle) == "Missão 01 · Range");
    CHECK(Rlist_view_get_item_count(app.view().find(names::kMenuList)) == 17);
  }
}

TEST_CASE("headless: mission 01 from the menu to the debrief with injected input") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build());
  Driver d(app);
  d.frames(2);

  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.to_briefing());
  CHECK(d.text(names::kBriefCapital) == "Capital virtual: 10.000,00 cr");

  REQUIRE(d.click(names::kBriefGo));
  REQUIRE(app.vm().screen() == Screen::Game);
  CHECK(d.text(names::kGameTurn) == "Barra 1 de 79");

  // A calibration question can be due at the first decision step: skipped as a player would.
  Terminal t(app, d);
  t.skip_question();

  // Comma input: "1,5" contracts sized by quantity, bought at market after the plan (skipped).
  d.choose(gn::kSizeBy, 1);
  d.type_into(names::kGameQty, "1,5");
  CHECK(app.vm().qty_text() == "1,5");
  REQUIRE(d.click(names::kGameBuy));
  t.submit_with_plan();

  // Space advances one bar (focus off the text fields); the order fills at the next bar's open.
  t.advance();
  CHECK(d.text(names::kGameAccount).find("COMPRADO 1,5") != std::string::npos);
  CHECK(d.text(names::kGameTurn) == "Barra 2 de 79");
  int presses = 1;
  while (app.vm().screen() == Screen::Game && presses < 200) {
    t.advance();
    ++presses;
  }
  CHECK(presses == 79);
  REQUIRE(app.vm().screen() == Screen::Debrief);
  CHECK(d.text(names::kDebriefTitle) == "Missão concluída");
  CHECK(d.text(names::kDebriefScore).find("/100") != std::string::npos);

  REQUIRE(d.click(names::kDebriefMenu));
  CHECK(app.vm().screen() == Screen::Menu);
}

TEST_CASE("headless: Space typed in the quantity field types, it does not advance") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 2u; });
  REQUIRE(app.build());
  Driver d(app);
  d.frames(2);
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.to_briefing());
  REQUIRE(d.click(names::kBriefGo));
  Terminal(app, d).skip_question();
  const int step = app.vm().snapshot()->step;
  REQUIRE(d.click(names::kGameQty));
  d.key(R_KEY_SPACE);
  d.frames(5);
  CHECK_FALSE(app.vm().waiting());
  CHECK(app.vm().snapshot()->step == step);
}

TEST_CASE("headless: the language button switches every text to en and back") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build());
  Driver d(app);
  d.frames(2);
  REQUIRE(d.click(names::kMenuLanguage));
  CHECK(app.vm().locale() == Locale::En);
  CHECK(d.text(names::kMenuDetailTitle) == "Mission 01 · Range");
  CHECK(std::string(Rlist_view_get_item_text(app.view().find(names::kMenuList), 0)) == "Mission 01 · Range");
  REQUIRE(d.click(names::kMenuLanguage));
  CHECK(d.text(names::kMenuDetailTitle) == "Missão 01 · Range");
}

// The web's soundtrack on miniaudio (platform/audio_out): every track decodes on the null device, which
// mixes like a real one without being heard; a missing file is no player, not a failure.
TEST_CASE("headless: the web's tracks open and play on miniaudio's null device") {
  auto out = platform::MiniaudioOut::create(TRADELAB_WEB_AUDIO_DIR, true);
  REQUIRE(out != nullptr);
  for (const char* file : {"menu-1.mp3", "menu-2.mp3", "game-1.mp3", "game-2.mp3", "tense-1.mp3", "tense-2.mp3",
                           "trade-win.mp3", "trade-loss.mp3"}) {
    CAPTURE(file);
    auto track = out->open(file, !std::string_view(file).starts_with("trade"));
    REQUIRE(track != nullptr);
    track->set_volume(0.5F);
    CHECK(track->volume() == doctest::Approx(0.5));
    track->play();
    CHECK_FALSE(track->finished());
    track->pause();
  }
  CHECK(out->open("missing.mp3", true) == nullptr);
  CHECK(platform::audio_dir(TRADELAB_WEB_AUDIO_DIR) == TRADELAB_WEB_AUDIO_DIR);
  CHECK(platform::audio_dir("/nonexistent").empty());
}
