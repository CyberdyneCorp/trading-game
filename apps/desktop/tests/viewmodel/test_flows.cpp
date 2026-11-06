// AppViewModel flows of this change, with a fake engine: the tutorial before the briefing (first time
// only), assisted practice (rewind / another market: flagged, excluded from progress, points and the
// campaign), runs saved in an earlier launch (campaign and pack runs), and the debrief's chart, candle
// table and lesson results with the question wording.
#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "model/progress_json.hpp"
#include "support/fake_engine.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using testing::base_state;
using testing::FakeEngine;
using testing::snapshot_of;

namespace {

std::vector<MissionInfo> missions() {
  return {{"m01-false-breakout", 140, 60, 5, true}, {"m02-trend", 140, 60, 5, true}};
}

std::shared_ptr<const Snapshot> stamped(const GameState& g, CommandResult r, const std::string& run_id, bool assisted = false) {
  auto s = std::make_shared<Snapshot>(*snapshot_of(g, std::move(r)));
  s->run_id = run_id;
  s->assisted = assisted;
  return s;
}

// A debrief with lesson answers (one wrong choice, one right number) and two trades.
std::shared_ptr<const Snapshot> debrief_snapshot(const GameState& active, const std::string& run_id, bool assisted = false) {
  GameState g = active;
  g.status = "finished";
  auto s = std::make_shared<Snapshot>(*snapshot_of(g, CommandResult{CommandKind::Advance, true, "none"}));
  s->run_id = run_id;
  s->assisted = assisted;
  s->debrief->advisor_points_earned = 2;
  s->debrief->raw = *json::parse(R"({"advisorPointsEarned":2,
    "lessons":{"points":1,"total":2,"reviewed":true,"questions":[
      {"id":"breakout-plan","kind":"choice","answered":true,"skipped":false,"correct":false,"given":0,"answer":1},
      {"id":"risk-two-percent","kind":"numeric","answered":true,"skipped":false,"correct":true,"given":"200.00000000","answer":"200.00000000"}]},
    "trades":[{"entryStep":70,"entryAvg":"100.5","exitStep":80,"exitAvg":"101.5","net":"10"},
              {"entryStep":90,"entryAvg":"101.0","exitStep":null,"exitAvg":null,"net":"-3"}]})");
  return s;
}

struct Fixture {
  FakeEngine engine;
  AppViewModel vm{builtin_catalog(), missions(), engine, [] { return 42u; }};
  GameState g = base_state(70);

  void start(const std::string& run_id = "run-1") {
    vm.start_selected();
    vm.on_snapshot(stamped(g, CommandResult{CommandKind::Start, true, "none"}, run_id));
  }
  void to_game(const std::string& run_id = "run-1") {
    start(run_id);
    if (vm.screen() == Screen::Tutorial) vm.tutorial_finish();
    vm.open_terminal();
    REQUIRE(vm.screen() == Screen::Game);
  }
};

}  // namespace

// ---- Tutorial ----

TEST_CASE("tutorial: shown before the briefing the first time a mission starts, then never again") {
  Fixture f;
  f.start();
  REQUIRE(f.vm.screen() == Screen::Tutorial);
  ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kTutTitle) == "Leia o gráfico");
  CHECK(m.text_of(names::kTutStep).starts_with("1 de "));
  CHECK(m.text_of(names::kTutHead).find("Missão 01") != std::string::npos);
  CHECK_FALSE(m.is_enabled(names::kTutPrev));
  f.vm.tutorial_step(1);
  CHECK(f.vm.screen_model().text_of(names::kTutTitle) == "Long ou short");
  f.vm.tutorial_step(-1);
  CHECK(f.vm.tutorial_index() == 0);
  for (int i = 0; i < 20 && f.vm.screen() == Screen::Tutorial; ++i) {
    if (f.vm.screen_model().text_of(names::kTutNext) == "Ir para o briefing \xE2\x96\xB6") {
      f.vm.tutorial_step(1);
      break;
    }
    f.vm.tutorial_step(1);
  }
  CHECK(f.vm.screen() == Screen::Briefing);
  CHECK(f.vm.game().tutorial_seen("m01-false-breakout"));
  f.vm.back_to_menu();
  f.start("run-2");
  CHECK(f.vm.screen() == Screen::Briefing);  // seen on this device
  // The briefing can replay it.
  CHECK(f.vm.screen_model().is_visible(names::kBriefTutorial, false));
  f.vm.show_tutorial();
  CHECK(f.vm.screen() == Screen::Tutorial);
  f.vm.tutorial_finish();  // Skip / Esc
  CHECK(f.vm.screen() == Screen::Briefing);
}

TEST_CASE("tutorial: a pack run has none and goes straight to its briefing") {
  Fixture f;
  f.vm.on_pack_event(packs::PackEvent{packs::PackEvent::Kind::Library, {storage::StoredPack{"abc", "csv-1", "BTC · 1h"}}});
  f.vm.start_pack("abc");
  CHECK(f.engine.log.back() == "start-pack abc 42");
  GameState g = f.g;
  g.mission = "csv-1";
  f.vm.on_snapshot(stamped(g, CommandResult{CommandKind::StartPack, true, "none"}, "run-p"));
  CHECK(f.vm.screen() == Screen::Briefing);
  CHECK(f.vm.run_pack() == "abc");
  CHECK(screens::mission_title(f.vm, "csv-1") == "BTC · 1h");
  CHECK(f.vm.progress().runs.back().pack_digest == "abc");
}

// ---- Assisted practice ----

TEST_CASE("assisted: rewind sends the decision typed (clamped), links the attempt and keeps the plans before it") {
  Fixture f;
  f.to_game();
  f.vm.add_journal_entry(PlanEntry{62, true, "antes", 99.0, std::nullopt, 60, 10.0, false, "", ""});
  f.vm.add_journal_entry(PlanEntry{66, true, "depois", 99.0, std::nullopt, 60, 10.0, false, "", ""});
  REQUIRE(f.vm.rewind_range());
  CHECK(*f.vm.rewind_range() == std::pair<int, int>{60, 69});
  ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kGameAssist.panel, false));
  CHECK(m.text_of(names::kGameAssist.turn_label) == "Voltar à barra (1–10)");
  f.vm.set_assist_turn("15");  // above the range: brought into it
  CHECK(f.vm.assist_target_turn() == 10);
  f.vm.set_assist_turn("4,6");
  CHECK(f.vm.assist_target_turn() == 5);
  f.vm.set_assist_turn("x");
  CHECK(f.vm.assist_target_turn() == 1);
  f.vm.set_assist_turn("5");
  f.vm.rewind();
  CHECK(f.engine.log.back() == "rewind 64");
  const auto link = progress_json::assisted_from(*json::parse(f.engine.last_links.assisted));
  REQUIRE(link);
  CHECK(*link == AssistedLink{"run-1", "rewind", 64});

  GameState g = base_state(64);
  f.vm.on_snapshot(stamped(g, CommandResult{CommandKind::Rewind, true, "none"}, "run-2", true));
  CHECK(f.vm.screen() == Screen::Game);
  CHECK(f.vm.run_assisted() == link);
  const RunRecord& attempt = f.vm.progress().runs.back();
  CHECK(attempt.run_id == "run-2");
  CHECK(attempt.assisted);
  REQUIRE(attempt.journal.size() == 1);
  CHECK(attempt.journal[0].thesis == "antes");
  CHECK(f.vm.progress().runs.front().status == RunStatus::Left);  // the original attempt stays as it was
  ScreenModel game = f.vm.screen_model();
  CHECK(game.is_visible(names::kGameAssistedTag, false));
  CHECK(game.text_of(names::kGameAssistedTag) == "ASSISTIDA");
}

TEST_CASE("assisted: an attempt never completes the mission, earns no points and shows its banner") {
  Fixture f;
  f.to_game();
  f.vm.set_assist_turn("1");
  f.vm.rewind();
  f.vm.on_snapshot(stamped(base_state(60), CommandResult{CommandKind::Rewind, true, "none"}, "run-2", true));
  const int points = f.vm.game().prefs().advisor_points;
  f.vm.on_snapshot(debrief_snapshot(base_state(60), "run-2", true));
  REQUIRE(f.vm.screen() == Screen::Debrief);
  CHECK(f.vm.progress().completed.empty());
  CHECK(f.vm.game().prefs().finished.empty());
  CHECK(f.vm.game().prefs().advisor_points == points);
  const ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kDebriefAssisted, false));
  CHECK(m.text_of(names::kDebriefAssisted).starts_with("[ASSISTIDA] Prática assistida"));
  CHECK(f.vm.progress().runs.back().status == RunStatus::Finished);
}

TEST_CASE("assisted: another market starts the same mission on a new seed without the advisor") {
  Fixture f;
  f.to_game();
  f.vm.practice_new_seed();
  CHECK(f.engine.log.back().starts_with("start m01-false-breakout 42"));
  CHECK(f.engine.last_options.advice_allowance == 0);
  const auto link = progress_json::assisted_from(*json::parse(f.engine.last_links.assisted));
  REQUIRE(link);
  CHECK(link->kind == "seed");
  CHECK(link->from == "run-1");
  f.vm.on_snapshot(stamped(base_state(60), CommandResult{CommandKind::Start, true, "none"}, "run-3", true));
  CHECK(f.vm.screen() == Screen::Briefing);  // the tutorial was already seen in this run's start
  CHECK(f.vm.run_assisted());
  CHECK(f.vm.progress().runs.back().assisted);
}

TEST_CASE("assisted: blocked in a campaign mission in progress, offered after its debrief") {
  Fixture f;
  f.vm.campaign_start();
  f.vm.on_snapshot(stamped(f.g, CommandResult{CommandKind::Start, true, "none"}, "run-c"));
  if (f.vm.screen() == Screen::Tutorial) f.vm.tutorial_finish();
  f.vm.open_terminal();
  CHECK_FALSE(f.vm.assist_allowed());
  const ScreenModel m = f.vm.screen_model();
  CHECK(m.is_visible(names::kGameAssist.blocked, false));
  CHECK_FALSE(m.is_visible(names::kGameAssist.rewind, true));
  const std::size_t sent = f.engine.log.size();
  f.vm.rewind();
  CHECK(f.engine.log.size() == sent);
  f.vm.on_snapshot(debrief_snapshot(f.g, "run-c"));
  CHECK(f.vm.assist_allowed());
}

// ---- Runs saved in an earlier launch ----

TEST_CASE("saved runs: a campaign mission is offered while its campaign waits on it, and resumes as one") {
  Fixture f;
  MemoryProgressRepository repo;
  tradelab::desktop::Progress p;
  p.campaign = CampaignRun{"camp-1", 1, "10000", 0, CampaignStatus::Active, {}};
  repo.save(p);
  f.vm.attach_repositories(&repo, nullptr);
  f.vm.offer_saved_run(SavedRun{"m01-false-breakout", 5, "run-old", CampaignLink{"camp-1", 0}, "", std::nullopt});
  REQUIRE(f.vm.resumable());
  CHECK(f.vm.screen_model().text_of(names::kMenuResumeText) == "[CAMPANHA] Missão 01 · Range · seed 5");
  f.vm.resume();
  CHECK(f.engine.log.back() == "resume");
  f.vm.on_snapshot(stamped(f.g, CommandResult{CommandKind::Resume, true, "none"}, "run-old"));
  CHECK(f.vm.screen() == Screen::Game);
  CHECK(f.vm.shell().run_campaign == CampaignLink{"camp-1", 0});
  CHECK(f.vm.shell().run_id == "run-old");

  // One the campaign has moved past is not offered.
  f.vm.offer_saved_run(SavedRun{"m01-false-breakout", 5, "run-older", CampaignLink{"camp-0", 0}, "", std::nullopt});
  CHECK_FALSE(f.vm.saved_run());
}

TEST_CASE("saved runs: a pack run and an assisted attempt are offered with their label and tag") {
  Fixture f;
  f.vm.on_pack_event(packs::PackEvent{packs::PackEvent::Kind::Library, {storage::StoredPack{"abc", "csv-1", "BTC · 1h"}}});
  f.vm.offer_saved_run(SavedRun{"csv-1", 9, "run-p", std::nullopt, "abc", AssistedLink{"run-x", "rewind", 70}});
  CHECK(f.vm.screen_model().text_of(names::kMenuResumeText) == "[ASSISTIDA] BTC · 1h · seed 9");
  f.vm.resume();
  GameState g = f.g;
  g.mission = "csv-1";
  f.vm.on_snapshot(stamped(g, CommandResult{CommandKind::Resume, true, "none"}, "run-p", true));
  CHECK(f.vm.run_pack() == "abc");
  CHECK(f.vm.run_assisted());
}

// ---- Debrief: chart, candle table, lesson results ----

TEST_CASE("debrief: lesson results show each question's wording, the answer given and the key") {
  Fixture f;
  f.to_game();
  f.vm.on_snapshot(debrief_snapshot(f.g, "run-1"));
  const ScreenModel m = f.vm.screen_model();
  const WidgetLines* l = m.lines_of(names::kDebriefLessons);
  REQUIRE(l);
  std::vector<std::string> texts;
  for (const auto& line : l->lines) texts.push_back(line.text);
  REQUIRE(texts.size() == 7);  // title, score, then each question (a right one has no key line)
  CHECK(texts[2].starts_with("1. O preço rompe o topo de um range"));
  CHECK(texts[3] == "\xE2\x9C\x96 Sua resposta: Nada: rompimento é sinal de compra (errada)");
  CHECK(texts[4] == "Resposta certa: Um ponto de invalidação (stop) e o risco calculado");
  CHECK(texts[5].starts_with("2. Com patrimônio de 10.000"));
  CHECK(texts[6] == "\xE2\x9C\x94 Sua resposta: 200 (certa)");
}

TEST_CASE("debrief: the run's chart with entries and exits marked, and its candle table by pages") {
  Fixture f;
  f.to_game();
  f.vm.on_snapshot(debrief_snapshot(base_state(130), "run-1"));
  REQUIRE(f.vm.screen() == Screen::Debrief);
  const game::ChartScene scene = f.vm.debrief_scene(900, 320);
  REQUIRE_FALSE(scene.empty);
  int entries = 0;
  int exits = 0;
  for (const auto& r : scene.rects) {
    entries += r.outline && r.ink == game::Ink::Info && r.w == 10 ? 1 : 0;
    exits += r.outline && r.ink == game::Ink::Attention && r.w == 10 ? 1 : 0;
  }
  CHECK(entries == 2);
  CHECK(exits == 1);  // the second trade is still open
  ScreenModel m = f.vm.screen_model();
  const WidgetTable* t = m.table_of(names::kDebriefBars);
  REQUIRE(t);
  CHECK(t->columns.front() == "#");
  CHECK(t->rows.size() == 50);  // the last 50 of 131 closed bars
  CHECK(t->rows.front().front() == "81");
  CHECK(m.text_of(names::kDebriefBarsPage) == "Barras #81–#130 de 131");
  CHECK_FALSE(m.is_enabled(names::kDebriefBarsNext));
  f.vm.debrief_page(-1);
  m = f.vm.screen_model();
  CHECK(m.table_of(names::kDebriefBars)->rows.front().front() == "31");
  CHECK(m.is_enabled(names::kDebriefBarsNext));
}
