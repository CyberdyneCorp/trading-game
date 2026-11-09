// Playtest of wave 2 of add-practice-and-advanced-strategies (W2-D3) on the Vulkan renderer: m18-m25,
// each played to the debrief with a sensible and a reckless approach, one in PT-BR at 1280x800 and the
// other in en at 1024x768. Each run captures every tutorial card (the new drawings), the briefing (risk
// limits, calendar margin), the terminal and the debrief (bias findings with evidence, the objectives
// read from the facts, a moment marked on the chart, the episodes and the strategy comparison), and
// writes a text report in $TRADELAB_CAPTURE_DIR/playtest; no text may be cut by its panel.
#include <doctest/doctest.h>

#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include <functional>
#include <string>

#include "support/playtest_run.hpp"
#include "support/strategy_flows.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/game/strategies.hpp"
#include "viewmodel/game/wave1_widgets.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace gn = tradelab::desktop::names::game;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::RavenSession;
using tradelab::desktop::testing::Run;

namespace {

struct Window {
  Locale locale;
  int width;
  int height;
};

constexpr Window kPt{Locale::PtBR, 1280, 800};
constexpr Window kEn{Locale::En, 1024, 768};

struct Player {
  RavenSession raven;
  App app;
  std::unique_ptr<Driver> d;
  std::unique_ptr<Run> run;
  std::unique_ptr<testing::Terminal> t;
  Player(Window w, const std::string& name) : app(builtin_catalog(), [] { return 3u; }) {
    REQUIRE(app.build(w.width, w.height));
    app.vm().set_crt(CrtSettings{false, false, false, false});
    d = std::make_unique<Driver>(app);
    d->frames(4);
    app.vm().set_locale(w.locale);
    d->frames(2);
    run = std::make_unique<Run>(app, *d, name, w.width, w.height);
    t = std::make_unique<testing::Terminal>(app, *d);
    run->report() << name << " seed 3 " << w.width << "x" << w.height << "\n";
  }
  game::GameViewModel& vm() { return app.vm().game(); }
  void dump(const std::string& title, std::string_view widget) {
    d->frames(3);
    run->report() << "---- " << title << "\n" << testing::dump_texts(d->widget(widget));
  }
  void view(std::string_view widget, const std::string& label) {
    Rwidget_scroll_into_view(d->widget(widget));
    run->shot(label);
    run->clipping(label);
  }
};

// A perpetual order by quantity from the ticket; an entry's plan is saved with `thesis` (skipped when
// empty), a reducing order has none.
void trade(Player& p, bool buy, std::string_view qty, int stop_chip = -1, std::string_view thesis = "") {
  p.t->skip_question();
  if (p.vm().tab() != game::Tab::Perp) REQUIRE(p.d->click(gn::kTabPerp));
  REQUIRE(p.d->click(buy ? names::kGameBuy : names::kGameSell));
  p.d->choose(gn::kSizeBy, 1);
  p.t->type(names::kGameQty, qty);
  if (stop_chip >= 0) REQUIRE(p.d->click("game-stop-chip-" + std::to_string(stop_chip)));
  REQUIRE(p.d->click(gn::kSubmit));
  if (p.vm().modal() == game::Modal::Plan) {
    if (thesis.empty()) {
      REQUIRE(p.d->click(gn::kPlanSkip));
    } else {
      p.t->type(gn::kPlanThesis, thesis);
      REQUIRE(p.d->click(gn::kPlanSave));
    }
  }
  REQUIRE(p.d->idle());
  p.run->report() << "  " << (buy ? "buy " : "sell ") << qty << ": " << p.d->text(names::kGameNotice) << "\n";
  p.run->note("order");
}

void structure(Player& p, game::Template tpl) {
  p.t->skip_question();
  const auto it = std::find(game::kTemplates.begin(), game::kTemplates.end(), tpl);
  testing::pick_template(p.app, *p.d, static_cast<std::size_t>(it - game::kTemplates.begin()));
  p.dump("builder " + std::string(game::template_code(tpl)), names::builder::kBody);
  testing::submit_combo(p.app, *p.d);
  p.run->report() << "  combo: " << p.d->text(names::kGameNotice) << "\n";
}

void advance_until(Player& p, const std::function<bool()>& done, int max_bars) {
  for (int i = 0; i < max_bars && p.app.vm().screen() == Screen::Game && !done(); ++i) p.run->advance(1);
}

bool flat(Player& p) { return p.vm().state()->position.qty == 0; }

// The tutorial cards one by one, then the briefing.
void tutorial_and_briefing(Player& p, const std::string& mission) {
  p.app.vm().select(testing::mission_index(mission));
  p.d->frames(2);
  REQUIRE(p.d->click(names::kMenuStart));
  REQUIRE(p.d->until([&] { return p.app.vm().screen() == Screen::Tutorial; }));
  for (int card = 1; p.app.vm().screen() == Screen::Tutorial && card <= 8; ++card) {
    p.run->shot("tut-" + std::to_string(card));
    p.run->clipping("tutorial card " + std::to_string(card));
    p.dump("tutorial card " + std::to_string(card), "tutorial-scroll");
    REQUIRE(p.d->click(names::kTutNext));
  }
  REQUIRE(p.d->until([&] { return p.app.vm().screen() == Screen::Briefing; }));
  p.run->shot("briefing");
  p.run->clipping("briefing");
  p.run->report() << "---- briefing\n" << testing::dump_texts(p.app.view().screen_root(Screen::Briefing));
  REQUIRE(p.d->click(names::kBriefGo));
  REQUIRE(p.app.vm().screen() == Screen::Game);
  p.d->frames(3);
}

// The debrief's v14 views: findings and objectives, the first moment marked on the chart, the episodes
// and the comparison.
void debrief_views(Player& p) {
  p.run->shot("debrief");
  p.run->clipping("debrief");
  p.view(names::kDebriefFindings, "findings");
  if (Rwidget_get_visibility(p.d->widget(names::kDebriefMomentsBox)) == R_VISIBLE) {
    p.app.vm().debrief_show_moment(0);
    p.d->frames(3);
    CHECK(p.app.vm().debrief_moment().has_value());
    p.view(names::kDebriefChart, "moment");
  }
  if (Rwidget_get_visibility(p.d->widget(names::kDebriefEpisodesPanel)) == R_VISIBLE) p.view(names::kDebriefEpisodesPanel, "episodes");
  if (Rwidget_get_visibility(p.d->widget(names::kDebriefComparisonPanel)) == R_VISIBLE) p.view(names::kDebriefComparisonPanel, "comparison");
}

struct Play {
  const char* mission;
  const char* approach;  // sensible | reckless
  Window window;
  std::function<void(Player&)> script;
};

void play(const Play& s) {
  const std::string name = std::string("w2-") + std::string(s.mission).substr(0, 3) + "-" + s.approach + "-" + std::to_string(s.window.width);
  INFO(name);
  Player p(s.window, name);
  tutorial_and_briefing(p, s.mission);
  p.run->shot("terminal");
  s.script(p);
  p.run->shot("before-end");
  p.run->clipping("terminal");
  p.run->play_to_debrief();
  debrief_views(p);
  p.run->finish();
}

// The portfolio's delta hedge (rounded to the lot toward zero), confirmed and sent with a skipped plan;
// nothing when the delta is already within a lot.
void rehedge(Player& p) {
  namespace w1 = names::wave1;
  p.t->skip_question();
  REQUIRE(p.d->until([&] { return p.vm().portfolio().has_value() && p.vm().portfolio()->risk.has_value(); }));
  REQUIRE(p.d->click(w1::kRailPortfolioOpen));
  REQUIRE(p.vm().modal() == game::Modal::Portfolio);
  p.dump("portfolio", names::game::kModal);
  if (!p.vm().portfolio_view().can_hedge) {
    REQUIRE(p.d->click(w1::kPortfolioClose));
    return;
  }
  REQUIRE(p.d->click(w1::kPortfolioHedge));
  if (p.vm().modal() == game::Modal::Confirm) REQUIRE(p.d->click(w1::kConfirmYes));
  if (p.vm().modal() == game::Modal::Plan) REQUIRE(p.d->click(gn::kPlanSkip));
  REQUIRE(p.d->idle());
  p.run->report() << "  hedge: " << p.d->text(names::kGameNotice) << "\n";
}

// m18: a straddle of 20 contracts (the first expiry's at-the-money row) rehedged from the portfolio
// every 8 bars, against the same straddle loaded with more delta from the perpetual.
void m18_hedged(Player& p) {
  p.run->option(4, 1, "20");
  p.run->option(4, 4, "20");
  for (int k = 0; k < 6 && p.app.vm().screen() == Screen::Game; ++k) {
    p.run->advance(8);
    if (p.app.vm().screen() == Screen::Game) rehedge(p);
  }
}
void m18_loaded(Player& p) {
  p.run->option(4, 1, "20");
  p.run->option(4, 4, "20");
  p.run->advance(4);
  trade(p, true, "40");
}

// m19: a hedged carry (a call bought and a put written on the last expiry, the perpetual shorted) against
// an unhedged short.
void m19_carry(Player& p) {
  p.run->option(13, 1, "30");
  p.run->option(13, 3, "30");
  trade(p, false, "30", -1, "carrego: funding paga os custos");
}
void m19_short(Player& p) { trade(p, false, "80"); }

// m20: a straddle bought early and held, against a straddle written through the release.
void m20_early(Player& p) { structure(p, game::Template::Straddle); }
void m20_written(Player& p) {
  p.run->option(5, 0, "20");
  p.run->option(5, 3, "20");
}

// m21: a call calendar from the builder, against a short straddle on the near expiry.
void m21_calendar(Player& p) { structure(p, game::Template::CalendarCall); }
void m21_short(Player& p) {
  p.run->option(5, 0, "20");
  p.run->option(5, 3, "20");
}

// m22: scale in with a stop, take part off higher, against one big entry without a stop.
void m22_managed(Player& p) {
  trade(p, true, "20", 2, "tendência: entro em duas partes");
  p.run->advance(5);
  trade(p, true, "10", 2, "segunda parte, mesmo stop");
  p.run->advance(10);
  if (!flat(p)) trade(p, false, "15");
}
void m22_all_in(Player& p) { trade(p, true, "120"); }

// m23: a stopped trade, a pause and a planned re-entry, against an immediate re-entry at twice the size.
void m23_cooled(Player& p) {
  trade(p, true, "20", 0, "rompimento da máxima de 20 barras");
  p.run->advance(1);
  advance_until(p, [&] { return flat(p); }, 60);
  p.run->advance(4);
  if (p.app.vm().screen() == Screen::Game) trade(p, true, "20", 1, "setup formado de novo");
}
void m23_revenge(Player& p) {
  trade(p, true, "20", 0);
  p.run->advance(1);
  advance_until(p, [&] { return flat(p); }, 60);
  if (p.app.vm().screen() == Screen::Game) trade(p, true, "40");
}

// m24: a planned entry with a stop, against a chase with no stop after a run of rising closes.
void m24_planned(Player& p) {
  p.run->advance(20);
  trade(p, true, "15", 2, "rompimento com stop a 2 ATR");
}
void m24_chase(Player& p) {
  p.run->advance(38);
  trade(p, true, "40");
}

// m25: one entry with its stop, against two adds to a loser with no stop.
void m25_single(Player& p) { trade(p, true, "30", 2, "entrada única, stop abaixo da mínima"); }
void m25_average(Player& p) {
  trade(p, true, "30");
  for (int k = 0; k < 2; ++k) {
    p.run->advance(12);
    if (p.app.vm().screen() == Screen::Game) trade(p, true, "60");
  }
}

}  // namespace

TEST_CASE("playtest wave 2: m18-m21, a sensible and a reckless approach each (pt-BR 1280x800, en 1024x768)") {
  const Play plays[] = {
      {"m18-gamma-scalp", "sensible", kPt, m18_hedged},   {"m18-gamma-scalp", "reckless", kEn, m18_loaded},
      {"m19-funding-carry", "sensible", kEn, m19_carry},  {"m19-funding-carry", "reckless", kPt, m19_short},
      {"m20-event-vol", "sensible", kPt, m20_early},      {"m20-event-vol", "reckless", kEn, m20_written},
      {"m21-calendar", "sensible", kEn, m21_calendar},    {"m21-calendar", "reckless", kPt, m21_short},
  };
  for (const auto& s : plays) play(s);
}

TEST_CASE("playtest wave 2: m22-m25, a sensible and a reckless approach each (pt-BR 1280x800, en 1024x768)") {
  const Play plays[] = {
      {"m22-scale-manage", "sensible", kPt, m22_managed},  {"m22-scale-manage", "reckless", kEn, m22_all_in},
      {"m23-second-chance", "sensible", kEn, m23_cooled},  {"m23-second-chance", "reckless", kPt, m23_revenge},
      {"m24-fast-market", "sensible", kPt, m24_planned},   {"m24-fast-market", "reckless", kEn, m24_chase},
      {"m25-the-dip", "sensible", kEn, m25_single},        {"m25-the-dip", "reckless", kPt, m25_average},
  };
  for (const auto& s : plays) play(s);
}

// The curriculum map with its branches beside the path, in both languages and sizes.
TEST_CASE("playtest wave 2: the curriculum map's branches and the elective stage (pt-BR 1280x800, en 1024x768)") {
  for (const Window w : {kPt, kEn}) {
    Player p(w, "w2-curriculum-" + std::to_string(w.width));
    REQUIRE(p.d->click(names::kMenuCurriculum));
    p.run->shot("curriculum");
    p.run->clipping("curriculum");
    p.dump("curriculum", names::kCurriculumStages);
    p.run->finish();
  }
}
