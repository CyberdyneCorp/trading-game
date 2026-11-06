// The order ticket (OrderTicket.svelte) and the chart viewmodel (Chart.svelte) on hand-made states.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "support/fake_engine.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/panels_vm.hpp"
#include "viewmodel/game/ticket_vm.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;
using testing::base_state;

namespace {
struct Texted {
  Catalog catalog = builtin_catalog();
  Texts t{catalog};
};
}  // namespace

TEST_CASE("ticket: risk sizing needs a stop; with one, 1% of equity at risk sizes the order") {
  Texted x;
  GameState g = base_state();
  TicketViewModel tk;
  tk.sync(g);
  TicketView v = tk.view(x.t, g, false);
  CHECK_FALSE(v.risk_needs_stop.empty());
  CHECK_FALSE(v.no_stop.empty());
  CHECK(v.est_qty == "10,0");  // risk mode falls back to the quantity field until a stop is set
  CHECK(v.leverage_label == "Alavancagem (\xE2\x89\xA4 5x)");
  tk.stop = "99,1";
  v = tk.view(x.t, g, false);
  CHECK(v.risk_needs_stop.empty());
  const double qty = fmt::parse_decimal(v.est_qty);
  CHECK(qty > 50);
  CHECK(qty < 120);
  REQUIRE(v.stop_hint);
  CHECK(v.stop_hint->find("ATR") != std::string::npos);
  CHECK(v.stop_chips.size() == 5);
  CHECK(v.tp_chips.size() == 3);
  CHECK(v.tp_chips[1].value.has_value());
  REQUIRE(v.preview);
  CHECK(*v.preview->stop == doctest::Approx(99.1));
}

TEST_CASE("ticket: notional in PT-BR reads '1.000' as one thousand credits; % of equity is exposure") {
  Texted x;
  GameState g = base_state();
  TicketViewModel tk;
  tk.size_by = SizeBy::Notional;
  tk.notional = "1.000";
  const double entry = estimated_market_entry(game::Side::Buy, g.account.mark, costs_of(g.contract, std::nullopt));
  CHECK(fmt::parse_decimal(tk.view(x.t, g, false).est_qty) == doctest::Approx(std::floor(1000 / entry * 10) / 10));
  tk.size_by = SizeBy::Equity;
  tk.equity = "10";
  CHECK(fmt::parse_decimal(tk.view(x.t, g, false).est_qty) == doctest::Approx(std::floor(1000 / entry * 10) / 10));
}

TEST_CASE("ticket: wrong-side levels, a missing limit price and an unaffordable size block the submit") {
  Texted x;
  GameState g = base_state();
  TicketViewModel tk;
  tk.size_by = SizeBy::Qty;
  tk.qty = "1";
  CHECK(tk.view(x.t, g, false).can_submit);
  CHECK_FALSE(tk.view(x.t, g, true).can_submit);  // a command in flight
  tk.stop = "150";
  TicketView v = tk.view(x.t, g, false);
  CHECK(v.stop_warn);
  CHECK_FALSE(v.can_submit);
  tk.stop.clear();
  tk.take_profit = "50";
  CHECK(tk.view(x.t, g, false).tp_warn);
  tk.take_profit.clear();
  tk.type = TicketType::Limit;
  CHECK_FALSE(tk.view(x.t, g, false).can_submit);
  tk.price = "99,5";
  CHECK(tk.view(x.t, g, false).can_submit);
  CHECK(tk.draft(g, false, Locale::PtBR)->price == "99.50");
  tk.type = TicketType::Market;
  tk.qty = "500";  // 50,000 cr at 1x
  v = tk.view(x.t, g, false);
  CHECK_FALSE(v.cant_afford.empty());
  CHECK(v.cant_afford.find("10.000,00 cr") != std::string::npos);
  CHECK_FALSE(v.can_submit);
}

TEST_CASE("ticket: a stop beyond liquidation needs a confirmation that belongs to that exact order") {
  Texted x;
  GameState g = base_state();
  TicketViewModel tk;
  tk.size_by = SizeBy::Qty;
  tk.qty = "40";
  tk.leverage = "5";
  tk.stop = "70";  // liquidation of a 5x long is ~80
  TicketView v = tk.view(x.t, g, false);
  CHECK_FALSE(v.beyond_liq.empty());
  CHECK(v.needs_beyond_confirm);
  CHECK_FALSE(v.can_submit);
  tk.confirm_beyond(g, true);
  CHECK(tk.view(x.t, g, false).can_submit);
  tk.qty = "41";  // another order: asked again
  CHECK_FALSE(tk.view(x.t, g, false).can_submit);
}

TEST_CASE("ticket: short locked, opposing order, relever, liquidity shock and margin tier notes") {
  Texted x;
  GameState g = base_state();
  g.contract.allow_short = false;
  TicketViewModel tk;
  tk.side = game::Side::Sell;
  CHECK_FALSE(tk.view(x.t, g, false).short_locked.empty());
  g.contract.allow_short = true;
  g.position.qty = 10;
  g.position.entry = 100;
  g.position.margin = 1000;
  g.position.qty_text = "10.00000000";
  tk.size_by = SizeBy::Qty;
  tk.qty = "4";
  TicketView v = tk.view(x.t, g, false);
  CHECK(v.opposing.find("10,0") != std::string::npos);
  CHECK(v.opposing.find("6,0") != std::string::npos);
  tk.side = game::Side::Buy;
  tk.leverage = "5";
  CHECK(tk.view(x.t, g, false).relever.find("5") != std::string::npos);
  g.liquidity = Liquidity{0.0003, 0.0006, 0.05};
  CHECK_FALSE(tk.view(x.t, g, false).shock.empty());
  g.contract.tiers = {{0, 0.005, 50}, {500, 0.01, 2}};
  CHECK_FALSE(tk.view(x.t, g, false).above_tier.empty());
}

TEST_CASE("ticket: leverage follows the account only when it changes; a fill clears stop and target") {
  GameState g = base_state();
  TicketViewModel tk;
  tk.sync(g);
  tk.leverage = "4";
  tk.stop = "95";
  tk.sync(g);  // a new state without a leverage change keeps what the player typed
  CHECK(tk.leverage == "4");
  g.leverage = 2;
  tk.sync(g);
  CHECK(tk.leverage == "2");
  g.totals.fills = 1;
  tk.sync(g);
  CHECK(tk.stop.empty());
}

TEST_CASE("chart: a frame has candles, volume, the future zone, levels with tags and drag zones") {
  Texted x;
  GameState g = base_state();
  g.position.qty = 1;
  g.position.entry = 100.5;
  g.account.liquidation = 90;
  Order tp;
  tp.id = 3;
  tp.type = "take_profit";
  tp.side = "sell";
  tp.price = 103;
  tp.reduce_only = true;
  g.orders.push_back(tp);
  ChartViewModel c;
  const ChartScene s = c.build(x.t, g, TicketPreview{100.6, 99.0, std::nullopt, 95.0}, false, 1600, 420);
  CHECK_FALSE(s.empty);
  CHECK(s.rects.size() > 60);
  const auto has_text = [&](std::string_view needle) {
    return std::any_of(s.texts.begin(), s.texts.end(), [&](const SceneText& t) { return t.text.find(needle) != std::string::npos; });
  };
  CHECK(has_text("ENTRY"));
  CHECK(has_text("TP #3"));
  CHECK(has_text("SL?"));
  CHECK(has_text("LIQ"));
  CHECK(has_text("RSI 14"));
  CHECK(has_text(x.t.tr("chart.future")));
  CHECK(c.frame().zones.size() == 2);  // TP #3 and SL?
  g.status = "finished";
  const ChartScene done = c.build(x.t, g, std::nullopt, false, 1600, 420);
  CHECK_FALSE(std::any_of(done.texts.begin(), done.texts.end(), [&](const SceneText& t) { return t.text == x.t.tr("chart.future"); }));
}

TEST_CASE("chart: hover readout, wheel zoom, pick on chart, keyboard focus and the bar table") {
  Texted x;
  GameState g = base_state(150);
  ChartViewModel c;
  c.build(x.t, g, std::nullopt, false, 900, 420);
  CHECK(c.zoom() == 100);
  c.wheel(g, 1);  // up: zoom in
  CHECK(c.zoom() == 90);
  for (int i = 0; i < 20; ++i) c.wheel(g, 1);
  CHECK(c.zoom() == 30);
  for (int i = 0; i < 30; ++i) c.wheel(g, -1);
  CHECK(c.zoom() == 151);  // never more than the bars
  c.build(x.t, g, std::nullopt, false, 900, 420);
  c.pointer_move(g, 2, 50, false);
  REQUIRE(c.hover_bar(g));
  CHECK(*c.hover_bar(g) == 0);
  CHECK(c.readout(x.t, g).rfind("#0  O ", 0) == 0);
  const ChartAction pick = c.click(g, 10, 100, true);
  CHECK(pick.kind == ChartAction::Kind::Picked);
  CHECK(pick.price == doctest::Approx(c.price_at(100)));
  c.pointer_leave();
  c.focus_gained(g);
  CHECK(*c.focus_bar() == 150);
  c.key(g, "ArrowLeft");
  CHECK(*c.focus_bar() == 149);
  CHECK(c.announcement(x.t, g).find("149") != std::string::npos);
  c.key(g, "Home");
  CHECK(*c.focus_bar() == 0);
  CHECK(c.zoom() == 151);
  CHECK(c.page(g).end == 151);
  c.page_by(g, -1);
  CHECK(c.page(g).end == 101);
  c.page_by(g, 5);
  CHECK(c.page(g).end == 151);
}

TEST_CASE("chart: warm-up note while EMA50 has no value and continuity bars counted") {
  Texted x;
  GameState g = base_state(20);
  g.indicators.ema50.assign(g.bars.size(), std::nullopt);
  g.bars[10].continuity = true;
  ChartViewModel c;
  const std::string line = c.status_line(x.t, g, false);
  CHECK(line.find("EMA50") != std::string::npos);
  CHECK(line.find("29") != std::string::npos);
  CHECK(line.find('1') != std::string::npos);
}

TEST_CASE("rail and panels: liquidation warning, funding, orders, feed and news lines") {
  Texted x;
  GameState g = base_state();
  g.position.qty = 2;
  g.position.entry = 100;
  g.position.margin = 40;
  g.leverage = 5;
  g.account.mark = 100;
  g.account.liquidation = 98.5;
  g.account.next_funding_rate = 0.0001;
  g.account.next_funding_step = 64;
  g.rules.objectives = {{"risk_per_idea", 100}};
  const RailView r = rail_view(x.t, g, 30.0);
  CHECK(r.open);
  CHECK(r.liq_danger);
  CHECK_FALSE(r.liq_warning.empty());
  CHECK(r.funding_in == "em 4 barras");
  CHECK(r.planned_risk->value == "30,00 cr");
  CHECK(r.objectives.size() == 1);
  Order o;
  o.id = 5;
  o.type = "stop_market";
  o.side = "sell";
  o.qty = 2;
  o.price_text = "95.00000000";
  o.reduce_only = true;
  g.orders.push_back(o);
  SeriesNames names;
  const auto rows = order_rows(x.t, g, names);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].text.find("#5  STOP") == 0);
  CHECK(rows[0].text.find("95,00") != std::string::npos);
  News n;
  n.step = 60;
  n.kind = "macro_data";
  n.tone = 1;
  n.credibility = 80;
  g.news.push_back(n);
  g.upcoming.push_back({70, "rate_decision", 0});
  const NewsView nv = news_view(x.t, g);
  CHECK(nv.upcoming.size() == 1);
  CHECK(nv.items[0].text.find("[60]") == 0);
  CHECK(nv.items[0].tone == Tone::Up);
  CHECK(top_bar(x.t, g).turn == "Barra 1 de 79 \xC2\xB7 1h");
}

TEST_CASE("chart: after the reveal the ended run's chart shows its regimes as named bands") {
  Texted x;
  GameState g = base_state();
  g.status = "finished";
  ChartViewModel c;
  const auto count_bands = [](const ChartScene& s) {
    return std::count_if(s.rects.begin(), s.rects.end(), [](const SceneRect& r) { return r.alpha == 0.6f && r.h == 6; });
  };
  CHECK(count_bands(c.build(x.t, g, std::nullopt, false, 1600, 420)) == 0);  // nothing before the reveal
  const auto spans = json::parse(R"([{"label":"range","direction":0,"start":0,"bars":40},
                                     {"label":"trend","direction":1,"start":40,"bars":21},
                                     {"label":"shock","direction":-1,"start":200,"bars":5}])");
  REQUIRE(spans);
  c.set_regimes(regime_spans(*spans));
  REQUIRE(c.regimes().size() == 3);
  const ChartScene s = c.build(x.t, g, std::nullopt, false, 1600, 420);
  CHECK(count_bands(s) == 2);  // the third span lies after the last bar
  const auto has_text = [&](const std::string& t) {
    return std::any_of(s.texts.begin(), s.texts.end(), [&](const SceneText& st) { return st.text == t; });
  };
  CHECK(has_text("Lateral"));
  CHECK(has_text("Tendência de alta"));
  // The trend band starts where bar 40 starts.
  const float slot = c.frame().slot_w;
  CHECK(std::any_of(s.rects.begin(), s.rects.end(),
                    [&](const SceneRect& r) { return r.h == 6 && r.ink == Ink::Info && r.x == std::round(40 * slot); }));
}

TEST_CASE("chart: a branch-mode market marks its first generated bar and tints what follows") {
  Texted x;
  GameState g = base_state();
  ChartViewModel c;
  const auto marked = [&](const ChartScene& s) {
    return std::any_of(s.texts.begin(), s.texts.end(), [&](const SceneText& t) { return t.text == x.t.tr("top.branch"); });
  };
  for (auto& b : g.bars) b.synthetic = false;
  g.market_mode = "branch";
  CHECK_FALSE(marked(c.build(x.t, g, std::nullopt, false, 1600, 420)));  // not branched yet
  CHECK_FALSE(branch_turn(g));
  for (std::size_t i = 50; i < g.bars.size(); ++i) g.bars[i].synthetic = true;
  const ChartScene s = c.build(x.t, g, std::nullopt, false, 1600, 420);
  CHECK(marked(s));
  CHECK(std::any_of(s.lines.begin(), s.lines.end(),
                    [](const SceneLine& l) { return l.ink == Ink::Attention && l.dash == 4 && l.x0 == l.x1; }));
  REQUIRE(branch_turn(g));
  CHECK(*branch_turn(g) == 50 - g.warmup_bars + 1);
  CHECK(top_bar(x.t, g).market == "Mercado K2A RAMO SINTÉTICO");
  g.market_mode = "synthetic";  // a synthetic mission: every bar is generated, nothing to mark
  CHECK_FALSE(marked(c.build(x.t, g, std::nullopt, false, 1600, 420)));
  CHECK(top_bar(x.t, g).market == "Mercado K2A SINTÉTICO");
}
