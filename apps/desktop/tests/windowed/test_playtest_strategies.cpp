// Playtest of the Strategies stage (add-academy-and-strategies D9, X1): the builder's seven templates with
// their analysis, the terminal and Academy labs with their sliders, m13–m17 each played with a structure
// to the debrief, and a campaign through the Strategies stage, in PT-BR and en and two window sizes. Each
// run leaves captures and a text report in $TRADELAB_CAPTURE_DIR/playtest (the builder's and the lab's
// texts after each step, the account line, the debrief), compared with the web for the same seed and
// actions (README "Playtest"); no text may be cut by its panel.
#include <doctest/doctest.h>

#include <raven/raven_widget.h>

#include <string>
#include <utility>

#include "support/playtest_run.hpp"
#include "support/strategy_flows.hpp"
#include "viewmodel/game/builder_widgets.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
namespace bn = tradelab::desktop::names::builder;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::RavenSession;
using tradelab::desktop::testing::Run;

namespace {

struct Window {
  Locale locale;
  int width;
  int height;
};

// A fresh app on a fixed seed, CRT off (the captures are read for their numbers).
struct Player {
  RavenSession raven;
  App app;
  std::unique_ptr<Driver> d;
  std::unique_ptr<Run> run;
  Player(std::uint32_t seed, Window w, const std::string& name) : app(builtin_catalog(), [seed] { return seed; }) {
    REQUIRE(app.build(w.width, w.height));
    app.vm().set_crt(CrtSettings{false, false, false, false});
    d = std::make_unique<Driver>(app);
    d->frames(4);
    app.vm().set_locale(w.locale);
    d->frames(2);
    run = std::make_unique<Run>(app, *d, name, w.width, w.height);
    run->report() << name << " seed " << seed << " " << w.width << "x" << w.height << "\n";
  }
  testing::StrategyShot shot() {
    return [this](const std::string& label) { run->shot(label); };
  }
  void dump(const std::string& title, std::string_view widget) {
    d->frames(3);
    run->report() << "---- " << title << "\n" << testing::dump_texts(d->widget(widget));
  }
};

// A slider moved by its arrow keys from `from` to `to`, then the engine's answer awaited.
void slide(Player& p, std::string_view slider, int from, int to, const std::function<bool()>& answered) {
  p.d->focus(slider);
  for (int v = from; v != to; v += to > from ? 1 : -1) p.d->key(to > from ? R_KEY_RIGHT : R_KEY_LEFT);
  REQUIRE(p.d->until(answered));
}

}  // namespace

TEST_CASE("playtest strategies: the builder's templates on m13 and the terminal lab (pt-BR, 1280x800)") {
  Player p(3, {Locale::PtBR, 1280, 800}, "builder");
  testing::Terminal t(p.app, *p.d);
  t.open(testing::mission_index("m13-call-spread"));
  t.skip_question();
  game::GameViewModel& vm = p.app.vm().game();
  for (std::size_t i = 0; i < game::kTemplates.size(); ++i) {
    const std::string code(game::template_code(game::kTemplates[i]));
    testing::pick_template(p.app, *p.d, i);
    Rwidget_scroll_into_view(p.d->widget(bn::kSubmit));
    p.run->shot(code);
    p.run->clipping(code);
    p.dump("template " + code, bn::kBody);
  }
  testing::pick_template(p.app, *p.d, 4);  // long straddle into the lab
  REQUIRE(p.d->click(bn::kOpenLab));
  REQUIRE(p.d->until([&] { return vm.lab.result().has_value(); }));
  p.dump("lab zero", "game-lab");
  slide(p, "game-lab-price", 0, 15, [&] { return vm.lab.shift.price == 15 && vm.lab.result().has_value(); });
  p.dump("lab price +15", "game-lab");
  p.run->shot("lab-price-15");
  p.run->clipping("lab");
  p.run->finish();
}

TEST_CASE("playtest strategies: the Academy lab from the menu, price, IV and time to expiry (en, 1024x768)") {
  Player p(4242, {Locale::En, 1024, 768}, "academy-lab");
  p.run->shot("menu");
  p.run->clipping("menu");  // the row of eight screen buttons at the narrowest window
  REQUIRE(p.d->click(bn::kMenuLab));
  game::LabViewModel& lab = p.app.vm().academy_lab();
  REQUIRE(p.d->until([&] { return lab.result().has_value(); }));
  p.dump("lab default", "lab-scroll");
  p.run->clipping("academy lab");
  slide(p, "lab-price", 0, 10, [&] { return lab.shift.price == 10 && lab.result().has_value(); });
  p.dump("lab price +10", "lab-scroll");
  slide(p, "lab-iv", 0, -30, [&] { return lab.shift.iv == -30 && lab.result().has_value(); });
  slide(p, "lab-time", 0, 30, [&] { return lab.shift.time == 30 && lab.result().has_value(); });
  p.dump("lab price +10, iv -30, time +30", "lab-scroll");
  p.run->shot("expiry");
  p.run->clipping("academy lab at expiry");
  p.run->finish();
}

namespace {

struct StrategyPlay {
  const char* mission;
  std::size_t tpl;
  Window window;
};

void play_strategy(const StrategyPlay& s) {
  Player p(3, s.window, s.mission);
  testing::Terminal t(p.app, *p.d);
  testing::play_strategy_mission(p.app, *p.d, t, s.mission, s.tpl, [&](const std::string& label) {
    p.run->note(label);
    p.run->shot(label);
    p.run->clipping(label);
  });
  p.run->finish();
}

}  // namespace

TEST_CASE("playtest strategies: m13–m17 each with a structure from the builder to the debrief") {
  const StrategyPlay plays[] = {
      {"m13-call-spread", 0, {Locale::PtBR, 1280, 800}},  {"m14-covered-call", 2, {Locale::En, 1024, 768}},
      {"m15-protective-put", 3, {Locale::PtBR, 1024, 768}}, {"m16-range-condor", 6, {Locale::En, 1280, 800}},
      {"m17-before-expiry", 1, {Locale::PtBR, 1280, 800}},
  };
  for (const auto& s : plays) {
    INFO(s.mission);
    play_strategy(s);
  }
}

namespace {

// A campaign leg from its briefing: the strategy missions trade a structure, the others only wait.
void campaign_leg(Player& p, testing::Terminal& t, int leg) {
  REQUIRE(p.d->to_briefing());
  const std::string mode = p.d->text(names::kBriefMode);
  CHECK(mode.find(std::to_string(leg) + " de 17") != std::string::npos);
  REQUIRE(p.d->click(names::kBriefGo));
  t.skip_question();
  const std::string id = t.g().mission;
  p.run->report() << "== leg " << leg << " " << id << "\n";
  const std::pair<const char*, std::size_t> structures[] = {{"m13", 0}, {"m14", 2}, {"m15", 3}, {"m16", 6}, {"m17", 1}};
  for (const auto& [prefix, index] : structures) {
    if (id.rfind(prefix, 0) != 0) continue;
    testing::pick_template(p.app, *p.d, index);
    testing::submit_combo(p.app, *p.d);
  }
  p.run->play_to_debrief();
  p.run->clipping("leg " + std::to_string(leg));
  p.run->report() << testing::dump_texts(p.d->widget(names::kDebriefCampaignPanel));
}

}  // namespace

TEST_CASE("playtest strategies: a campaign through the Strategies stage (pt-BR, 1280x800)") {
  Player p(31, {Locale::PtBR, 1280, 800}, "campaign-strategies");
  testing::Terminal t(p.app, *p.d);
  REQUIRE(p.d->click(names::kCampaignStart));
  campaign_leg(p, t, 1);
  for (int leg = 2; leg <= 14; ++leg) {
    REQUIRE(p.d->click(names::kDebriefCampaignNext));
    campaign_leg(p, t, leg);
  }
  p.run->shot("leg14-debrief");
  REQUIRE(p.d->click(names::kDebriefMenu));
  REQUIRE(p.d->click(names::kMenuCurriculum));
  p.run->shot("curriculum");
  p.run->clipping("curriculum");
  p.dump("curriculum", names::kCurriculumStages);
  p.run->finish();
}
