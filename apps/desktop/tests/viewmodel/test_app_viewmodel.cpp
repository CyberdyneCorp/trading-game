#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "model/engine_host.hpp"
#include "support/ui_queue.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using tradelab::desktop::testing::UiQueue;

namespace {

// Records commands; the test answers with hand-made snapshots.
struct FakeEngine final : EngineCommands {
  std::vector<std::string> log;
  void start(const std::string& mission, std::uint32_t seed) override {
    log.push_back("start " + mission + " " + std::to_string(seed));
  }
  void advance(int steps) override { log.push_back("advance " + std::to_string(steps)); }
  void market(Side side, const std::string& qty) override {
    log.push_back(std::string(side == Side::Buy ? "buy " : "sell ") + qty);
  }
};

std::vector<MissionInfo> two_missions() {
  return {{"m01-false-breakout", 140, 60, 5, true}, {"m06-thin-book", 170, 60, 2, false}};
}

std::shared_ptr<const Snapshot> snap(CommandKind kind, bool accepted = true, std::string reason = "none",
                                     std::string status = "active") {
  auto s = std::make_shared<Snapshot>();
  s->mission = "m01-false-breakout";
  s->alias = "K2A";
  s->status = std::move(status);
  s->seed = 42;
  s->step = 60;
  s->warmup_bars = 60;
  s->last_decision_step = 138;
  s->initial_balance = "10000.00000000";
  s->leverage_cap = 5;
  s->lot_size = "0.10000000";
  s->tick_size = "0.01000000";
  s->equity = "10000.00000000";
  s->available = "10000.00000000";
  s->position_qty = "0.00000000";
  s->position_entry = "0.00000000";
  s->bars = {Bar{100, 101, 99, 100.5, 10}};
  s->results.push_back(CommandResult{kind, accepted, std::move(reason)});
  if (s->status != "active") s->debrief = Debrief{"9998.98228227", "-1.01771773", "-85.79694750", 47, "risk", 1};
  return s;
}

std::string text_of(const AppViewModel& vm, std::string_view widget) {
  for (const auto& b : vm.bindings()) {
    if (b.widget == widget) return b.text;
  }
  return "<missing " + std::string(widget) + ">";
}

struct Fixture {
  FakeEngine engine;
  AppViewModel vm{builtin_catalog(), two_missions(), engine, [] { return 42u; }};

  void to_game() {
    vm.start_selected();
    vm.on_snapshot(snap(CommandKind::Start));
    vm.open_terminal();
  }
};

}  // namespace

TEST_CASE("app vm: menu lists the missions with web texts in PT-BR") {
  Fixture f;
  CHECK(f.vm.screen() == Screen::Menu);
  const auto titles = f.vm.mission_titles();
  REQUIRE(titles.size() == 2);
  CHECK(titles[0] == "Missão 01 · Range");
  CHECK(text_of(f.vm, names::kMenuStart) == "Iniciar missão");
  CHECK(text_of(f.vm, names::kMenuDetailTags) == "Alavancagem máx. 5x · 80 barras · SINTÉTICO");
  f.vm.select(1);
  CHECK(text_of(f.vm, names::kMenuDetailTags) == "Alavancagem máx. 2x · 110 barras · SINTÉTICO");
  f.vm.select(7);  // out of range: ignored
  CHECK(f.vm.selected() == 1);
}

TEST_CASE("app vm: start -> briefing -> terminal -> debrief -> menu") {
  Fixture f;
  f.vm.start_selected();
  CHECK(f.engine.log == std::vector<std::string>{"start m01-false-breakout 42"});
  CHECK(f.vm.waiting());
  CHECK(f.vm.screen() == Screen::Menu);  // stays until the engine acknowledges
  f.vm.start_selected();                 // a second click while waiting sends nothing
  CHECK(f.engine.log.size() == 1);

  f.vm.on_snapshot(snap(CommandKind::Start));
  CHECK(f.vm.screen() == Screen::Briefing);
  CHECK_FALSE(f.vm.waiting());
  CHECK(text_of(f.vm, names::kBriefCapital) == "Capital virtual: 10.000,00 cr");
  CHECK(text_of(f.vm, names::kBriefLeverage) == "Alavancagem permitida: 5x");
  CHECK(text_of(f.vm, names::kBriefMode).find("semente 42") != std::string::npos);

  f.vm.open_terminal();
  CHECK(f.vm.screen() == Screen::Game);
  CHECK(text_of(f.vm, names::kGameTurn) == "Barra 1 de 79");
  CHECK(text_of(f.vm, names::kGameAccount) ==
        "Patrimônio 10.000,00 cr · Disponível 10.000,00 cr · Posição: Sem posição");

  f.vm.advance();
  CHECK(f.engine.log.back() == "advance 1");
  f.vm.on_snapshot(snap(CommandKind::Advance, true, "none", "finished"));
  CHECK(f.vm.screen() == Screen::Debrief);
  CHECK(text_of(f.vm, names::kDebriefTitle) == "Missão concluída");
  CHECK(text_of(f.vm, names::kDebriefPnl) == "\xE2\x96\xBC -1,02 cr");
  CHECK(text_of(f.vm, names::kDebriefScore) == "Nota de processo: 47/100");
  CHECK(text_of(f.vm, names::kDebriefHold) == "Comprar e segurar (1x): -85,80 cr");

  f.vm.back_to_menu();
  CHECK(f.vm.screen() == Screen::Menu);
}

TEST_CASE("app vm: play again starts the same mission on a new market") {
  Fixture f;
  f.to_game();
  f.vm.advance();
  f.vm.on_snapshot(snap(CommandKind::Advance, true, "none", "bankrupt"));
  REQUIRE(f.vm.screen() == Screen::Debrief);
  CHECK(text_of(f.vm, names::kDebriefTitle) == "GAME OVER");
  f.vm.play_again();
  CHECK(f.engine.log.back() == "start m01-false-breakout 42");
  f.vm.on_snapshot(snap(CommandKind::Start));
  CHECK(f.vm.screen() == Screen::Briefing);
}

TEST_CASE("app vm: comma input - '1,5' in PT-BR sends the engine decimal 1.5") {
  Fixture f;
  f.to_game();
  f.vm.set_qty_text("1,5");
  f.vm.buy();
  CHECK(f.engine.log.back() == "buy 1.5");
  f.vm.on_snapshot(snap(CommandKind::Market));
  f.vm.set_qty_text("0.27");  // floored to the 0.1 lot
  f.vm.sell();
  CHECK(f.engine.log.back() == "sell 0.2");
}

TEST_CASE("app vm: an invalid quantity is explained and nothing is sent") {
  Fixture f;
  f.to_game();
  const auto sent = f.engine.log.size();
  f.vm.set_qty_text("abc");
  f.vm.buy();
  CHECK(f.engine.log.size() == sent);
  CHECK(text_of(f.vm, names::kGameNotice) == "Digite uma quantidade válida (ex.: 1,5).");
  f.vm.set_qty_text("0,05");  // below one lot
  f.vm.buy();
  CHECK(f.engine.log.size() == sent);
}

TEST_CASE("app vm: an engine rejection shows its plain-language reason") {
  Fixture f;
  f.to_game();
  f.vm.buy();
  f.vm.on_snapshot(snap(CommandKind::Market, false, "insufficient_margin"));
  CHECK(text_of(f.vm, names::kGameNotice) == "\xE2\x9C\x96 margem insuficiente");
  f.vm.buy();
  f.vm.on_snapshot(snap(CommandKind::Market, false, "some_new_code"));
  CHECK(text_of(f.vm, names::kGameNotice) == "\xE2\x9C\x96 some_new_code");  // unknown code shown as is
}

TEST_CASE("app vm: Space advances one bar, Shift+Space five; ignored while typing or off the terminal") {
  Fixture f;
  CHECK_FALSE(f.vm.handle_key(Key::Space, false));  // menu
  f.to_game();
  CHECK_FALSE(f.vm.handle_key(Key::Space, true));  // typing in the quantity field
  CHECK(f.engine.log.back() != "advance 1");
  CHECK(f.vm.handle_key(Key::Space, false));
  CHECK(f.engine.log.back() == "advance 1");
  f.vm.on_snapshot(snap(CommandKind::Advance));
  CHECK(f.vm.handle_key(Key::ShiftSpace, false));
  CHECK(f.engine.log.back() == "advance 5");
  CHECK_FALSE(f.vm.handle_key(Key::Other, false));
}

TEST_CASE("app vm: switching to en re-renders every text, numbers included") {
  Fixture f;
  f.to_game();
  f.vm.toggle_locale();
  CHECK(f.vm.locale() == Locale::En);
  CHECK(text_of(f.vm, names::kGameTurn) == "Bar 1 of 79");
  CHECK(text_of(f.vm, names::kGameAccount).find("10,000.00 cr") != std::string::npos);
  CHECK(f.vm.mission_titles()[0] == "Mission 01 · Range");
}

TEST_CASE("app vm: with the real engine host, a short mission flow through the UI queue") {
  UiQueue ui;
  std::unique_ptr<AppViewModel> vm;
  EngineHost host([&](EngineHost::Task t) { ui.post(std::move(t)); },
                  [&](std::shared_ptr<const Snapshot> s) { vm->on_snapshot(std::move(s)); });
  vm = std::make_unique<AppViewModel>(builtin_catalog(), host.missions(), host, [] { return 5u; });
  vm->start_selected();
  REQUIRE(ui.pump_until([&] { return vm->screen() == Screen::Briefing; }));
  vm->open_terminal();
  vm->buy();
  REQUIRE(ui.pump_until([&] { return !vm->waiting(); }));
  vm->advance();  // a market order fills at the next bar's open
  REQUIRE(ui.pump_until([&] { return !vm->waiting(); }));
  CHECK(text_of(*vm, names::kGameAccount).find("COMPRADO 1,0") != std::string::npos);
  while (vm->screen() == Screen::Game) {
    vm->advance(5);
    REQUIRE(ui.pump_until([&] { return !vm->waiting(); }));
  }
  CHECK(vm->screen() == Screen::Debrief);
  CHECK(text_of(*vm, names::kDebriefScore).find("/100") != std::string::npos);
}
