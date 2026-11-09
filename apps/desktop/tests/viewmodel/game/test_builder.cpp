// The strategy builder and the what-if lab in the terminal (web StrategyBuilder.svelte, ComboNotice,
// WhatIfLab; add-academy-and-strategies D3, D4): with a fake engine, the analysis requests and stale
// replies, the combo's plan (one per leg for the engine, one journal entry), its legs in submission
// order with their tags, the cancel of the accepted legs when one is refused, the incomplete notice and
// its close; with the real engine host, a condor previewed, submitted and filled, and the lab at zero
// shift equal to the chain. Values typed as a PT-BR player would.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "model/engine_host.hpp"
#include "model/engine_run.hpp"
#include "model/storage/actions.hpp"
#include "support/fake_engine.hpp"
#include "support/ui_queue.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/game_viewmodel.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;
using testing::base_state;
using testing::FakeEngine;
using testing::game_result;
using testing::snapshot_of;
using testing::UiQueue;

namespace {

constexpr double kStrikes[] = {90, 92.5, 95, 97.5, 100, 102.5, 105, 107.5, 110};

// Calls have odd ids (1, 3, ...), puts even ones: strike i -> call 2i+1, put 2i+2. Last price 100.1.
GameState options_state(bool write = true) {
  GameState g = base_state();
  g.mission = "m16-range-condor";
  g.options.enabled = true;
  g.options.allow_short = write;
  g.options.multiplier = 1;
  g.options.lot = 0.1;
  g.options.premium_tick = 0.01;
  g.rules.options_buy = true;
  g.rules.options_write = write;
  g.rules.spread_margin = true;
  int i = 0;
  for (const double k : kStrikes) {
    for (const char* kind : {"call", "put"}) {
      OptionQuote q;
      q.id = 2 * i + (std::string(kind) == "call" ? 1 : 2);
      q.kind = kind;
      q.strike = k;
      q.strike_text = fmt::price_to_tick(k, 0.01);
      q.expiry_step = 100;
      q.bid = k == 110 && q.kind == "call" ? 0 : 1.0;
      q.ask = q.bid + 0.2;
      g.options.chain.push_back(q);
    }
    ++i;
  }
  return g;
}

const char* kWhatIf =
    R"({"forward":"100.10000000","atExpiry":false,"legs":[],"net":{"value":"0","pnl":"-0.10000000","delta":"0","gamma":"0","theta":"0.01","vega":"-0.02"},)"
    R"("expiry":{"breakevens":["98.20000000","102.90000000"],"maxProfit":"0.60000000","maxLoss":"1.90000000","curve":[["70","-1.9"],["100","0.6"],["130","-1.9"]]},)"
    R"("today":[["70","-1.5"],["100","0.4"],["130","-1.5"]]})";

std::string preview(bool accepted) {
  const std::string leg = R"({"accepted":true,"reason":"none","quote":"1.20000000","premium":"1.2","fee":"0.01","required":"0","availableAfter":"9999"})";
  const std::string refused = R"({"accepted":false,"reason":"insufficient_margin","quote":"1.00000000","premium":"1","fee":"0.01","required":"9","availableAfter":"0"})";
  return std::string(R"({"accepted":)") + (accepted ? "true" : "false") + R"(,"legs":[)" + leg + "," + leg + "," + leg + "," +
         (accepted ? leg : refused) +
         R"(],"netPremium":"0.60000000","fees":"0.04","required":"2.50000000","availableAfter":"9997.5","optionMarginAfterFill":"2.50000000","positionMarginAfterFill":"0","availableAfterFill":"9997.10000000"})";
}

struct Fixture {
  Catalog catalog = builtin_catalog();
  FakeEngine engine;
  GameViewModel vm{catalog, engine, nullptr, {}};
  GameState g;

  explicit Fixture(GameState state = options_state()) : g(std::move(state)) {
    vm.on_snapshot(*snapshot_of(g, CommandResult{CommandKind::Start, true, "none"}));
  }
  void answer(GameCommandKind kind, bool ok = true, std::string reason = "none", int id = 0) {
    vm.on_snapshot(*snapshot_of(g, game_result(kind, ok, std::move(reason), id)));
  }
  void condor() {
    vm.set_tab(Tab::Builder);
    vm.builder.pick(*vm.state(), Template::IronCondor, Locale::PtBR);
    vm.builder_changed();
  }
  void reply_analysis(bool accepted = true) {
    REQUIRE(engine.analyses.size() >= 2);
    const auto n = engine.analyses.size();
    engine.analyses[n - 2].reply(kWhatIf);
    engine.analyses[n - 1].reply(preview(accepted));
  }
};

}  // namespace

TEST_CASE("builder: an iron condor's legs from the chain, its analysis from the engine, stale replies dropped") {
  Fixture f;
  f.condor();
  const auto& legs = f.vm.builder.legs;
  REQUIRE(legs.size() == 4);
  CHECK(*legs[0].strike == 97.5);  // +P below the forward (100.1), −P, −C, +C above
  CHECK(*legs[1].strike == 100);
  CHECK(*legs[2].strike == 102.5);
  CHECK(*legs[3].strike == 105);
  REQUIRE(f.engine.analyses.size() == 2);
  CHECK(f.engine.analyses[0].kind == AnalysisKind::RunWhatIf);
  CHECK(f.engine.analyses[1].kind == AnalysisKind::ComboPreview);
  // Submission order: long options, then short options.
  CHECK(f.engine.analyses[1].request ==
        R"({"legs":[{"series":8,"side":"buy","qty":"1.0"},{"series":13,"side":"buy","qty":"1.0"},{"series":10,"side":"sell","qty":"1.0"},{"series":11,"side":"sell","qty":"1.0"}]})");
  CHECK_FALSE(f.vm.builder_view().can_submit);  // waiting for the engine
  // A changed quantity asks again; the first answers arrive late and are dropped.
  f.vm.builder.qty = "2";
  f.vm.builder.apply_qty(Locale::PtBR);
  f.vm.builder_changed();
  REQUIRE(f.engine.analyses.size() == 4);
  f.engine.analyses[0].reply(kWhatIf);
  f.engine.analyses[1].reply(preview(true));
  CHECK_FALSE(f.vm.builder.whatif().has_value());
  f.reply_analysis();
  const BuilderView v = f.vm.builder_view();
  CHECK(v.can_submit);
  CHECK(v.caption == "Iron condor");
  REQUIRE(v.analysis.size() == 3);
  CHECK(v.analysis[0].second == "98,20 \xC2\xB7 102,90");
  CHECK(v.analysis[2].second.find("1,90") != std::string::npos);
  REQUIRE(v.totals.size() == 4);
  CHECK(v.totals[0].first.find("Cr\xC3\xA9" "dito") != std::string::npos);
  CHECK(v.totals[2].first.find("venda coberta") != std::string::npos);  // spread margin declared by the mission
  REQUIRE(v.preview.size() == 5);
  CHECK(v.preview[1].text.find("+2,0 P 97,50") != std::string::npos);
  // The payoff chart has the lab's legend (price range, line styles) and an accessible description.
  CHECK(v.chart_legend.rfind("70,00", 0) == 0);
  CHECK(v.chart_legend.find("no vencimento") != std::string::npos);
  CHECK(v.chart_summary.find("98,20") != std::string::npos);
}

TEST_CASE("builder: a refused leg in the dry run blocks the submit") {
  Fixture f;
  f.condor();
  f.reply_analysis(false);
  const BuilderView v = f.vm.builder_view();
  CHECK_FALSE(v.can_submit);
  CHECK(v.preview.back().tone == Tone::Down);
  f.vm.builder_submit();
  CHECK(f.vm.modal() == Modal::None);
}

TEST_CASE("builder: one plan per leg for the engine, one journal entry, then the legs tagged in order") {
  Fixture f;
  f.condor();
  f.reply_analysis();
  f.vm.builder_submit();
  REQUIRE(f.vm.modal() == Modal::Plan);
  const std::string headline = f.vm.plan.headline(f.vm.texts());
  CHECK(headline.find("Iron condor \xC2\xB7 4 pernas") != std::string::npos);
  CHECK(headline.find("1,90") != std::string::npos);  // the planned risk is the maximum loss
  f.vm.plan.thesis = "range com IV acima da realizada";
  f.vm.plan_confirm(false);
  REQUIRE(f.engine.log.size() == 1);
  CHECK(f.engine.log[0] == "plan inv  risk 0.47");  // 1.90 split over four legs
  for (int i = 0; i < 4; ++i) f.answer(GameCommandKind::Plan);
  CHECK(f.engine.log.back() == "option 8 buy 1.0 [c1 iron_condor]");
  f.answer(GameCommandKind::Option, true, "none", 31);
  CHECK(f.engine.log.back() == "option 13 buy 1.0 [c1 iron_condor]");
  f.answer(GameCommandKind::Option, true, "none", 32);
  CHECK(f.engine.log.back() == "option 10 sell 1.0 [c1 iron_condor]");
  f.answer(GameCommandKind::Option, true, "none", 33);
  CHECK(f.engine.log.back() == "option 11 sell 1.0 [c1 iron_condor]");
  f.answer(GameCommandKind::Option, true, "none", 34);
  CHECK(f.vm.notice().text.find("Iron condor") != std::string::npos);
  CHECK(f.vm.notice().tone == Tone::Ok);
  REQUIRE(f.vm.journal().size() == 1);
  CHECK(f.vm.journal()[0].combo == "c1");
  CHECK(f.vm.journal()[0].instrument == "Iron condor \xC2\xB7 4 pernas");
  CHECK(f.vm.journal()[0].thesis == "range com IV acima da realizada");
}

TEST_CASE("builder: a leg refused at placement cancels the legs already accepted, before anything else") {
  Fixture f;
  f.condor();
  f.reply_analysis();
  f.vm.builder_submit();
  f.vm.plan_confirm(true);
  for (int i = 0; i < 4; ++i) f.answer(GameCommandKind::Plan);
  f.vm.advance(1);  // pressed meanwhile: waits for the combo
  f.answer(GameCommandKind::Option, true, "none", 21);
  f.answer(GameCommandKind::Option, true, "none", 22);
  f.answer(GameCommandKind::Option, false, "insufficient_margin");
  CHECK(f.engine.log.back() == "cancel 21 [c1 iron_condor]");
  CHECK(f.vm.notice().text.find("Perna 3 recusada") != std::string::npos);
  f.answer(GameCommandKind::Cancel);
  CHECK(f.engine.log.back() == "cancel 22 [c1 iron_condor]");
  f.answer(GameCommandKind::Cancel);
  CHECK(f.engine.log.back() == "advance 1");
  CHECK(std::count_if(f.engine.log.begin(), f.engine.log.end(), [](const std::string& l) { return l.rfind("option 11", 0) == 0; }) == 0);
}

TEST_CASE("builder: writing locked by the mission disables the templates that write, with the reason") {
  Fixture f(options_state(false));
  f.vm.set_tab(Tab::Builder);
  const BuilderView v = f.vm.builder_view();
  REQUIRE(v.templates.size() == 11);
  CHECK_FALSE(v.templates[0].enabled);  // bull call spread
  CHECK(v.templates[3].enabled);        // protective put
  CHECK(v.templates[5].enabled);        // long strangle
  CHECK_FALSE(v.templates[6].enabled);  // iron condor
  CHECK_FALSE(v.templates[7].enabled);  // calendar (writes the near expiry)
  CHECK_FALSE(v.write_locked.empty());
  f.vm.builder.pick(*f.vm.state(), Template::Strangle, Locale::PtBR);
  f.vm.builder.toggle_short(*f.vm.state(), Locale::PtBR);
  CHECK(f.vm.builder.strategy == "short_strangle");
  CHECK(f.vm.builder.problem(*f.vm.state()) == "write_locked");
}

TEST_CASE("builder: covered call on a held long uses that long and covers all of it") {
  GameState g = options_state();
  g.position.qty = 4.8;
  g.position.qty_text = "4.80000000";
  g.position.entry = 99.5;
  g.position.entry_text = "99.50000000";
  Fixture f(g);
  f.vm.set_tab(Tab::Builder);
  f.vm.builder.pick(*f.vm.state(), Template::CoveredCall, Locale::PtBR);
  f.vm.builder_changed();
  CHECK(f.vm.builder.use_long);
  CHECK(f.vm.builder.qty == "4,8");
  REQUIRE(f.engine.analyses.size() == 2);
  // Only the short call is sent; the what-if values the held long from its entry.
  CHECK(f.engine.analyses[1].request == R"({"legs":[{"series":11,"side":"sell","qty":"4.8"}]})");
  CHECK(f.engine.analyses[0].request ==
        R"({"legs":[{"series":11,"side":"sell","qty":"4.8"},{"kind":"perp","side":"buy","qty":"4.80000000","entry":"99.50000000"}]})");
}

namespace {

GameState held_long_state() {
  GameState g = options_state();
  g.position.qty = 4.8;
  g.position.qty_text = "4.80000000";
  g.position.entry = 99.5;
  g.position.entry_text = "99.50000000";
  return g;
}

Order stop_order(int id, const std::string& type, double price, double qty, double filled = 0) {
  Order o;
  o.id = id;
  o.type = type;
  o.side = "sell";
  o.qty = qty;
  o.filled = filled;
  o.price = price;
  o.price_text = fmt::price_to_tick(price, 0.01);
  o.reduce_only = true;
  o.status = "working";
  return o;
}

}  // namespace

// Playtest fix (web strategies.ts heldLong/longStop): the covered call on a stopped long planned the
// long's whole value as its risk ("one entry risked ~4,650 cr"); the long's stop now bounds it.
TEST_CASE("builder: a covered call on a held long with a stop values the long to that stop; the size follows the long") {
  GameState g = held_long_state();
  g.orders = {stop_order(1, "stop_market", 97.5, 4.8), stop_order(2, "trailing_stop", 98.2, 5),
              stop_order(3, "stop_market", 99, 4.8, 1)};  // partly filled: no longer closes all of it
  Fixture f(g);
  CHECK(long_stop(*f.vm.state()) == std::optional<std::string>("98.20"));
  f.vm.set_tab(Tab::Builder);
  f.vm.builder.pick(*f.vm.state(), Template::CoveredCall, Locale::PtBR);
  f.vm.builder_changed();
  REQUIRE(f.engine.analyses.size() == 2);
  CHECK(f.engine.analyses[0].request ==
        R"({"legs":[{"series":11,"side":"sell","qty":"4.8"},{"kind":"perp","side":"buy","qty":"4.80000000","entry":"99.50000000","stop":"98.20"}]})");
  f.reply_analysis();
  const BuilderView v = f.vm.builder_view();
  CHECK(v.size_held.find("98,20") != std::string::npos);
  CHECK_FALSE(v.sizing);  // the size is the long's, not a risk budget's
  CHECK_FALSE(f.vm.builder.sized(*f.vm.state(), Locale::PtBR).has_value());
  // The combo's planned risk is the engine's maximum loss with the stop.
  const auto d = f.vm.builder.draft(f.vm.texts(), *f.vm.state(), {}, false);
  REQUIRE(d.has_value());
  CHECK(d->risk == doctest::Approx(1.9));
}

TEST_CASE("builder: a held long without a stop says its maximum loss runs to a price of 0") {
  Fixture f(held_long_state());
  f.vm.set_tab(Tab::Builder);
  f.vm.builder.pick(*f.vm.state(), Template::ProtectivePut, Locale::En);
  f.vm.builder_changed();
  REQUIRE(f.engine.analyses.size() == 2);
  CHECK(f.engine.analyses[0].request.find("stop") == std::string::npos);
  f.reply_analysis();
  CHECK(f.vm.builder_view().size_held.find("pre\xC3\xA7o 0") != std::string::npos);
}

// Playtest fix: the builder defaulted to 1 contract with no way to size; applying the size wrote no
// quantity into the legs, and the hint showed the old maximum loss while the engine re-analysed.
TEST_CASE("builder: the sizing aid turns a risk % of equity into contracts from the engine's maximum loss") {
  Fixture f;
  f.condor();
  f.reply_analysis();  // maximum loss 1,90 at 1 contract; equity 10.000
  BuilderView v = f.vm.builder_view();
  REQUIRE(v.sizing);
  CHECK(v.size_label == "Risco (% patrim\xC3\xB4nio)");
  CHECK(v.size_apply == "Usar 52,6 contratos");  // 1% of 10.000 / 1,90 = 52,63 -> the 0,1 lot
  CHECK(v.can_size);
  CHECK(v.size_hint.find("1,0 contratos") != std::string::npos);
  CHECK(v.size_hint.find("1,90") != std::string::npos);
  f.vm.builder.risk_pct = "2,5";
  CHECK(f.vm.builder.sized(*f.vm.state(), Locale::PtBR) == doctest::Approx(131.5));
  f.vm.builder.risk_pct = "1";
  REQUIRE(f.vm.builder.apply_size(*f.vm.state(), Locale::PtBR));
  f.vm.builder_changed();
  for (const auto& l : f.vm.builder.legs) CHECK(l.qty == doctest::Approx(52.6));
  CHECK(f.vm.builder.qty == "52,6");
  REQUIRE(f.engine.analyses.size() == 4);
  CHECK(f.engine.analyses[3].request.find(R"("qty":"52.6")") != std::string::npos);
  v = f.vm.builder_view();
  CHECK(v.size_hint.empty());  // the old maximum loss is not shown while the engine re-analyses
  // Legs of different sizes: no single size to suggest.
  f.reply_analysis();
  f.vm.builder.set_leg_qty(0, "3");
  CHECK_FALSE(f.vm.builder_view().sizing);
  CHECK_FALSE(f.vm.builder.apply_size(*f.vm.state(), Locale::PtBR));
}

TEST_CASE("builder: an incomplete combo names the open legs and their risk; keep, or close after confirming") {
  Fixture f;
  auto s = std::make_shared<Snapshot>(*snapshot_of(f.g, CommandResult{CommandKind::Advance, true, "none"}));
  GameState g = f.g;
  g.step = 61;
  const auto ev = [](int seq, int step, const char* type, int id, const char* status, const char* side) {
    EngineEvent e;
    e.seq = seq;
    e.step = step;
    e.type = type;
    e.order_id = id;
    e.status = status;
    e.side = side;
    e.qty = "1.00000000";
    return e;
  };
  g.events = {ev(1, 60, "order", 5, "accepted", "buy"), ev(2, 60, "order", 6, "accepted", "sell"), ev(3, 61, "fill", 5, "", "buy"),
              ev(4, 61, "order", 6, "cancelled", "sell")};
  g.events[2].price = "1.20000000";
  g.events[3].reason = "no_bid";
  s->game = std::make_shared<GameState>(g);
  s->log = std::make_shared<const std::vector<std::string>>(std::vector<std::string>{
      R"({"k":"opt","series":9,"side":"buy","type":"market","qty":"1.0","premium":null,"combo":"c1","strategy":"bull_call"})",
      R"({"k":"opt","series":11,"side":"sell","type":"market","qty":"1.0","premium":null,"combo":"c1","strategy":"bull_call"})",
      R"({"k":"adv","n":1})"});
  f.vm.on_snapshot(*s);
  auto n = f.vm.combo_notice();
  REQUIRE(n.has_value());
  CHECK(n->title.find("Trava de alta com calls") != std::string::npos);
  CHECK(n->open_legs.find("compra C 100,00") != std::string::npos);
  CHECK(n->cancelled_legs.find("venda C 102,50") != std::string::npos);
  REQUIRE(f.engine.analyses.size() == 1);  // the risk of the legs actually held
  CHECK(f.engine.analyses[0].request == R"({"legs":[{"series":9,"side":"buy","qty":"1.0","entry":"1.20000000"}]})");
  f.engine.analyses[0].reply(
      R"({"forward":"100.1","atExpiry":false,"legs":[],"net":{"value":"1","pnl":"0","delta":"0.5","gamma":"0","theta":"0","vega":"0"},"expiry":{"breakevens":["101.20000000"],"maxProfit":null,"maxLoss":"1.20000000","curve":[]},"today":[]})");
  n = f.vm.combo_notice();
  CHECK(n->risk.find("1,20") != std::string::npos);
  CHECK(f.vm.combo_note("c1").rfind("incompleto", 0) == 0);
  f.vm.combo_close();
  REQUIRE(f.vm.modal() == Modal::ConfirmCombo);
  CHECK(f.vm.combo_close_text().find("compra C 100,00") != std::string::npos);
  f.vm.combo_close_confirm(true);
  CHECK(f.engine.log.back() == "option 9 sell 1.0");
  CHECK_FALSE(f.vm.combo_notice().has_value());  // handled: no longer shown
}

TEST_CASE("terminal lab: from the positions, sliders ask the engine again, only the latest answer shows") {
  GameState g = options_state();
  g.options.positions.push_back({9, 2, 1.25, "2.00000000", "1.25000000"});
  Fixture f(g);
  f.vm.open_lab_positions();
  REQUIRE(f.vm.modal() == Modal::Lab);
  REQUIRE(f.engine.analyses.size() == 1);
  CHECK(f.engine.analyses[0].kind == AnalysisKind::RunWhatIf);
  CHECK(f.engine.analyses[0].request.find(R"("shift":{"forward":"100.10","bars":0,"ivPoints":"0"})") != std::string::npos);
  CHECK(f.vm.lab.max_time() == 40);  // bars to the expiry at step 100
  f.vm.lab.slide(Slider::Price, 7);
  f.vm.lab.slide(Slider::Time, 99);  // clamped to the expiry
  f.vm.lab_changed();
  REQUIRE(f.engine.analyses.size() == 2);
  CHECK(f.engine.analyses[1].request.find(R"("shift":{"forward":"107.11","bars":40,"ivPoints":"0"})") != std::string::npos);
  f.engine.analyses[0].reply(kWhatIf);  // the zero-shift answer arrives after the slider moved: dropped
  CHECK_FALSE(f.vm.lab.result().has_value());
  f.engine.analyses[1].reply(kWhatIf);
  REQUIRE(f.vm.lab.result().has_value());
  const LabView v = f.vm.lab.view(f.vm.texts());
  CHECK(v.note.find("Black-76") != std::string::npos);
  CHECK(v.sliders[0].text == "+7% \xE2\x86\x92 107,11");
  CHECK(v.sliders[1].text == "+40 barras");
  CHECK(f.vm.handle_key({"Space", false, false, false}) == false);  // the market cannot move behind the lab
  CHECK(f.engine.log.empty());
  CHECK(f.vm.handle_key({"Escape", false, false, false}));
  CHECK(f.vm.modal() == Modal::None);
}

TEST_CASE("terminal lab: an engine without the analysis says so") {
  GameState g = options_state();
  g.options.positions.push_back({9, 1, 1.25, "1.00000000", "1.25000000"});
  Fixture f(g);
  f.vm.open_lab_positions();
  f.engine.analyses[0].reply(R"({"error":"unsupported"})");
  CHECK(f.vm.lab.view(f.vm.texts()).error.find("vers\xC3\xA3o do motor") != std::string::npos);
}

// ---- the real engine (EngineHost worker, UI queue) ----

namespace {

struct RealRun {
  UiQueue ui;
  Catalog catalog = builtin_catalog();
  std::unique_ptr<GameViewModel> vm;
  EngineHost host{[this](EngineHost::Task t) { ui.post(std::move(t)); },
                  [this](std::shared_ptr<const Snapshot> s) { vm->on_snapshot(*s); }};

  explicit RealRun(const std::string& mission, std::uint32_t seed) {
    vm = std::make_unique<GameViewModel>(catalog, host, nullptr, std::vector<std::string>{});
    host.start(mission, seed);
    REQUIRE(ui.pump_until([&] { return vm->state() != nullptr; }));
  }
  // Runs the worker and the UI queue until neither has anything left (a reply can send a command).
  void settle() {
    for (int i = 0; i < 1000; ++i) {
      host.wait_idle();
      if (ui.drain() == 0 && !vm->busy() && !vm->builder.pending()) return;
    }
    FAIL("the terminal never settled");
  }
};

}  // namespace

TEST_CASE("builder on the real engine: m16's iron condor previewed, submitted as tagged legs, filled at the open") {
  RealRun r("m16-range-condor", 3);
  GameViewModel& vm = *r.vm;
  REQUIRE(vm.state()->options.enabled);
  CHECK(vm.state()->rules.spread_margin);
  vm.set_tab(Tab::Builder);
  vm.builder.pick(*vm.state(), Template::IronCondor, Locale::PtBR);
  vm.builder_changed();
  r.settle();
  REQUIRE(vm.builder.whatif().has_value());
  REQUIRE(vm.builder.whatif()->ok());
  REQUIRE(vm.builder.preview()->ok());
  const ComboPreview& p = *vm.builder.preview()->value;
  CHECK(p.accepted);
  const auto& e = vm.builder.whatif()->value->expiry;
  REQUIRE(e.has_value());
  REQUIRE(e->max_loss.has_value());  // defined risk
  CHECK(e->breakevens.size() == 2);
  // Spread margin: the condor's margin is its width (2.5 per unit), not the two naked shorts'.
  CHECK(fmt::num(p.option_margin_after_fill) == doctest::Approx(2.5).epsilon(1e-9));
  vm.builder_submit();
  REQUIRE(vm.modal() == Modal::Plan);
  vm.plan_confirm(true);
  r.settle();
  if (vm.modal() == Modal::Calibration) {
    vm.calibration_answer(true);
    r.settle();
  }
  REQUIRE(vm.combos().size() == 1);
  CHECK(vm.combos()[0].strategy == "iron_condor");
  CHECK(vm.combos()[0].legs.size() == 4);
  vm.advance(1);
  r.settle();
  const auto st = vm.combo_statuses();
  REQUIRE(st.size() == 1);
  CHECK(st[0].state == ComboState::Filled);
  CHECK(vm.state()->options.positions.size() == 4);
  CHECK_FALSE(vm.combo_notice().has_value());
  CHECK(vm.combo_note("c1").rfind("completo", 0) == 0);
}

TEST_CASE("builder on the real engine: a covered call on a stopped long plans the loss to the stop, not the long's value") {
  RealRun r("m14-covered-call", 3);
  GameViewModel& vm = *r.vm;
  const double last = vm.state()->last_price();
  vm.ticket.size_by = SizeBy::Qty;
  vm.ticket.qty = "10";
  vm.ticket.stop = fmt::fmt_num(last * 0.97, Locale::PtBR, 2);
  vm.submit();
  REQUIRE(vm.modal() == Modal::Plan);
  vm.plan_confirm(true);
  r.settle();
  for (int i = 0; i < 4 && vm.state()->position.qty <= 0; ++i) {  // a due calibration question first
    if (vm.modal() == Modal::Calibration) vm.calibration_answer(true);
    else vm.advance(1);
    r.settle();
  }
  REQUIRE(vm.state()->position.qty > 0);
  const auto stop = long_stop(*vm.state());
  REQUIRE(stop.has_value());
  vm.set_tab(Tab::Builder);
  vm.builder.pick(*vm.state(), Template::CoveredCall, Locale::PtBR);
  vm.builder_changed();
  r.settle();
  REQUIRE(vm.builder.use_long);
  REQUIRE(vm.builder.whatif().has_value());
  REQUIRE(vm.builder.whatif()->ok());
  const auto& e = vm.builder.whatif()->value->expiry;
  REQUIRE(e.has_value());
  REQUIRE(e->max_loss.has_value());
  const double loss = fmt::num(*e->max_loss);
  const double value = vm.state()->position.qty * vm.state()->position.entry;
  CHECK(loss > 0);
  CHECK(loss < value * 0.05);  // without the stop it was the long's whole value
  // The loss to the stop: the long from its breakeven (entry less the call's premium) down to the stop.
  CHECK(loss <= vm.state()->position.qty * (vm.state()->position.entry - fmt::num(*stop)) + 1e-6);
  CHECK(vm.builder_view().size_held.find(fmt::fmt_price(*stop, Locale::PtBR)) != std::string::npos);
}

TEST_CASE("terminal lab on the real engine: at zero shift every leg's value is its series' mid and its Greeks the chain's") {
  RealRun r("m13-call-spread", 7);
  GameViewModel& vm = *r.vm;
  const GameState& g = *vm.state();
  REQUIRE(g.options.enabled);
  std::vector<AnalysisLeg> legs;
  for (const auto& q : g.options.chain) {
    if (q.listed) legs.push_back({q.id, true, "1", std::nullopt});
  }
  REQUIRE(legs.size() >= 8);
  for (std::size_t from = 0; from < legs.size(); from += 8) {  // the engine values up to 8 legs at once
    const std::vector<AnalysisLeg> part(legs.begin() + static_cast<std::ptrdiff_t>(from),
                                        legs.begin() + static_cast<std::ptrdiff_t>(std::min(legs.size(), from + 8)));
    vm.lab.open_terminal(part, g.last_price(), 0);
    vm.lab_changed();
    REQUIRE(r.ui.pump_until([&] { return vm.lab.result().has_value() || !vm.lab.error().empty(); }));
    REQUIRE_MESSAGE(vm.lab.result().has_value(), vm.lab.error());
    const WhatIfResult& w = *vm.lab.result();
    REQUIRE(w.legs.size() == part.size());
    for (std::size_t i = 0; i < part.size(); ++i) {
      const auto q = std::find_if(g.options.chain.begin(), g.options.chain.end(), [&](const OptionQuote& x) { return x.id == part[i].series; });
      INFO("series " << q->id);
      const double tick = g.options.premium_tick;
      CHECK(std::nearbyint(fmt::num(w.legs[i].value) / tick) * tick == doctest::Approx(q->mid).epsilon(1e-12));  // half-even
      CHECK(fmt::num(w.legs[i].delta) == q->delta);
      CHECK(fmt::num(w.legs[i].gamma) == q->gamma);
      CHECK(fmt::num(w.legs[i].theta) == q->theta);
      CHECK(fmt::num(w.legs[i].vega) == q->vega);
    }
  }
}

// Design D3 "Combos in the log": the desktop logs the web's tags on each leg (and on the cancels of a
// withdrawn combo); replay ignores them, so a tagged log reaches the untagged log's digest.
TEST_CASE("builder: tagged legs are logged as the web logs them and replay to the untagged digest") {
  auto plain = EngineRun::create("m16-range-condor", 4);
  auto tagged = EngineRun::create("m16-range-condor", 4);
  REQUIRE(plain);
  REQUIRE(tagged);
  const GameState g = *plain->snapshot().game;
  const int expiry = expiries_of(g.options.chain).front();
  const auto legs = template_legs(Template::IronCondor, strikes_of(g.options.chain, expiry), g.last_price(), 1);
  REQUIRE(legs);
  const auto orders = order_legs(g, expiry, *legs);
  REQUIRE(orders);
  for (const auto& o : *orders) {
    const GameCommand c = GameCommand::option_order(o.series, o.buy, false, o.qty, {});
    const storage::Action untagged = storage::actions::from_game(c);
    const storage::Action with_tag = storage::actions::from_game(c.tagged("c1", "iron_condor"));
    CHECK(with_tag.json.find(R"("combo":"c1","strategy":"iron_condor"})") != std::string::npos);
    CHECK(untagged.json.find("combo") == std::string::npos);
    CHECK(storage::apply_action(*plain, untagged).accepted);
    CHECK(storage::apply_action(*tagged, with_tag).accepted);
  }
  const storage::Action cancel = storage::actions::from_game(GameCommand::cancel(1).tagged("c1", "iron_condor"));
  CHECK(cancel.json.find(R"("combo":"c1")") != std::string::npos);
  plain->advance(1);
  tagged->advance(1);
  CHECK(plain->snapshot().digest == tagged->snapshot().digest);
  CHECK(tagged->snapshot().game->options.positions.size() == 4);
}

// Playtest X1: the builder's per-leg lines left out what the web's WhatIfTable shows in the same panel:
// each leg's IV (the skew across a condor's strikes), the position's value and P&L, the IV-floor note.
TEST_CASE("builder: the compact what-if lines carry each leg's IV, the position's value and P&L, the floor note") {
  const Catalog catalog = builtin_catalog();
  const Texts t{catalog};
  WhatIfResult r;
  const WhatIfLeg put{"put",    true,   "1",     8,        100,      "97.5",   "0.55",  "0.53",
                      "0.668", false,  "-0.2037", "0.0621", "-0.0161", "0.0193", "0.53", "-0.02"};
  const WhatIfLeg call{"call",  false,  "1",      11,       100,      "102.5",  "1.16",   "1.21",
                       "0.05", true,   "0.3887", "0.0847", "-0.0217", "0.0262", "-1.21", "-0.05"};
  const WhatIfLeg perp{"perp", true, "1", 0, 0, std::nullopt, "101.11", "101.11", std::nullopt, false, "1", "0", "0", "0", "101.11", "0"};
  r.legs = {put, call, perp};
  r.net = {"-1.46", "-0.12", "0.007", "-0.0445", "0.01", "-0.01"};
  const std::vector<Line> lines = whatif_compact(t, r);
  REQUIRE(lines.size() == 5);
  CHECK(lines[0].text == "+1 P 97,50 \xC2\xB7 0,53 \xC2\xB7 IV 66,8% \xC2\xB7 \xCE\x94 -0,2037 \xC2\xB7 -0,02 cr");
  CHECK(lines[1].text.find("IV 5,0%*") != std::string::npos);
  CHECK(lines[2].text.find("IV") == std::string::npos);  // the perpetual has no volatility
  CHECK(lines[3].text.rfind("Posi\xC3\xA7\xC3\xA3o: -1,46 cr \xC2\xB7 P&L -0,12 cr \xC2\xB7 \xCE\x94 0,007", 0) == 0);
  CHECK(lines[3].tone == Tone::Down);
  CHECK(lines[4].text.rfind("* ", 0) == 0);
}
