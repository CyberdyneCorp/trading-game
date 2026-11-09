// Wave 1 of add-practice-and-advanced-strategies in the terminal's viewmodels, with a fake engine: the
// ticket's time in force and "Protect with: trailing stop", the planned-trailing notice, OCO linking
// with its confirmation, the portfolio and its risk-limit warnings (a declared limit and an engine risk
// block given by the test: no built-in mission declares one before m18), "Hedge delta", the heatmap's
// cells and readout, the builder's limit legs and two expiries, and the briefing's risk limits.
#include <doctest/doctest.h>

#include <string>

#include "support/fake_engine.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/game_viewmodel.hpp"
#include "viewmodel/game/portfolio_vm.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;
using testing::base_state;
using testing::FakeEngine;
using testing::game_result;
using testing::snapshot_of;

namespace {

Order working(int id, const std::string& type, const std::string& side, double price) {
  Order o;
  o.id = id;
  o.type = type;
  o.side = side;
  o.qty = 1;
  o.price = price;
  o.price_text = fmt::price_to_tick(price, 0.01);
  o.status = "active";
  return o;
}

// A risk block as the engine writes it with holdings: true, with one declared limit exceeded.
const char* kHoldings =
    R"({"forward":"100.10000000","atExpiry":false,"legs":[{"kind":"perp","side":"buy","qty":"3.00000000","series":0,"expiryStep":0,"entry":"99.00000000","value":"100.10000000","delta":"1","gamma":"0","theta":"0","vega":"0","positionValue":"300.30000000","pnl":"3.30000000"}],)"
    R"("net":{"value":"300.3","pnl":"3.3","delta":"3","gamma":"0","theta":"0","vega":"0"},"expiry":null,"today":[["90","-27"],["110","33"]],)"
    R"("heatmap":{"prices":["90.00000000","100.00000000","110.00000000"],"times":[0,12],"pnl":[["-27.00000000","3.00000000","33.00000000"],["-27.10000000","2.90000000","32.90000000"]]},)"
    R"("risk":{"deltaUnits":"3.27000000","deltaNotional":"327.30000000","gamma1pct":"0","vegaPoint":"0","thetaBar":"0","fundingNext":"-0.03000000","deltaBp":712,"gammaBp":0,"vegaBp":0,"thetaBp":0,"fundingBp":0,"equity":"10000","limits":[{"kind":"delta","limitBp":500,"valueBp":712,"exceeded":true}]}})";

struct Fixture {
  Catalog catalog = builtin_catalog();
  FakeEngine engine;
  GameViewModel vm{catalog, engine, nullptr, {}};
  GameState g;

  explicit Fixture(GameState state = base_state()) : g(std::move(state)) {
    vm.on_snapshot(*snapshot_of(g, CommandResult{CommandKind::Start, true, "none"}));
  }
  void answer(GameCommandKind kind, bool ok = true, std::string reason = "none", int id = 0) {
    vm.on_snapshot(*snapshot_of(g, game_result(kind, ok, std::move(reason), id)));
  }
  [[nodiscard]] Texts t() const { return vm.texts(); }
};

}  // namespace

TEST_CASE("wave 1 ticket: time in force blocks what the order type cannot take and is logged with the order") {
  Fixture f;
  TicketViewModel& tk = f.vm.ticket;
  tk.size_by = SizeBy::Qty;
  tk.qty = "1";
  tk.tif_choice = TifChoice::Ioc;  // a market order cannot be IOC: it falls back to GTC
  TicketView v = tk.view(f.t(), *f.vm.state(), false);
  CHECK(v.tif.index == static_cast<int>(TifChoice::Gtc));
  CHECK_FALSE(v.tif.blocked[static_cast<std::size_t>(TifChoice::Ioc)].empty());
  CHECK(tk.draft(*f.vm.state(), false, Locale::PtBR)->tif.empty());
  tk.type = TicketType::Limit;
  tk.price = "95";
  v = tk.view(f.t(), *f.vm.state(), false);
  CHECK(v.tif.index == static_cast<int>(TifChoice::Ioc));
  CHECK(v.tif.hint.find("61") != std::string::npos);  // only the open of the next bar
  CHECK(tk.draft(*f.vm.state(), false, Locale::PtBR)->tif == "ioc");
  tk.tif_choice = TifChoice::Bars;
  tk.tif_bars = "500";  // more than the bars left: refused before the engine sees it
  v = tk.view(f.t(), *f.vm.state(), false);
  CHECK(v.tif.error);
  CHECK_FALSE(v.can_submit);
  tk.tif_bars = "3";
  const auto d = tk.draft(*f.vm.state(), false, Locale::PtBR);
  REQUIRE(d.has_value());
  CHECK(d->tif == "bars");
  CHECK(d->tif_bars == 3);
  f.vm.submit();
  REQUIRE(f.vm.modal() == Modal::Plan);
  f.vm.plan_confirm(true);
  f.answer(GameCommandKind::Plan);
  CHECK(f.engine.log.back() == "order 1 buy 1.0 @95.00 tif bars:3");
}

TEST_CASE("wave 1 ticket: Protect with a trailing stop plans it; the notice offers it after the fill, then Protect sends it") {
  Fixture f;
  TicketViewModel& tk = f.vm.ticket;
  tk.size_by = SizeBy::Qty;
  tk.qty = "1";
  tk.stop = "95";
  tk.protect_with_trail = true;
  tk.plan_trail = "1,5";
  TicketView v = tk.view(f.t(), *f.vm.state(), false);
  CHECK(v.trail_planned.find("Nada") != std::string::npos);  // nothing is sent without the click
  f.vm.submit();
  REQUIRE(f.vm.modal() == Modal::Plan);
  f.vm.plan_confirm(true);
  REQUIRE(f.vm.journal().back().trail.has_value());
  CHECK(f.vm.journal().back().trail->value == "1,5");
  f.answer(GameCommandKind::Plan);
  CHECK(f.engine.log.back() == "order 0 buy 1.0");  // no bracket stop: the trailing stop replaces it
  CHECK_FALSE(f.vm.trail_notice().has_value());   // not filled yet
  f.g.step += 1;
  f.g.position.qty = 1;
  f.g.position.qty_text = "1.00000000";
  f.g.position.entry = 100.1;
  f.answer(GameCommandKind::Order, true, "none", 1);
  const auto n = f.vm.trail_notice();
  REQUIRE(n.has_value());
  // The engine's first trigger: the last close minus 1.50 on the tick.
  CHECK(n->text.find("1,50") != std::string::npos);
  f.vm.trail_notice_place();
  CHECK(tk.protect_tab == ProtectTab::Trail);
  CHECK(tk.prot_trail == "1,5");
  v = tk.view(f.t(), *f.vm.state(), false);
  CHECK(v.tabs[1].find("\xE2\x80\xA2") != std::string::npos);
  CHECK(v.trail_rule.find("Gatilho agora") != std::string::npos);
  CHECK_FALSE(f.vm.trail_notice().has_value());  // used once
  f.vm.protect();
  CHECK(f.engine.log.back() == "order 4 sell 1.00000000 ro trail 1.50");
}

TEST_CASE("wave 1 orders: two working orders ticked and linked OCO after a confirmation; problems named") {
  GameState g = base_state();
  g.orders = {working(3, "limit", "buy", 95), working(4, "stop_market", "buy", 105)};
  Fixture f(g);
  CHECK(f.vm.link_view().shown);
  CHECK_FALSE(f.vm.link_view().can_link);
  f.vm.toggle_link_pick(3);
  f.vm.toggle_link_pick(3);
  CHECK_FALSE(f.vm.link_picked(3));
  f.vm.toggle_link_pick(3);
  f.vm.toggle_link_pick(4);
  REQUIRE(f.vm.link_view().can_link);
  f.vm.link_oco();
  REQUIRE(f.vm.modal() == Modal::Confirm);
  CHECK(f.vm.confirm_view().title.find("#3") != std::string::npos);
  f.vm.confirm_answer(true);
  CHECK(f.engine.log.back() == "oco 3 4");
  f.g.orders[0].link_group = 3;
  f.g.orders[1].link_group = 3;
  f.answer(GameCommandKind::LinkOco, true, "none", 3);
  CHECK(f.vm.notice().text.find("#3") != std::string::npos);
  CHECK_FALSE(f.vm.link_picked(3));
  // Linked orders name their partner, in the panel and on the chart; a second link is refused.
  const auto rows = order_rows(f.t(), *f.vm.state(), f.vm.series_names());
  CHECK(rows[0].text.find("\xE2\x9B\x93#4") != std::string::npos);
  f.vm.toggle_link_pick(3);
  f.vm.toggle_link_pick(4);
  CHECK_FALSE(f.vm.link_view().can_link);
  CHECK(f.vm.link_view().hint.find(f.t().tr("orders.linkProblem.linked")) != std::string::npos);
}

TEST_CASE("wave 1 portfolio: the engine's totals, a declared limit exceeded warns (never blocks), Hedge delta asks first") {
  GameState g = base_state();
  g.rules.risk_delta_bp = 500;
  g.position.qty = 4;  // the hedge (a sell of 3.2) reduces it: no plan
  g.position.qty_text = "4.00000000";
  g.position.entry = 99;
  Fixture f(g);
  REQUIRE_FALSE(f.engine.holdings.empty());  // refreshed on every state
  CHECK(f.engine.holdings.back().request.find("\"heatmap\"") != std::string::npos);
  f.engine.holdings.back().reply(kHoldings);
  const PortfolioView v = f.vm.portfolio_view();
  REQUIRE(v.rows.size() == 1);
  CHECK(v.totals.size() == 6);
  CHECK(v.totals[1].second.find("712 bp") != std::string::npos);
  REQUIRE(v.warnings.size() == 1);
  CHECK(v.warnings[0].find("500") != std::string::npos);
  CHECK(v.limits.front().second);  // the row is marked exceeded
  const RailPortfolio rail = f.vm.rail_portfolio_view();
  REQUIRE(rail.warnings.size() == 1);
  // −3.27 units of delta: a perpetual sell of 3.2 (rounded toward zero to the 0.1 lot).
  REQUIRE(v.can_hedge);
  CHECK(v.hedge_note.find("3,2") != std::string::npos);
  f.vm.open_portfolio();
  f.vm.hedge_delta();
  REQUIRE(f.vm.modal() == Modal::Confirm);
  CHECK(f.vm.confirm_view().text.find("0,07") != std::string::npos);  // delta after ≈ 0.07 units
  f.vm.confirm_answer(true);
  REQUIRE_FALSE(f.engine.log.empty());
  CHECK(f.engine.log.back() == "order 0 sell 3.2");  // reduces the long: no plan needed
  // The briefing names the declared limit.
  CHECK(brief_risk_limits(f.t(), f.vm.state()->rules).find("500") != std::string::npos);
}

TEST_CASE("wave 1 heatmap: diverging cells by share of the largest |P&L|, a readout for the focused cell, a table") {
  Fixture f;
  const auto r = whatif_from_json(kHoldings);
  REQUIRE(r.ok());
  const HeatmapView v = heatmap_view(f.t(), *r.value->heatmap, false, 1, 2);
  REQUIRE(v.cells.size() == 2);
  CHECK(v.cells[0][0].share == 80);  // 27 / 33 x 85 → 70, + 10 off zero
  CHECK_FALSE(v.cells[0][0].up);
  CHECK(v.cells[0][2].share == 95);
  CHECK(v.focus.find("110,00") != std::string::npos);
  CHECK(v.focus.find("12") != std::string::npos);
  const ChartScene s = heatmap_scene(v, 300, 120, 1, 2);
  CHECK(s.rects.size() == 2 * 6 + 1);  // background and ink per cell, the focus outline
  const auto hit = heatmap_hit(v, 300, 120, 290, 100);
  REQUIRE(hit.has_value());
  CHECK(hit->first == 1);
  CHECK(hit->second == 2);
  CHECK_FALSE(heatmap_hit(v, 300, 120, 5, 10).has_value());  // the bars axis
}
