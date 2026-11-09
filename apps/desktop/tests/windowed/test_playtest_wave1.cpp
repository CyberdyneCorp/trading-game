// Playtest of wave 1 of add-practice-and-advanced-strategies on the Vulkan renderer: time in force and
// OCO links, the planned trailing stop, and a calendar on m17 with the heatmap, an adjustment, a roll
// and the portfolio, in PT-BR at 1280x800 and in en at 1024x768. Each run leaves captures and a text
// report in $TRADELAB_CAPTURE_DIR/playtest (the ticket's, the orders panel's, the builder's and the
// portfolio's texts at each step); no text may be cut by its panel.
#include <doctest/doctest.h>

#include <raven/raven_widget.h>

#include <cmath>
#include <string>

#include "support/playtest_run.hpp"
#include "support/wave1_flows.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/builder_widgets.hpp"
#include "viewmodel/game/strategies.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::RavenSession;
using tradelab::desktop::testing::Run;

namespace {

struct Window {
  Locale locale;
  int width;
  int height;
};

constexpr Window kWindows[] = {{Locale::PtBR, 1280, 800}, {Locale::En, 1024, 768}};

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
    run = std::make_unique<Run>(app, *d, name + "-" + std::to_string(w.width), w.width, w.height);
    t = std::make_unique<testing::Terminal>(app, *d);
  }
  testing::Wave1Shot shot() {
    return [this](const std::string& label) {
      run->shot(label);
      run->report() << "---- " << label << "\n" << testing::dump_texts(d->widget(names::game::kModal)) << testing::dump_texts(d->widget("game-content"));
    };
  }
};

}  // namespace

TEST_CASE("playtest wave 1: time in force and an OCO link (pt-BR 1280x800, en 1024x768)") {
  for (const Window& w : kWindows) {
    Player p(w, "w1-tif-oco");
    testing::flow_tif_oco(p.app, *p.d, *p.t, p.shot());
  }
}

TEST_CASE("playtest wave 1: the planned trailing stop (pt-BR 1280x800, en 1024x768)") {
  for (const Window& w : kWindows) {
    Player p(w, "w1-trail");
    testing::flow_planned_trail(p.app, *p.d, *p.t, p.shot());
  }
}

TEST_CASE("playtest wave 1: a calendar with the heatmap, adjusted, rolled, and the portfolio (pt-BR 1280x800, en 1024x768)") {
  for (const Window& w : kWindows) {
    Player p(w, "w1-calendar");
    testing::flow_calendar(p.app, *p.d, *p.t, p.shot());
  }
}

// W1-X1: one m16 and one m17 seed played with the same inputs as the web playtest (seed 3, calibration
// questions and plans skipped). The report lists what the web shows for the same steps: the builder's
// dry run, totals and analysis, the fills, the heatmap cells 0, 10 and 50 of the 21-column grid, the
// roll and the hedge, the portfolio totals and the action log (the web run file's actions).
namespace {

namespace bn = names::builder;
namespace w1 = names::wave1;

void report_live(Player& p, const char* label) {
  const auto v = p.app.vm().game().live_view();
  auto& r = p.run->report();
  r << "---- " << label << "\n";
  for (const auto& o : v.orders) r << "  " << o.text << "\n";
  for (const auto& [k, val] : v.totals) r << "  " << k << ": " << val << "\n";
  if (!v.hedge_none.empty()) r << "  " << v.hedge_none << "\n";
}

void report_portfolio(Player& p) {
  game::GameViewModel& vm = p.app.vm().game();
  REQUIRE(p.d->until([&] { return vm.portfolio().has_value() && vm.portfolio()->risk.has_value(); }));
  const auto v = vm.portfolio_view();
  auto& r = p.run->report();
  r << "---- portfolio\n";
  for (const auto& row : v.rows) {
    r << " ";
    for (const auto& c : row) r << " " << c;
    r << "\n";
  }
  for (const auto& [k, val] : v.totals) r << "  " << k << ": " << val << "\n";
  r << "  " << v.hedge_note << "\n";
}

void report_log(Player& p) {
  auto& r = p.run->report();
  r << "---- actions\n";
  if (const auto* log = p.app.vm().game().action_log()) {
    for (const auto& a : *log) r << "  " << a << "\n";
  }
}

void skip_plan(Player& p) {
  REQUIRE(p.app.vm().game().modal() == game::Modal::Plan);
  REQUIRE(p.d->click(names::game::kPlanSkip));
  REQUIRE(p.d->idle());
}

void stop_order(Player& p, bool buy, double price) {
  p.t->skip_question();
  REQUIRE(p.d->click(buy ? names::kGameBuy : names::kGameSell));
  p.d->choose(names::game::kType, 2);  // stop market
  p.d->choose(names::game::kSizeBy, 1);
  p.t->type(names::kGameQty, "10");
  p.t->type(names::game::kPrice, fmt::price_input_text(std::round(price * 100) / 100, 0.01, Locale::PtBR));
  REQUIRE(p.d->click(names::game::kSubmit));
  if (p.app.vm().game().modal() == game::Modal::Plan) skip_plan(p);
  REQUIRE(p.d->idle());
}

}  // namespace

TEST_CASE("playtest wave 1 X1: m16 seed 3, an IOC limit condor and an OCO pair, same inputs as the web") {
  Player p({Locale::PtBR, 1280, 800}, "w1-x1-m16");
  game::GameViewModel& vm = p.app.vm().game();
  p.t->open(testing::mission_index("m16-range-condor"));
  p.t->skip_question();
  if (vm.tab() != game::Tab::Builder) REQUIRE(p.d->click(bn::kTab));
  p.t->type(bn::kQty, "10");
  testing::pick_template(p.app, *p.d, 6);  // iron condor
  for (std::size_t i = 0; i < vm.builder.legs.size(); ++i) vm.builder.set_leg_limit(p.t->g(), i, true);
  vm.builder_changed();
  REQUIRE(p.d->until([&] { return testing::analysis_ready(vm); }));
  p.run->report() << "---- condor, limit legs at the default (IOC)\n" << testing::dump_texts(p.d->widget(bn::kBody));
  testing::submit_combo(p.app, *p.d);
  p.run->report() << "  notice: " << p.d->text(names::kGameNotice) << "\n";
  p.t->advance();
  p.run->report() << "---- after the open\n" << testing::dump_texts(p.d->widget(names::game::kFeed));
  REQUIRE(p.d->click(names::game::kTabPerp));
  const double close = p.t->g().last_price();
  p.run->report() << "  close " << close << "\n";
  stop_order(p, true, close * 1.005);
  stop_order(p, false, close * 0.995);
  REQUIRE(p.t->g().orders.size() == 2);
  const int a = p.t->g().orders[0].id;
  const int b = p.t->g().orders[1].id;
  p.d->frames(2);
  REQUIRE(p.d->click(std::string(w1::kOrderPickPrefix) + std::to_string(a)));
  REQUIRE(p.d->click(std::string(w1::kOrderPickPrefix) + std::to_string(b)));
  REQUIRE(p.d->click(w1::kLink));
  REQUIRE(p.d->click(w1::kConfirmYes));
  REQUIRE(p.d->idle());
  for (int i = 0; i < 10 && p.t->g().orders.size() == 2; ++i) p.t->advance();
  p.run->report() << "---- OCO at bar " << p.t->g().step << "\n" << testing::dump_texts(p.d->widget(names::game::kFeed));
  report_portfolio(p);
  p.run->play_to_debrief();
  report_log(p);
  p.run->finish();
}

TEST_CASE("playtest wave 1 X1: m17 seed 3, a calendar with its heatmap, rolled and hedged, same inputs as the web") {
  Player p({Locale::PtBR, 1280, 800}, "w1-x1-m17");
  game::GameViewModel& vm = p.app.vm().game();
  p.t->open(testing::mission_index("m17-before-expiry"));
  p.t->skip_question();
  if (vm.tab() != game::Tab::Builder) REQUIRE(p.d->click(bn::kTab));
  p.t->type(bn::kQty, "5");
  p.d->choose(bn::kFar, static_cast<int>(vm.builder.later(p.t->g()).size()) - 1);
  testing::pick_template(p.app, *p.d, 7);  // calendar with calls
  REQUIRE(p.d->until([&] { return testing::analysis_ready(vm); }));
  p.run->report() << "---- calendar\n" << testing::dump_texts(p.d->widget(bn::kBody));
  REQUIRE(p.d->click(w1::kBuilderHeatmapToggle));
  REQUIRE(p.d->until([&] { return vm.builder_heatmap().has_value(); }));
  const auto& m = *vm.builder_heatmap();
  for (const std::size_t i : {std::size_t{0}, std::size_t{10}, std::size_t{50}}) {
    const std::size_t t = i / m.prices.size();
    const std::size_t c = i % m.prices.size();
    p.run->report() << "  cell " << i << ": price " << m.prices[c] << " after " << m.times[t] << " bars: " << m.pnl[t][c] << "\n";
  }
  testing::submit_combo(p.app, *p.d);
  p.t->advance();
  p.run->report() << "---- fills\n" << testing::dump_texts(p.d->widget(names::game::kFeed));
  p.d->frames(2);
  REQUIRE(p.d->click("game-live-roll-c1"));
  REQUIRE(p.d->until([&] { return vm.live_view().can_send; }));
  report_live(p, "roll");
  REQUIRE(p.d->click(w1::kLiveSend));
  if (vm.modal() == game::Modal::Confirm) REQUIRE(p.d->click(w1::kConfirmYes));
  skip_plan(p);
  p.t->advance();
  p.d->frames(2);
  REQUIRE(p.d->click("game-live-adjust-c1"));
  REQUIRE(p.d->click(w1::kLiveHedge));
  REQUIRE(p.d->until([&] { return vm.live_view().can_send || !vm.live_view().hedge_none.empty(); }));
  report_live(p, "hedge");
  // The hedge is the portfolio's own proposal for these holdings (regression: it was sized a step old).
  REQUIRE(vm.portfolio().has_value());
  REQUIRE(vm.portfolio()->risk.has_value());
  const auto expected = game::hedge_order(p.t->g(), fmt::num(vm.portfolio()->risk->delta_units));
  CHECK(vm.live_view().can_send == expected.has_value());
  if (vm.live_view().can_send) {
    REQUIRE(p.d->click(w1::kLiveSend));
    skip_plan(p);
  }
  p.t->advance();
  report_portfolio(p);
  p.run->play_to_debrief();
  report_log(p);
  p.run->finish();
}
