// GameViewModel with a fake engine: the command pipeline (one at a time, plan before an entry, queued
// advances), notices with numbers, dialogs, keyboard, chart drags, protection, options, advisor, perks
// and points. Values typed as a PT-BR player would (decimal comma).
#include <algorithm>
#include <doctest/doctest.h>

#include <filesystem>

#include "support/fake_engine.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/game_viewmodel.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;
using testing::base_state;
using testing::FakeEngine;
using testing::game_result;
using testing::snapshot_of;

namespace {

struct Fixture {
  Catalog catalog = builtin_catalog();
  FakeEngine engine;
  GameViewModel vm{catalog, engine, nullptr, {"m01-false-breakout", "m02-funding-squeeze"}};
  GameState g = base_state();

  Fixture() {
    CommandResult started{CommandKind::Start, true, "none"};
    vm.on_snapshot(*snapshot_of(g, started));
  }
  void answer(GameCommandKind kind, bool ok = true, std::string reason = "none", int id = 0) {
    vm.on_snapshot(*snapshot_of(g, game_result(kind, ok, std::move(reason), id)));
  }
  void advanced() { vm.on_snapshot(*snapshot_of(g, CommandResult{CommandKind::Advance, true, "none"})); }
  KeyPress key(std::string name) const { return {std::move(name), false, false, false}; }
};

GameState with_long(double qty, double entry, int leverage, std::optional<double> liq) {
  GameState g = base_state();
  g.position.qty = qty;
  g.position.qty_text = qty == 1 ? "1.00000000" : "2.00000000";
  g.position.entry = entry;
  g.position.margin = qty * entry / leverage;
  g.leverage = leverage;
  g.account.liquidation = liq;
  g.totals.fills = 1;
  return g;
}

}  // namespace

TEST_CASE("game vm: an entry asks for its plan, then plan and order go one at a time; advances wait") {
  Fixture f;
  f.vm.ticket.size_by = SizeBy::Qty;
  f.vm.ticket.qty = "2,5";
  f.vm.ticket.stop = "95,5";
  f.vm.submit();
  REQUIRE(f.vm.modal() == Modal::Plan);
  CHECK(f.engine.commands.empty());
  CHECK(f.vm.plan.invalidation == "95.50");  // the plan starts from the order's stop (web: draft.stop)
  f.vm.plan_confirm(false);                 // no thesis yet: not saved
  CHECK(f.vm.modal() == Modal::Plan);
  f.vm.plan.thesis = "rompimento com volume";
  f.vm.plan_confirm(false);
  CHECK(f.vm.modal() == Modal::None);
  REQUIRE(f.engine.log.size() == 1);
  CHECK(f.engine.log.back().rfind("plan inv 95.50 risk ", 0) == 0);
  f.vm.advance(1);  // pressed while the sequence runs: queued, never between plan and order
  CHECK(f.engine.log.size() == 1);
  f.answer(GameCommandKind::Plan);
  CHECK(f.engine.log.back() == "order 0 buy 2.5 sl 95.50");
  f.answer(GameCommandKind::Order, true, "none", 7);
  CHECK(f.vm.notice().text == "#7 \xE2\x9C\x93 compra 2,5");
  CHECK(f.vm.notice().tone == Tone::Ok);
  CHECK(f.engine.log.back() == "advance 1");
  REQUIRE(f.vm.journal().size() == 1);
  CHECK(f.vm.journal()[0].thesis == "rompimento com volume");
}

TEST_CASE("game vm: a refused leverage change stops the order and quotes the margin it needed") {
  Fixture f;
  f.g = with_long(2, 100, 5, std::nullopt);  // margin 40 at 5x; 1x needs 200
  f.g.account.available = 50;
  f.advanced();
  f.vm.ticket.size_by = SizeBy::Qty;
  f.vm.ticket.qty = "1";
  f.vm.ticket.side = game::Side::Sell;  // reduces: no plan needed
  f.vm.ticket.leverage = "1";
  f.vm.submit();
  CHECK(f.engine.log.back() == "leverage 1");
  f.answer(GameCommandKind::Leverage, false, "insufficient_margin");
  CHECK(f.engine.log.back() == "leverage 1");  // the order is not sent
  CHECK(f.vm.notice().tone == Tone::Error);
  CHECK(f.vm.notice().text.find("cr") != std::string::npos);
  CHECK_FALSE(f.vm.busy());
}

TEST_CASE("game vm: an order refused by the engine explains the margin with its numbers") {
  Fixture f;
  f.g.account.available = 10'000;
  f.vm.ticket.size_by = SizeBy::Qty;
  f.vm.ticket.qty = "50";
  f.vm.submit();
  f.vm.plan_confirm(true);
  f.answer(GameCommandKind::Plan);
  f.g.account.available = 1'000;  // what the account had when the refusal came
  f.answer(GameCommandKind::Order, false, "insufficient_margin");
  CHECK(f.vm.notice().text.find("1.000,00 cr") != std::string::npos);
  CHECK(f.vm.notice().text.find("5.00") != std::string::npos);  // ~5,0xx cr needed
}

TEST_CASE("game vm: a due calibration question blocks advancing until answered") {
  Fixture f;
  f.g.calibration.due = true;
  f.advanced();
  CHECK(f.vm.modal() == Modal::Calibration);
  const auto sent = f.engine.log.size();
  f.vm.advance(1);
  CHECK(f.vm.handle_key(f.key("Space")) == false);
  CHECK(f.engine.log.size() == sent);
  f.vm.calibration.pct = 65;
  f.vm.calibration_answer(false);
  CHECK(f.engine.log.back() == "answer 6500");
  f.g.calibration.due = false;
  f.answer(GameCommandKind::Answer);
  CHECK(f.vm.modal() == Modal::None);
  // An advance never runs past the next question: from bar 60, 50 bars stop at 75.
  f.vm.advance(50);
  CHECK(f.engine.log.back() == "advance 15");
}

TEST_CASE("game vm: shortcuts follow the key map; typing and modals keep their keys") {
  Fixture f;
  CHECK(f.vm.handle_key(f.key("Space")));
  CHECK(f.engine.log.back() == "advance 1");
  f.advanced();
  CHECK(f.vm.handle_key(f.key("Shift+Space")));
  CHECK(f.engine.log.back() == "advance 5");
  f.advanced();
  CHECK_FALSE(f.vm.handle_key({"Space", true, false, false}));  // typing in a field
  CHECK_FALSE(f.vm.handle_key({"b", false, true, false}));      // Ctrl+B is not B
  CHECK(f.vm.handle_key(f.key("s")));
  CHECK(f.vm.ticket.side == game::Side::Sell);
  CHECK(f.vm.handle_key(f.key("?")));
  CHECK(f.vm.modal() == Modal::Help);
  const auto sent = f.engine.log.size();
  CHECK(f.vm.handle_key(f.key("Space")));  // swallowed by the help overlay
  CHECK(f.engine.log.size() == sent);
  CHECK(f.vm.handle_key(f.key("Escape")));
  CHECK(f.vm.modal() == Modal::None);
  // Remap advance to N: Space is then free, N advances.
  f.vm.remap.start(Action::Advance);
  CHECK(f.vm.handle_key(f.key("n")));
  CHECK(f.vm.keymap().key(Action::Advance) == "n");
  CHECK(f.vm.handle_key(f.key("n")));
  CHECK(f.engine.log.back() == "advance 1");
  f.advanced();
  CHECK_FALSE(f.vm.handle_key(f.key("Space")));
  f.vm.reset_keymap();
  CHECK(f.vm.keymap().key(Action::Advance) == "Space");
}

TEST_CASE("game vm: dragging SL? on the chart writes the ticket stop in the locale") {
  Fixture f;
  f.vm.ticket.stop = "99,4";
  const ChartScene scene = f.vm.chart_scene(800, 400);
  REQUIRE_FALSE(scene.empty);
  const auto& zones = f.vm.chart.frame().zones;
  const auto sl = std::find_if(zones.begin(), zones.end(), [](const DragZone& z) { return z.target.draft && z.target.stop; });
  REQUIRE(sl != zones.end());
  f.vm.chart.pointer_down(100, sl->line_y, false);
  REQUIRE(f.vm.chart.dragging());
  f.vm.chart_action(f.vm.chart.pointer_move(*f.vm.state(), 100, sl->line_y + 20, false));
  const ChartAction up = f.vm.chart.pointer_up();
  CHECK(up.kind == ChartAction::Kind::DraftStop);
  f.vm.chart_action(up);
  CHECK(f.vm.ticket.stop.find(',') != std::string::npos);
  CHECK(fmt::parse_decimal(f.vm.ticket.stop) < 99.4);
  CHECK(f.engine.commands.empty());  // a draft line never sends anything
}

TEST_CASE("game vm: moving a working stop past liquidation asks first; the wrong side is refused") {
  Fixture f;
  f.g = with_long(1, 100, 5, 80.5);
  Order stop;
  stop.id = 4;
  stop.type = "stop_market";
  stop.side = "sell";
  stop.qty = 1;
  stop.price = 95;
  stop.price_text = "95.00000000";
  stop.reduce_only = true;
  f.g.orders.push_back(stop);
  f.advanced();
  f.vm.chart_action({ChartAction::Kind::MoveOrder, 80.0, 4, {}});
  REQUIRE(f.vm.modal() == Modal::ConfirmMove);
  CHECK(f.engine.commands.empty());
  f.vm.confirm_move(true);
  CHECK(f.engine.log.back() == "move 4 80.00");
  f.answer(GameCommandKind::Move);
  CHECK(f.vm.notice().text.find("80,00") != std::string::npos);
  f.vm.chart_action({ChartAction::Kind::MoveOrder, 120.0, 4, {}});  // above the mark: would trigger at once
  CHECK(f.vm.notice().tone == Tone::Error);
  CHECK(f.engine.log.back() == "move 4 80.00");
}

TEST_CASE("game vm: protecting an open long sends a stop, a target and a 2% trailing stop") {
  Fixture f;
  f.g = with_long(1, 100, 1, std::nullopt);
  f.advanced();
  const TicketView v0 = f.vm.ticket.view(f.vm.texts(), *f.vm.state(), false);
  CHECK(v0.protect_visible);
  CHECK_FALSE(v0.unprotected.empty());
  f.vm.ticket.prot_stop = "97,25";
  f.vm.ticket.prot_tp = "108";
  f.vm.ticket.trail_by_pct = true;
  f.vm.ticket.prot_trail_pct = "2";
  f.vm.protect();
  CHECK(f.engine.log.back() == "order 2 sell 1.00000000 @97.25 ro");
  f.answer(GameCommandKind::Order, true, "none", 5);
  CHECK(f.engine.log.back() == "order 3 sell 1.00000000 @108.00 ro");
  f.answer(GameCommandKind::Order, true, "none", 6);
  CHECK(f.engine.log.back() == "trail-rate sell 1.00000000 0.020000");
  CHECK(f.vm.ticket.prot_stop.empty());
}

TEST_CASE("game vm: buying a call asks for its plan with the option label, then sends the option order") {
  Fixture f;
  f.g.options.enabled = true;
  f.g.options.fee_rate = 0.0003;
  OptionQuote q;
  q.id = 3;
  q.kind = "call";
  q.strike = 100;
  q.expiry_step = 96;
  q.bid = 2.4;
  q.ask = 2.6;
  f.g.options.chain.push_back(q);
  f.advanced();
  f.vm.pick_option(3, true);
  CHECK(f.vm.tab() == Tab::Options);
  const OptionTicketView v = f.vm.option_ticket.view(f.vm.texts(), *f.vm.state(), false);
  CHECK(v.series == "C 100 \xC2\xB7 exp 96");
  CHECK(*v.max_loss == "2,60 cr");
  CHECK(v.can_submit);
  f.vm.submit_option();
  REQUIRE(f.vm.modal() == Modal::Plan);
  CHECK(f.vm.plan.option());
  f.vm.plan_confirm(true);
  f.answer(GameCommandKind::Plan);
  CHECK(f.engine.log.back() == "option 3 buy 1.0");
  f.answer(GameCommandKind::Option, true, "none", 9);
  CHECK(f.vm.notice().text == "#9 \xE2\x9C\x93 compra C 100 \xC2\xB7 exp 96 \xC3\x97 1,0");
  // A market sell with no buyer, or a settling series, is refused before it is sent.
  f.g.options.chain[0].bid = 0;
  f.advanced();
  f.vm.option_ticket.buy = false;
  CHECK_FALSE(f.vm.option_ticket.view(f.vm.texts(), *f.vm.state(), false).no_bid.empty());
  f.g.options.chain[0].settling = true;
  f.advanced();
  CHECK_FALSE(f.vm.option_ticket.view(f.vm.texts(), *f.vm.state(), false).settling.empty());
}

TEST_CASE("game vm: the advisor's read fills the ticket (never submits) and asking uses a point") {
  Fixture f;
  f.vm.ask_advice();
  CHECK(f.engine.log.back() == "advice");
  Advice a;
  a.step = 60;
  a.stance = "short";
  a.confidence_bp = 6200;
  a.stop = "102.50000000";
  a.size = "12.30000000";
  f.g.advisor.advice.push_back(a);
  f.g.advisor.left = 2;
  f.answer(GameCommandKind::Advice);
  const AdvisorView v = advisor_view(f.vm.texts(), *f.vm.state(), false);
  CHECK(v.points.find('2') != std::string::npos);
  CHECK(v.actionable);
  CHECK_FALSE(v.can_ask);  // one read per bar
  const auto sent = f.engine.log.size();
  f.vm.apply_advice();
  CHECK(f.vm.ticket.side == game::Side::Sell);
  CHECK(f.vm.ticket.stop == "102,50");
  CHECK(f.vm.ticket.qty == "12,3");
  CHECK(f.vm.ticket.size_by == SizeBy::Qty);
  CHECK(f.engine.log.size() == sent);
}

TEST_CASE("game vm: points settle once at the end; finished missions unlock perks for the next run") {
  const auto dir = std::filesystem::temp_directory_path() / "tradelab-game-vm-prefs";
  std::filesystem::remove_all(dir);
  PrefsFile file((dir / "prefs.json").string());
  Catalog catalog = builtin_catalog();
  FakeEngine engine;
  GameViewModel vm(catalog, engine, &file, {"m01-false-breakout", "m02-funding-squeeze"});
  CHECK(vm.options_for_new_run().advice_allowance == 3);
  CHECK_FALSE(vm.options_for_new_run().calendar);
  GameState g = base_state();
  vm.on_snapshot(*snapshot_of(g, CommandResult{CommandKind::Start, true, "none"}));
  g.advisor.left = 1;  // two reads used
  g.status = "finished";
  auto end = snapshot_of(g, CommandResult{CommandKind::Advance, true, "none"});
  auto with_points = std::make_shared<Snapshot>(*end);
  with_points->debrief->advisor_points_earned = 4;
  vm.on_snapshot(*with_points);
  vm.on_snapshot(*with_points);  // a second delivery settles nothing more
  CHECK(vm.prefs().advisor_points == 5);
  const GamePrefs saved = file.load();
  CHECK(saved.advisor_points == 5);
  CHECK(saved.finished == std::vector<std::string>{"m01-false-breakout"});
  const RunOptions next = vm.options_for_new_run();
  CHECK(next.advice_allowance == 5);
  CHECK(next.calendar);
  std::filesystem::remove_all(dir);
}

TEST_CASE("game vm: an intel perk switched before the first action restarts the same market") {
  const auto dir = std::filesystem::temp_directory_path() / "tradelab-game-vm-perks";
  std::filesystem::remove_all(dir);
  PrefsFile file((dir / "prefs.json").string());
  GamePrefs p;
  p.finished = {"m01-false-breakout"};
  file.save(p);
  Catalog catalog = builtin_catalog();
  FakeEngine engine;
  GameViewModel vm(catalog, engine, &file, {"m01-false-breakout"});
  vm.options_for_new_run();
  vm.on_snapshot(*snapshot_of(base_state(), CommandResult{CommandKind::Start, true, "none"}, 77));
  CHECK(vm.perks_editable());
  CHECK(vm.perk_rows()[0].enabled);
  CHECK_FALSE(vm.perk_rows()[1].unlocked);
  vm.set_perk(Perk::Calendar, false);
  CHECK(engine.log.back() == "start m01-false-breakout 77 opts 3/0/-");
  vm.on_snapshot(*snapshot_of(base_state(), CommandResult{CommandKind::Start, true, "none"}, 77));
  CHECK_FALSE(vm.perk_rows()[0].enabled);
  vm.advance(1);
  CHECK_FALSE(vm.perks_editable());  // after the first action the run's options are fixed
  std::filesystem::remove_all(dir);
}

TEST_CASE("game vm: lesson questions: a choice, an invalid number flagged, a valid one sent once") {
  Fixture f;
  f.g.lesson = {"m01-false-breakout", true, {{"breakout-plan", false, 3, 1}, {"risk-two-percent", true, 0, 1}}};
  f.advanced();
  f.vm.open_lessons();
  const auto qs = f.vm.lessons.questions(f.vm.texts(), f.vm.state()->lesson);
  REQUIRE(qs.size() == 2);
  CHECK(qs[0].choices.size() == 3);
  f.vm.lessons.choose(0, 1);
  f.vm.lesson_answer(0);
  CHECK(f.engine.log.back() == "lesson 0 1 ");
  f.answer(GameCommandKind::Lesson);
  f.vm.lessons.type(1, "duzentos");
  f.vm.lesson_answer(1);
  CHECK(f.vm.lessons.questions(f.vm.texts(), f.vm.state()->lesson)[1].invalid);
  f.vm.lessons.type(1, "200,5");
  f.vm.lesson_answer(1);
  CHECK(f.engine.log.back() == "lesson 1 0 200.50000000");
  f.answer(GameCommandKind::Lesson);
  CHECK(f.vm.lessons.questions(f.vm.texts(), f.vm.state()->lesson)[1].answered);
  CHECK(f.vm.perks_editable());  // lesson answers do not fix the run's options
}

TEST_CASE("game vm: auto-advance plays one bar per tick and pauses on a fill") {
  Fixture f;
  CHECK(f.vm.handle_key(f.key("a")));
  CHECK(f.vm.autoplay.running);
  f.vm.tick(1.0);
  CHECK(f.engine.log.back() == "advance 1");
  EngineEvent fill;
  fill.type = "fill";
  fill.step = 61;
  f.g.step = 61;
  f.g.events.push_back(fill);
  f.advanced();
  CHECK_FALSE(f.vm.autoplay.running);
  CHECK(*f.vm.autoplay.paused == PauseReason::Fill);
}

TEST_CASE("game vm: tutorial cards step through and are remembered as seen") {
  Fixture f;
  f.vm.open_tutorial();
  REQUIRE(f.vm.modal() == Modal::Tutorial);
  const auto cards = tutorial_cards(f.vm.texts(), "m01-false-breakout");
  for (std::size_t i = 0; i < cards.size(); ++i) f.vm.tutorial_step(1);
  CHECK(f.vm.modal() == Modal::None);
  CHECK(f.vm.prefs().tutorials_seen == std::vector<std::string>{"m01-false-breakout"});
}

TEST_CASE("game vm: the run's end reveals its regimes on the chart; a new run starts without them") {
  Fixture f;
  CHECK(f.vm.chart.regimes().empty());
  f.g.status = "finished";
  Snapshot ended = *snapshot_of(f.g, CommandResult{CommandKind::Advance, true, "none"});
  ended.debrief = Debrief{};
  ended.debrief->reveal = *json::parse(R"({"spans":[{"label":"trend","direction":-1,"start":10,"bars":30}]})");
  f.vm.on_snapshot(ended);
  REQUIRE(f.vm.chart.regimes().size() == 1);
  CHECK(f.vm.chart.regimes()[0].label == "trend");
  CHECK(f.vm.chart.regimes()[0].direction == -1);
  f.g.status = "active";
  f.vm.on_snapshot(*snapshot_of(f.g, CommandResult{CommandKind::Start, true, "none"}));
  CHECK(f.vm.chart.regimes().empty());
}
