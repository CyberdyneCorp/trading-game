// Tutorial card art of the strategy missions (web TutorialArt.svelte: spread, covered, insurance, condor,
// roll): a display list on the 160x100 view box scaled into the widget, its labels in the locale.
#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "model/i18n.hpp"
#include "viewmodel/game/dialogs_vm.hpp"
#include "viewmodel/game/tutorial_art.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;

namespace {

bool has_text(const ChartScene& s, const std::string& text) {
  return std::any_of(s.texts.begin(), s.texts.end(), [&](const SceneText& t) { return t.text.find(text) != std::string::npos; });
}

}  // namespace

TEST_CASE("tutorial art: the strategy cards' drawings stay in their box and name their parts in the locale") {
  const Catalog catalog = builtin_catalog();
  const Texts t(catalog);
  for (const char* art : {"spread", "covered", "insurance", "condor", "roll"}) {
    INFO(art);
    CHECK(has_tutorial_art(art));
    const ChartScene s = tutorial_art_scene(t, art, 240, 150);
    CHECK_FALSE(s.empty);
    CHECK(s.lines.size() >= 4);
    CHECK_FALSE(s.texts.empty());
    for (const auto& l : s.lines) {
      CHECK(std::min(l.x0, l.x1) >= 0);
      CHECK(std::max(l.x0, l.x1) <= 240);
      CHECK(std::min(l.y0, l.y1) >= 0);
      CHECK(std::max(l.y0, l.y1) <= 150);
    }
    // The payoff curve is the accent line (the web's .payoff), except the roll's arrows.
    if (std::string(art) != "roll") CHECK(std::any_of(s.lines.begin(), s.lines.end(), [](const SceneLine& l) { return l.ink == Ink::Up && l.width >= 2; }));
  }
  CHECK(has_text(tutorial_art_scene(t, "spread", 240, 150), "teto"));
  CHECK(has_text(tutorial_art_scene(t, "insurance", 240, 150), "piso"));
  CHECK(has_text(tutorial_art_scene(t, "roll", 240, 150), "seguinte"));
  CHECK(has_text(tutorial_art_scene(t, "condor", 240, 150), "strangle \xC2\xB7 aberta"));
  // Kept to the box's aspect: a wide box centres the 160x100 drawing.
  const ChartScene wide = tutorial_art_scene(t, "spread", 480, 150);
  for (const auto& l : wide.lines) CHECK(std::min(l.x0, l.x1) >= 480 / 2 - 120);
  CHECK_FALSE(has_tutorial_art("chart"));
  CHECK(tutorial_art_scene(t, "chart", 240, 150).empty);
}

TEST_CASE("tutorial art: each strategy mission's first card carries one of the drawn keys") {
  const Catalog catalog = builtin_catalog();
  const Texts t(catalog);
  for (const char* m : {"m13-call-spread", "m14-covered-call", "m15-protective-put", "m16-range-condor", "m17-before-expiry"}) {
    INFO(m);
    const auto cards = tutorial_cards(t, m);
    REQUIRE_FALSE(cards.empty());
    CHECK(has_tutorial_art(cards[0].art));
  }
}
