// Ports of the web's pure helper tests: shortcuts.ts, chartScale.ts, chartDrag.ts, chartA11y.ts,
// optionsMath.ts, points.ts, perks.ts, calibration.ts, rejections.ts and the feed lines.
#include <doctest/doctest.h>

#include "model/game_prefs.hpp"
#include "model/i18n.hpp"
#include "viewmodel/game/chart_scale.hpp"
#include "viewmodel/game/dialogs_vm.hpp"
#include "viewmodel/game/options_math.hpp"
#include "viewmodel/game/progress.hpp"
#include "viewmodel/game/shortcuts.hpp"
#include "viewmodel/game/texts.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;

TEST_CASE("shortcuts: web defaults, key labels, reserved keys and conflicts") {
  Keymap map;
  CHECK(map.key(Action::Advance) == "Space");
  CHECK(map.key(Action::Advance5) == "Shift+Space");
  CHECK(map.key(Action::Help) == "?");
  CHECK(*map.action_for("b") == Action::Buy);
  CHECK_FALSE(map.action_for("Enter"));
  CHECK(key_label("Shift+Space") == "Shift + Space");
  CHECK(key_label("b") == "B");
  CHECK(key_label("?") == "?");
  // Advance never shares Enter (submission), nor any reserved key, even with Shift.
  CHECK(map.bind(Action::Advance, "Enter").error == Keymap::BindError::Reserved);
  CHECK(map.bind(Action::Advance, "Shift+ArrowLeft").error == Keymap::BindError::Reserved);
  const auto conflict = map.bind(Action::Advance, "b");
  CHECK(conflict.error == Keymap::BindError::Conflict);
  CHECK(conflict.other == Action::Buy);
  CHECK(map.bind(Action::Advance, "n").error == Keymap::BindError::None);
  CHECK(*map.action_for("n") == Action::Advance);
}

TEST_CASE("shortcuts: a stored map is sanitized and survives the prefs file") {
  Keymap map;
  map.bind(Action::Journal, "k");
  GamePrefs p;
  p.keymap = map.to_map();
  const GamePrefs back = prefs_from_json(prefs_to_json(p));
  CHECK(Keymap::sanitized(back.keymap) == map);
  // Two actions on one key, or a reserved key: defaults.
  CHECK(Keymap::sanitized({{"advance", "b"}}) == Keymap{});
  CHECK(Keymap::sanitized({{"help", "Tab"}}) == Keymap{});
  CHECK(Keymap::sanitized({{"bogus", "z"}, {"mute", "z"}}).key(Action::Mute) == "z");
}

TEST_CASE("prefs: a damaged file reads as defaults; values round-trip") {
  CHECK(prefs_from_json("{nope").advisor_points == 3);
  GamePrefs p;
  p.advisor_points = 7;
  p.finished = {"m01-false-breakout"};
  p.tutorials_seen = {"m02-funding-squeeze"};
  p.settled_runs = {"r\"1"};
  const GamePrefs back = prefs_from_json(prefs_to_json(p));
  CHECK(back.advisor_points == 7);
  CHECK(back.finished == p.finished);
  CHECK(back.tutorials_seen == p.tutorials_seen);
  CHECK(back.settled_runs == p.settled_runs);
}

TEST_CASE("chart scale: near levels widen the range, far ones are pinned; tags never overlap") {
  const Range r = price_range(100, 110, {});
  CHECK(r.lo == doctest::Approx(99.2));
  CHECK(r.hi == doctest::Approx(110.8));
  const Range near = price_range(100, 110, {113});
  CHECK(near.hi > 113);
  const Range far = price_range(100, 110, {200});
  CHECK(far.hi < 120);
  CHECK(pin_of(200, far.lo, far.hi) == Pin::Above);
  CHECK(pin_of(50, far.lo, far.hi) == Pin::Below);
  const auto ys = spread_labels({100, 101, 102}, 0, 400);
  CHECK(ys[1] - ys[0] >= kLabelH);
  CHECK(ys[2] - ys[1] >= kLabelH);
  const auto squeezed = spread_labels({10, 10, 10, 10}, 0, 40);
  CHECK(squeezed[0] >= kLabelH / 2);
  CHECK(squeezed[3] <= 40 - kLabelH / 2);
  CHECK(hits_tag(105, {100}));
  CHECK_FALSE(hits_tag(130, {100}));
}

TEST_CASE("chart scale: thousands of bars merge to about one candle per pixel; continuity bars are skipped") {
  std::vector<GameBar> bars;
  for (int i = 0; i < 5000; ++i) bars.push_back({100.0 + i % 7, 103.0 + i % 7, 99.0, 101.0, double(i % 13), true, false});
  const auto merged = drawn_candles(bars, 0, 0.2, true);
  CHECK(merged.size() == 1000);
  CHECK(merged[0].o == doctest::Approx(100));
  CHECK(merged[0].h == doctest::Approx(107));
  CHECK(merged[0].v == doctest::Approx(4));
  std::vector<GameBar> gap = {{1, 2, 0.5, 1.5, 1, false, false}, {1.5, 1.5, 1.5, 1.5, 0, false, true}};
  CHECK(drawn_candles(gap, 0, 10, true).size() == 1);
  CHECK(drawn_candles(gap, 0, 10, false).size() == 2);
}

TEST_CASE("chart drag: hit test on the line or the axis tag; exits must stay on their side of the mark") {
  const std::vector<DragZone> zones = {{{true, true, 0}, 95, 200, 200}, {{false, false, 7}, 105, 100, 110}};
  CHECK(hit_test(zones, 50, 203, 600)->draft);
  CHECK(hit_test(zones, 650, 115, 600)->order_id == 7);
  CHECK_FALSE(hit_test(zones, 50, 150, 600));
  CHECK(exit_on_valid_side("sell", "stop_market", 95, 100));
  CHECK_FALSE(exit_on_valid_side("sell", "stop_market", 105, 100));
  CHECK(exit_on_valid_side("sell", "take_profit", 105, 100));
  CHECK(exit_on_valid_side("buy", "stop_market", 105, 100));
  CHECK(stop_moved_beyond_liquidation("sell", "stop_market", 90, 91.0));
  CHECK_FALSE(stop_moved_beyond_liquidation("sell", "stop_market", 92, 91.0));
  CHECK_FALSE(stop_moved_beyond_liquidation("sell", "take_profit", 90, 91.0));
}

TEST_CASE("chart a11y: pages end with the current bar; keyboard focus never leaves the closed bars") {
  const BarPage last = bar_page(120, std::nullopt);
  CHECK(last.start == 70);
  CHECK(last.end == 120);
  CHECK(last.pages == 3);
  const BarPage first = bar_page(120, 0);
  CHECK(first.start == 0);
  CHECK(first.end == 20);
  CHECK(*step_focus("ArrowLeft", std::nullopt, 10) == 8);
  CHECK(*step_focus("ArrowRight", 9, 10) == 9);
  CHECK(*step_focus("Home", 5, 10) == 0);
  CHECK(*step_focus("PageUp", 5, 10) == 0);
  CHECK_FALSE(step_focus("x", 5, 10));
  CHECK_FALSE(step_focus("End", std::nullopt, 0));
}

TEST_CASE("options math: payoff, breakeven, fee cap, write margin and series names") {
  const OptionLeg call{true, 100, true, 2, 3, 1};
  CHECK(payoff_at(call, 110) == doctest::Approx(14));
  CHECK(payoff_at(call, 90) == doctest::Approx(-6));
  CHECK(breakeven(call) == doctest::Approx(103));
  CHECK(*max_loss(call) == doctest::Approx(6));
  OptionLeg put = call;
  put.call = false;
  put.buy = false;
  CHECK_FALSE(max_loss(put));
  CHECK(breakeven(put) == doctest::Approx(97));
  // feeRate of the underlying notional, capped at 12.5% of the premium.
  CHECK(option_fee(call, 100, 0.0003) == doctest::Approx(0.06));
  CHECK(option_fee({true, 100, true, 2, 0.1, 1}, 100, 0.0003) == doctest::Approx(0.025));
  OptionQuote q;
  q.kind = "call";
  q.strike = 110;
  q.ask = 1;
  CHECK(write_margin(q, 100, 1, 1) == doctest::Approx(11));
  CHECK(series_label("call", 100, 96, Locale::PtBR) == "C 100 \xC2\xB7 exp 96");
  CHECK(series_label("put", 97.5, 85, Locale::PtBR) == "P 97,50 \xC2\xB7 exp 85");
  CHECK(series_label("put", 1250, 85, Locale::PtBR) == "P 1250 \xC2\xB7 exp 85");
}

TEST_CASE("points: balance - used + earned, capped, settled once per run") {
  GamePrefs p;
  CHECK(allowance_for(p) == 3);
  CHECK(settle_run(p, "run-1", 2, 4));
  CHECK(p.advisor_points == 5);
  CHECK_FALSE(settle_run(p, "run-1", 2, 4));
  CHECK(p.advisor_points == 5);
  settle_run(p, "run-2", 0, 20);
  CHECK(p.advisor_points == kMaxPoints);
  for (int i = 0; i < 150; ++i) settle_run(p, "r" + std::to_string(i), 0, 0);
  CHECK(p.settled_runs.size() == 100);
}

TEST_CASE("perks: unlocked by distinct finished missions; the longest wire wins") {
  const std::vector<std::string> built_in = {"m01", "m02", "m03", "m04"};
  CHECK(distinct_finished({"m01", "m01", "pack-x"}, built_in) == 1);
  CHECK(unlocked_perks(1) == std::vector<Perk>{Perk::Calendar});
  CHECK(unlocked_perks(4).size() == 3);
  const RunOptions o = perk_options({Perk::Calendar, Perk::Wire1, Perk::Wire2}, 3);
  CHECK(o.calendar);
  CHECK(o.news_lead == 2);
  CHECK(o.advice_allowance == 3);
  CHECK(perks_used(o) == std::vector<Perk>{Perk::Calendar, Perk::Wire2});
  CHECK(new_perks({Perk::Calendar, Perk::Wire1}, {"calendar"}) == std::vector<Perk>{Perk::Wire1});
}

TEST_CASE("calibration: an advance stops on the next question step") {
  GameState g;
  g.warmup_bars = 60;
  g.total_bars = 140;
  g.calibration.every = 15;
  g.calibration.horizon = 5;
  g.step = 60;
  CHECK(steps_until_next_question(g, 5) == 5);
  CHECK(steps_until_next_question(g, 50) == 15);
  g.step = 130;
  CHECK(steps_until_next_question(g, 5) == 5);  // the next one would end after the run
}

TEST_CASE("auto-advance: ticks at its speed and pauses on notable bars") {
  AutoPlay a;
  a.start(false);
  CHECK(a.running);
  CHECK_FALSE(a.tick(0.5));
  CHECK(a.tick(0.4));  // 2x = 0.8 s per bar
  GameState g;
  g.status = "active";
  g.step = 70;
  EngineEvent fill;
  fill.type = "fill";
  fill.step = 70;
  g.events.push_back(fill);
  a.after_advance(g);
  CHECK_FALSE(a.running);
  CHECK(*a.paused == PauseReason::Fill);
  a.start(false);
  a.pause_on_events = false;
  a.after_advance(g);
  CHECK(a.running);
  g.status = "finished";
  a.after_advance(g);
  CHECK(*a.paused == PauseReason::Finished);
}

TEST_CASE("texts: rejections quote their numbers; feed lines; objectives; news; interval") {
  Catalog c = builtin_catalog();
  const Texts t(c);
  RejectionFacts f;
  f.need = 12'000;
  f.available = 10'000;
  const std::string why = rejection_text(t, "insufficient_margin", f);
  CHECK(why.find("12.000,00 cr") != std::string::npos);
  CHECK(why.find("10.000,00 cr") != std::string::npos);
  f.need = 9'000;  // an estimate that fits does not explain the refusal: the plain reason
  CHECK(rejection_text(t, "insufficient_margin", f) == t.reason("insufficient_margin"));
  CHECK(rejection_text(t, "no_such_reason") == "no_such_reason");

  EngineEvent e;
  e.type = "fill";
  e.order_id = 3;
  e.side = "buy";
  e.qty = "1.50000000";
  e.price = "100.25000000";
  e.requested = "0";
  e.amount = "0.07500000";
  e.pnl = "0";
  SeriesNames names;
  const auto line = describe_event(t, e, names);
  REQUIRE(line);
  CHECK(line->text.find("1,5") != std::string::npos);
  CHECK(line->text.find("100,25") != std::string::npos);
  CHECK(line->tone == Tone::Info);

  CHECK(objective_text(t, {"risk_per_idea", 100}).find("1%") != std::string::npos);
  CHECK(bp_pct(250, Locale::PtBR) == "2,5%");
  News n;
  n.kind = "rate_decision";
  n.tone = -1;
  CHECK(news_headline(t, n) == "Banco Central de Varena sobe juros");
  CHECK(news_level(t, 2) == "muito positivo");
  CHECK(interval_label(3600) == "1h");
  CHECK(interval_label(7) == "7s");
  CHECK(vote_mark(2) == "\xE2\x96\xB2\xE2\x96\xB2");
  CHECK(t.count("rail.fundingIn", 1) == "em 1 barra");
  CHECK(t.count("rail.fundingIn", 3) == "em 3 barras");
}

TEST_CASE("dialogs: numeric lesson answers, plan commands, tutorial cards, remapping messages") {
  CHECK(*numeric_answer("0,5") == "0.50000000");
  CHECK_FALSE(numeric_answer("-1"));
  CHECK_FALSE(numeric_answer("abc"));
  Catalog c = builtin_catalog();
  const Texts t(c);
  const auto cards = tutorial_cards(t, "m01-false-breakout");
  REQUIRE(cards.size() >= 4);
  CHECK(cards[0].title == "Leia o gráfico");

  PlanDialog plan;
  plan.open(true, "16.6", 3, 50.0, "95,5", "", std::nullopt);
  CHECK_FALSE(plan.can_save());
  plan.thesis = "  rompe com volume  ";
  CHECK(plan.can_save());
  const PlanEntry e = plan.entry(70, false);
  CHECK(e.thesis == "rompe com volume");
  CHECK(*e.invalidation == doctest::Approx(95.5));
  const GameCommand cmd = plan.command(e);
  CHECK(cmd.stop == "95.50");
  CHECK(cmd.tp.empty());
  CHECK(cmd.amount == "50.00");
  CHECK(cmd.number == 60);
  CHECK(plan.headline(t).find("LONG 16,6") != std::string::npos);

  Keymap map;
  Remap r;
  r.start(Action::Advance);
  CHECK(r.key(t, map, "b"));
  CHECK_FALSE(r.message_ok);
  CHECK(r.message.find("B") != std::string::npos);
  r.start(Action::Advance);
  r.key(t, map, "n");
  CHECK(r.message_ok);
  CHECK(map.key(Action::Advance) == "n");
}
