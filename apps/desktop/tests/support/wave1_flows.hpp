// Scripted flows of wave 1 of add-practice-and-advanced-strategies (time in force, OCO links, the planned
// trailing stop, a calendar built, rolled and adjusted, the heatmap and the portfolio), driven by
// injected input on the real engine. Shared by the headless tests and the windowed captures.
#pragma once

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <string>

#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/strategy_flows.hpp"
#include "viewmodel/game/wave1_widgets.hpp"

namespace tradelab::desktop::testing {

namespace w1 = names::wave1;
using Wave1Shot = std::function<void(const std::string&)>;

inline const EngineEvent* last_event(const GameState& g, int order_id, std::string_view status) {
  const EngineEvent* out = nullptr;
  for (const auto& e : g.events) {
    if (e.type == "order" && e.order_id == order_id && e.status == status) out = &e;
  }
  return out;
}

inline const Order* order_by_id(const GameState& g, int id) {
  const auto it = std::find_if(g.orders.begin(), g.orders.end(), [id](const Order& o) { return o.id == id; });
  return it == g.orders.end() ? nullptr : &*it;
}

// A limit order with time in force IOC far from the market expires at the next open unfilled; two
// working orders are ticked in the orders panel and linked OCO after the confirmation.
inline void flow_tif_oco(App& app, Driver& d, Terminal& t, const Wave1Shot& shot = {}) {
  t.open(0);
  t.skip_question();
  game::GameViewModel& vm = app.vm().game();
  d.choose(names::game::kSizeBy, 1);  // quantity
  d.choose(names::game::kType, 1);    // limit
  t.type(names::game::kPrice, fmt::price_input_text(t.g().last_price() * 0.8, t.g().contract.tick, Locale::PtBR));
  t.type(names::kGameQty, "1");
  d.choose(w1::kTif, 3);  // IOC
  REQUIRE(vm.ticket.tif_choice == game::TifChoice::Ioc);
  CHECK(d.text(w1::kTifHint).find(std::to_string(t.g().step + 1)) != std::string::npos);
  if (shot) shot("w1-ticket-tif");
  t.submit_with_plan();
  const Order* ioc = find_order(t.g(), "limit");
  REQUIRE(ioc != nullptr);
  CHECK(ioc->tif == "ioc");
  CHECK(ioc->expire_step == t.g().step + 1);
  const int ioc_id = ioc->id;
  t.advance();
  CHECK(order_by_id(t.g(), ioc_id) == nullptr);
  const EngineEvent* gone = last_event(t.g(), ioc_id, "cancelled");
  REQUIRE(gone != nullptr);
  CHECK(gone->reason == "time_in_force");

  // Two GTC orders: a buy limit below and a buy stop above, ticked and linked.
  d.choose(w1::kTif, 0);
  t.type(names::game::kPrice, fmt::price_input_text(t.g().last_price() * 0.9, t.g().contract.tick, Locale::PtBR));
  t.submit_with_plan();
  d.choose(names::game::kType, 2);  // stop market
  t.type(names::game::kPrice, fmt::price_input_text(t.g().last_price() * 1.1, t.g().contract.tick, Locale::PtBR));
  t.submit_with_plan();
  REQUIRE(t.g().orders.size() == 2);
  const int a = t.g().orders[0].id;
  const int b = t.g().orders[1].id;
  d.frames(2);
  REQUIRE(d.click(std::string(w1::kOrderPickPrefix) + std::to_string(a)));
  REQUIRE(d.click(std::string(w1::kOrderPickPrefix) + std::to_string(b)));
  CHECK(vm.link_view().can_link);
  if (shot) shot("w1-orders-link");
  REQUIRE(d.click(w1::kLink));
  REQUIRE(vm.modal() == game::Modal::Confirm);
  CHECK(d.text(w1::kConfirmTitle).find(std::to_string(a)) != std::string::npos);
  if (shot) shot("w1-link-confirm");
  REQUIRE(d.click(w1::kConfirmYes));
  REQUIRE(d.idle());
  REQUIRE(order_by_id(t.g(), a) != nullptr);
  CHECK(order_by_id(t.g(), a)->link_group == std::min(a, b));
  CHECK(order_by_id(t.g(), b)->link_group == std::min(a, b));
  CHECK(d.text(names::kGameNotice).find(std::to_string(a)) != std::string::npos);
  // A third pick of a linked order names why it cannot be linked again.
  REQUIRE(d.click(std::string(w1::kOrderPickPrefix) + std::to_string(a)));
  CHECK_FALSE(vm.link_view().can_link);
  if (shot) shot("w1-orders-linked");
}

// "Protect with: trailing stop": the rule rides with the plan; after the fill the notice offers it
// with the engine's first trigger, fills the trailing tab, and Protect sends it.
inline void flow_planned_trail(App& app, Driver& d, Terminal& t, const Wave1Shot& shot = {}) {
  t.open(0);
  t.skip_question();
  game::GameViewModel& vm = app.vm().game();
  d.choose(names::game::kSizeBy, 1);
  t.type(names::kGameQty, "1");
  REQUIRE(d.click(w1::kProtectTrail));
  REQUIRE(vm.ticket.protect_with_trail);
  t.type(w1::kPlanTrail, "1,5");
  CHECK_FALSE(d.text(w1::kPlanTrailNote).empty());
  if (shot) shot("w1-protect-with-trail");
  t.submit_with_plan();
  REQUIRE(vm.journal().back().trail.has_value());
  CHECK(find_order(t.g(), "trailing_stop") == nullptr);  // never sent without the click
  t.advance();
  REQUIRE(t.g().position.qty > 0);
  REQUIRE(vm.trail_notice().has_value());
  CHECK(vm.trail_notice()->text.find("1,5") != std::string::npos);
  if (shot) shot("w1-trail-notice");
  REQUIRE(d.click(w1::kTrailNoticePlace));
  CHECK(vm.ticket.protect_tab == game::ProtectTab::Trail);
  CHECK(vm.ticket.prot_trail == "1,5");
  CHECK(d.text(w1::kTrailRule).find(",") != std::string::npos);
  REQUIRE(d.click(names::game::kProtect));
  REQUIRE(d.idle());
  const Order* trail = find_order(t.g(), "trailing_stop");
  REQUIRE(trail != nullptr);
  CHECK(trail->trail_distance_text.rfind("1.5", 0) == 0);
  CHECK_FALSE(vm.trail_notice().has_value());
  if (shot) shot("w1-trailing-placed");
}

// m17: a calendar built on two expiries (the horizon analysis), sent and filled, the heatmap opened,
// then rolled to a later expiry and adjusted with a perpetual hedge, the portfolio dialog over it.
inline void flow_calendar(App& app, Driver& d, Terminal& t, const Wave1Shot& shot = {}) {
  t.open(mission_index("m17-before-expiry"));
  t.skip_question();
  game::GameViewModel& vm = app.vm().game();
  if (vm.tab() != game::Tab::Builder) REQUIRE(d.click(names::builder::kTab));
  REQUIRE_FALSE(vm.builder.later(t.g()).empty());
  pick_template(app, d, 7);  // calendar call
  REQUIRE(vm.builder.legs.size() == 2);
  CHECK(vm.builder.legs[0].expiry != vm.builder.legs[1].expiry);
  CHECK_FALSE(d.text(names::builder::kHorizon).empty());
  // A limit leg: the dry run takes it with the combo's time in force (IOC by default).
  vm.builder.set_leg_limit(t.g(), 1, true);
  vm.builder_changed();
  REQUIRE(d.until([&] { return analysis_ready(vm); }));
  CHECK(vm.builder.has_limit());
  CHECK(shown(d, names::builder::kLimitTif));
  if (shot) shot("w1-builder-calendar-limit");
  vm.builder.set_leg_limit(t.g(), 1, false);
  vm.builder_changed();
  REQUIRE(d.until([&] { return analysis_ready(vm); }));
  REQUIRE(d.click(w1::kBuilderHeatmapToggle));
  REQUIRE(d.until([&] { return vm.builder_heatmap().has_value(); }));
  CHECK(vm.builder_heatmap()->prices.size() == 21);
  d.frames(2);
  if (shot) shot("w1-builder-heatmap");
  submit_combo(app, d);
  t.advance();
  REQUIRE(vm.combo_statuses().front().state == game::ComboState::Filled);
  REQUIRE(vm.live_combos().size() == 1);
  d.frames(2);
  REQUIRE(d.click("game-live-adjust-c1"));
  REQUIRE(d.until([&] { return vm.live_view().shown && !vm.live_view().held.empty(); }));
  REQUIRE(d.click(w1::kLiveHedge));
  d.until([&] { return vm.live_view().can_send || !vm.live_view().hedge_none.empty(); });
  if (shot) shot("w1-adjust");
  if (vm.live_view().can_send) {
    REQUIRE(d.click(w1::kLiveSend));
    REQUIRE(vm.modal() == game::Modal::Plan);
    REQUIRE(d.click(names::game::kPlanSkip));
    REQUIRE(d.idle());
    CHECK(vm.combos().front().submissions.back().kind == game::SubmissionKind::Adjust);
  } else {
    REQUIRE(d.click(w1::kLiveCancel));
  }
  t.advance();
  // The portfolio from the rail: every held position, the engine's totals and the hedge note.
  REQUIRE(d.until([&] { return vm.portfolio().has_value() && vm.portfolio()->risk.has_value(); }));
  REQUIRE(d.click(w1::kRailPortfolioOpen));
  REQUIRE(vm.modal() == game::Modal::Portfolio);
  CHECK_FALSE(vm.portfolio_view().rows.empty());
  CHECK(vm.portfolio_view().totals.size() == 6);
  CHECK_FALSE(vm.portfolio_view().no_limits.empty());  // no built-in mission declares limits before m18
  d.frames(2);
  if (shot) shot("w1-portfolio");
  REQUIRE(d.click(w1::kPortfolioClose));
  CHECK(vm.modal() == game::Modal::None);
  // The roll: the near short bought back and sold again on a later expiry, one tagged submission.
  const std::size_t before = vm.combos().front().submissions.size();
  d.frames(2);
  REQUIRE(d.click("game-live-roll-c1"));
  REQUIRE(d.until([&] { return vm.live_view().can_send; }));
  CHECK_FALSE(vm.live_view().totals.empty());
  if (shot) shot("w1-roll");
  REQUIRE(d.click(w1::kLiveSend));
  if (vm.modal() == game::Modal::Confirm) REQUIRE(d.click(w1::kConfirmYes));
  REQUIRE(vm.modal() == game::Modal::Plan);
  REQUIRE(d.click(names::game::kPlanSkip));
  REQUIRE(d.idle());
  REQUIRE(vm.combos().front().submissions.size() == before + 1);
  CHECK(vm.combos().front().submissions.back().kind == game::SubmissionKind::Roll);
  t.advance();
}

}  // namespace tradelab::desktop::testing
