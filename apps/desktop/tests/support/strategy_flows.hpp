// Scripted flows of the strategy builder, the what-if lab and the strategy missions (m13–m17) driven by
// injected input, the desktop counterpart of the web's e2e/strategies.spec.ts and missions-batch-c.spec.ts.
// Shared by the headless tests (Raven null platform) and the windowed tests (Vulkan, with captures).
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_controls.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <algorithm>
#include <functional>
#include <string>

#include "model/engine_run.hpp"
#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/shell_flows.hpp"
#include "view/app.hpp"
#include "viewmodel/game/builder_widgets.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::testing {

namespace bn = names::builder;
using StrategyShot = std::function<void(const std::string&)>;

// The menu index of a built-in mission (the menu lists them in engine order).
inline int mission_index(std::string_view id) {
  const auto missions = engine_missions();
  for (std::size_t i = 0; i < missions.size(); ++i) {
    if (missions[i].id == id) return static_cast<int>(i);
  }
  FAIL("no mission " << id);
  return -1;
}

inline bool analysis_ready(game::GameViewModel& vm) {
  const auto& b = vm.builder;
  return !b.pending() && b.whatif().has_value() && b.preview().has_value();
}

// The builder tab, a template (index in game::kTemplates), and the engine's analysis on screen.
inline void pick_template(App& app, Driver& d, std::size_t index) {
  game::GameViewModel& vm = app.vm().game();
  if (vm.tab() != game::Tab::Builder) REQUIRE(d.click(bn::kTab));
  REQUIRE(vm.tab() == game::Tab::Builder);
  const std::string button = std::string(bn::kTemplatePrefix) + std::to_string(index);
  Rwidget_scroll_into_view(d.widget(button));
  d.settle(d.widget(button));  // the legs and the analysis of the last template moved the layout
  // Inside its panel: a template button wider than the 340 px ticket column was clicked on its
  // scroll bar (the templates then sat in a two-column grid).
  float x = 0, y = 0, w = 0, h = 0, px = 0, py = 0, pw = 0, ph = 0;
  Rwidget_get_rect(d.widget(button), &x, &y, &w, &h);
  Rwidget_get_rect(d.widget(game::kPanelTicket), &px, &py, &pw, &ph);
  CHECK(x + w <= px + pw);
  REQUIRE(d.click(button));
  INFO("picked " << (vm.builder.tmpl ? game::template_code(*vm.builder.tmpl) : "none"));
  REQUIRE(vm.builder.tmpl == game::kTemplates[index]);
  REQUIRE(d.until([&] { return analysis_ready(vm); }));
}

// The combo's plan skipped, its legs sent one by one.
inline void submit_combo(App& app, Driver& d) {
  game::GameViewModel& vm = app.vm().game();
  REQUIRE(d.click(bn::kSubmit));
  REQUIRE(vm.modal() == game::Modal::Plan);
  REQUIRE(d.click(names::game::kPlanSkip));
  REQUIRE(d.idle());
}

// Every template on m13's first decision: its legs from the chain, the dry run leg by leg and the
// what-if (breakevens, bounds, payoff) from the engine; then the iron condor sent and filled.
inline void flow_each_template(App& app, Driver& d, Terminal& t, const StrategyShot& shot = {}) {
  t.open(mission_index("m13-call-spread"));
  t.skip_question();
  game::GameViewModel& vm = app.vm().game();
  REQUIRE(t.g().options.enabled);
  for (std::size_t i = 0; i < game::kTemplates.size(); ++i) {
    INFO("template " << game::template_code(game::kTemplates[i]));
    pick_template(app, d, i);
    const game::BuilderView v = vm.builder_view();
    CHECK_FALSE(v.legs.empty());
    CHECK(v.problem.empty());
    CHECK(vm.builder.whatif()->ok());
    REQUIRE(vm.builder.preview()->ok());
    CHECK(vm.builder.preview()->value->legs.size() == vm.builder.orders(t.g())->size());
    CHECK(vm.builder.preview()->value->accepted);
    CHECK(v.has_whatif);
    CHECK(shown(d, bn::kPayoff));
    CHECK(v.can_submit);
    if (shot) shot("builder-" + std::string(game::template_code(game::kTemplates[i])));
  }
  // The condor (the last template) goes out as four tagged market orders, longs first.
  submit_combo(app, d);
  REQUIRE(vm.combos().size() == 1);
  CHECK(vm.combos()[0].strategy == "iron_condor");
  CHECK(vm.combos()[0].legs.size() == 4);
  CHECK(d.text(names::kGameNotice).find("Iron condor") != std::string::npos);
  t.advance();
  const auto st = vm.combo_statuses();
  REQUIRE(st.size() == 1);
  CHECK(st[0].state == game::ComboState::Filled);
  CHECK(t.g().options.positions.size() == 4);
  if (shot) shot("builder-condor-filled");
}

// The terminal lab over the builder's straddle: at zero shift each leg is worth its series' mid; the
// price, time and IV sliders change the engine's numbers; Esc closes it.
inline void flow_lab_sliders(App& app, Driver& d, Terminal& t, const StrategyShot& shot = {}) {
  t.open(mission_index("m16-range-condor"));
  t.skip_question();
  game::GameViewModel& vm = app.vm().game();
  pick_template(app, d, 4);  // long straddle
  REQUIRE(d.click(bn::kOpenLab));
  REQUIRE(vm.modal() == game::Modal::Lab);
  REQUIRE(d.until([&] { return vm.lab.result().has_value(); }));
  const game::WhatIfResult zero = *vm.lab.result();
  REQUIRE(zero.legs.size() == 2);
  for (const auto& l : zero.legs) {
    const auto q = std::find_if(t.g().options.chain.begin(), t.g().options.chain.end(), [&](const OptionQuote& x) { return x.id == l.series; });
    REQUIRE(q != t.g().options.chain.end());
    CHECK(std::nearbyint(fmt::num(l.value) / t.g().options.premium_tick) * t.g().options.premium_tick == doctest::Approx(q->mid).epsilon(1e-12));
  }
  CHECK(shown(d, std::string(bn::kLabPrefix) + "payoff"));
  if (shot) shot("lab-zero");
  const std::string pnl0 = zero.net.pnl;
  d.focus(std::string(bn::kLabPrefix) + "price");
  for (int i = 0; i < 5; ++i) d.key(R_KEY_RIGHT);
  REQUIRE(d.until([&] { return vm.lab.shift.price == 5 && vm.lab.result() && vm.lab.result()->net.pnl != pnl0; }));
  CHECK(d.text(std::string(bn::kLabPrefix) + "price-value").rfind("+5%", 0) == 0);
  d.focus(std::string(bn::kLabPrefix) + "time");
  for (int i = 0; i < 3; ++i) d.key(R_KEY_RIGHT);
  REQUIRE(d.until([&] { return vm.lab.shift.time == 3 && vm.lab.result().has_value(); }));
  d.focus(std::string(bn::kLabPrefix) + "iv");
  for (int i = 0; i < 4; ++i) d.key(R_KEY_LEFT);
  REQUIRE(d.until([&] { return vm.lab.shift.iv == -4 && vm.lab.result().has_value(); }));
  d.frames(3);
  if (shot) shot("lab-shifted");
  REQUIRE(d.click(std::string(bn::kLabPrefix) + "reset"));
  REQUIRE(d.until([&] { return vm.lab.shift == game::LabShift{} && vm.lab.result() && vm.lab.result()->net.pnl == pnl0; }));
  REQUIRE(d.click(bn::kLabClose));
  CHECK(vm.modal() == game::Modal::None);
}

// The Academy lab from the menu: the call priced at the model, a slider, back to the menu.
inline void flow_academy_lab(App& app, Driver& d, const StrategyShot& shot = {}) {
  REQUIRE(app.vm().screen() == Screen::Menu);
  REQUIRE(d.click(bn::kMenuLab));
  REQUIRE(app.vm().screen() == Screen::Lab);
  game::LabViewModel& lab = app.vm().academy_lab();
  REQUIRE(d.until([&] { return lab.result().has_value() && lab.academy_legs.front().entry > 0; }));
  CHECK(fmt::num(lab.result()->net.pnl) == doctest::Approx(0).epsilon(0.01));  // bought at the model value
  if (shot) shot("academy-lab");
  d.focus("lab-price");
  for (int i = 0; i < 10; ++i) d.key(R_KEY_RIGHT);
  REQUIRE(d.until([&] { return lab.shift.price == 10 && lab.result() && fmt::num(lab.result()->net.pnl) > 1; }));
  REQUIRE(d.click(bn::kScreenAddPerp));
  REQUIRE(d.until([&] { return lab.result() && lab.result()->legs.size() == 2; }));
  if (shot) shot("academy-lab-shifted");
  REQUIRE(d.click(bn::kScreenBack));
  CHECK(app.vm().screen() == Screen::Menu);
}

// One strategy mission (m13–m17) played to its debrief with a structure from the builder: the combo
// sent at the first decision, then Shift+Space to the end.
inline void play_strategy_mission(App& app, Driver& d, Terminal& t, std::string_view mission, std::size_t tpl,
                                  const StrategyShot& shot = {}) {
  t.open(mission_index(mission));
  t.skip_question();
  game::GameViewModel& vm = app.vm().game();
  pick_template(app, d, tpl);
  CHECK(vm.builder.preview()->value->accepted);
  submit_combo(app, d);
  REQUIRE(vm.combos().size() == 1);
  t.advance();
  CHECK(vm.combo_statuses().front().state != game::ComboState::Working);
  if (shot) shot(std::string(mission) + "-combo");
  const bool filled = vm.combo_statuses().front().state == game::ComboState::Filled;
  play_to_debrief(app, d, t);
  CHECK(d.text(names::kDebriefScore).find("/100") != std::string::npos);
  // The combo's legs grouped under one row: its name, state and net premium.
  const ScreenModel model = app.vm().screen_model();
  const WidgetTable* trades = model.table_of(names::kDebriefTrades);
  REQUIRE(trades != nullptr);
  const bool grouped = std::any_of(trades->rows.begin(), trades->rows.end(),
                                   [](const auto& r) { return r.size() > 1 && r[1].rfind("\xE2\x97\x86 ", 0) == 0; });
  CHECK(grouped == filled);
  if (shot) shot(std::string(mission) + "-debrief");
}

}  // namespace tradelab::desktop::testing
