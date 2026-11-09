// Tutorial card art of the strategy missions (web TutorialArt.svelte: spread, covered, insurance, condor,
// roll): a display list on the 160x100 view box scaled into the widget, its labels in the locale.
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "model/i18n.hpp"
#include "viewmodel/game/dialogs_vm.hpp"
#include "viewmodel/game/tutorial_art.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;

namespace {

bool has_text(const ChartScene& s, const std::string& text) {
  return std::any_of(s.texts.begin(), s.texts.end(), [&](const SceneText& t) { return t.text.find(text) != std::string::npos; });
}

// A label's glyphs: its advance width (0.6 em a character, as the painter lays it out), from just below
// the cap line to the baseline, inset by one unit so a line merely touching the box does not count.
struct Box {
  float x0, y0, x1, y1;
};

Box ink_box(const SceneText& label) {
  const auto chars = static_cast<float>(std::count_if(label.text.begin(), label.text.end(), [](char ch) { return (ch & 0xC0) != 0x80; }));
  const float cap = label.size * 0.8f;
  return {label.x + 1, label.y + cap * 0.3f, label.x + chars * label.size * 0.6f - 1, label.y + cap - 0.5f};
}

// Whether a drawn segment (with its stroke width) passes through the box, sampled along its length.
bool crosses(const SceneLine& l, const Box& b) {
  const float pad = l.width / 2;
  for (int i = 0; i <= 200; ++i) {
    const float k = static_cast<float>(i) / 200;
    const float px = l.x0 + (l.x1 - l.x0) * k;
    const float py = l.y0 + (l.y1 - l.y0) * k;
    if (px >= b.x0 - pad && px <= b.x1 + pad && py >= b.y0 - pad && py <= b.y1 + pad) return true;
  }
  return false;
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

// Regression: "−prêmio" sat on the insurance payoff, "strangle · aberta" on the strangle's dashed line,
// "teto" on the covered call's long and "2 × spread" on the roll's near expiry (web parity:
// missions-batch-c.spec.ts checks the same on the SVG). Drawn at scale 1, so units are view-box units.
TEST_CASE("tutorial art: no label of a strategy card crosses a drawn line, in either locale") {
  for (const Locale locale : {Locale::PtBR, Locale::En}) {
    Catalog catalog = builtin_catalog();
    catalog.set_locale(locale);
    const Texts t(catalog);
    for (const char* art : {"spread", "covered", "insurance", "condor", "roll"}) {
      const ChartScene s = tutorial_art_scene(t, art, 160, 100);
      for (const auto& label : s.texts) {
        const Box box = ink_box(label);
        INFO(locale_code(locale), " ", std::string(art), " \"", label.text, "\"");
        CHECK_FALSE(std::any_of(s.lines.begin(), s.lines.end(), [&](const SceneLine& l) { return crosses(l, box); }));
      }
    }
  }
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

// add-practice-and-advanced-strategies W2-D3: the m18-m25 cards' drawings (the seven new ones and the
// earlier ones these missions reuse) as display lists; every label clear of every line in both locales.
// Regression: the straddle's "−prêmio" sat on the payoff's right arm (x 84 at the V's foot), its
// "strike" on "breakeven" in PT-BR, and the trailing card's TRAIL on a candle.
namespace {
const std::vector<std::string> kWave2Art = {"scalp", "carry", "parity", "crush", "calendarSpread", "scale", "cooldown",
                                            "straddle", "decay", "trail", "risk", "plan", "margin", "funding", "volatility"};
}

TEST_CASE("tutorial art: the wave 2 cards stay in their box and no label crosses a line, in either locale") {
  for (const Locale locale : {Locale::PtBR, Locale::En}) {
    Catalog catalog = builtin_catalog();
    catalog.set_locale(locale);
    const Texts t(catalog);
    for (const auto& art : kWave2Art) {
      INFO(locale_code(locale), " ", art);
      REQUIRE(has_tutorial_art(art));
      const ChartScene s = tutorial_art_scene(t, art, 160, 100);
      CHECK_FALSE(s.empty);
      for (const auto& l : s.lines) {
        CHECK(std::min(l.x0, l.x1) >= 0);
        CHECK(std::max(l.x0, l.x1) <= 160);
        CHECK(std::min(l.y0, l.y1) >= 0);
        CHECK(std::max(l.y0, l.y1) <= 100);
      }
      for (const auto& label : s.texts) {
        const Box box = ink_box(label);
        INFO("\"", label.text, "\"");
        CHECK(box.x0 >= 0);
        CHECK(box.x1 <= 160);
        CHECK_FALSE(std::any_of(s.lines.begin(), s.lines.end(), [&](const SceneLine& l) { return crosses(l, box); }));
      }
    }
  }
}

TEST_CASE("tutorial art: the wave 2 cards name their parts in the locale and every m18-m25 card is drawn") {
  const Catalog catalog = builtin_catalog();
  const Texts t(catalog);
  CHECK(has_text(tutorial_art_scene(t, "carry", 240, 150), "funding recebido"));
  CHECK(has_text(tutorial_art_scene(t, "crush", 240, 150), "divulgação"));
  CHECK(has_text(tutorial_art_scene(t, "calendarSpread", 240, 150), "longo"));
  CHECK(has_text(tutorial_art_scene(t, "cooldown", 240, 150), "esperar"));
  CHECK(has_text(tutorial_art_scene(t, "scalp", 240, 150), "hedge"));
  CHECK(has_text(tutorial_art_scene(t, "parity", 240, 150), "C \xE2\x88\x92 P = F \xE2\x88\x92 K"));
  for (const char* m : {"m18-gamma-scalp", "m19-funding-carry", "m20-event-vol", "m21-calendar", "m22-scale-manage",
                        "m23-second-chance", "m24-fast-market", "m25-the-dip"}) {
    const auto cards = tutorial_cards(t, m);
    REQUIRE(cards.size() >= 3);
    for (const auto& card : cards) {
      INFO(m, " ", card.art);
      CHECK((has_tutorial_art(card.art) || card.art == "book"));  // the order book card is not drawn on the desktop yet
    }
  }
}
