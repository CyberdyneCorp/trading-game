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
  REQUIRE(v.templates.size() == 7);
  CHECK_FALSE(v.templates[0].enabled);  // bull call spread
  CHECK(v.templates[3].enabled);        // protective put
  CHECK(v.templates[5].enabled);        // long strangle
  CHECK_FALSE(v.templates[6].enabled);  // iron condor
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
