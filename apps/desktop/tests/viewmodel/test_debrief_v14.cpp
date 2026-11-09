// Scoring v14 debrief views on the desktop (add-practice-and-advanced-strategies W2-D3), checked
// against the web with real engine debriefs:
// - tests/fixtures/engine/debriefs-w2.json holds the debrief parts the views read (findings with their
//   evidence, episodes, the mission's objectives) of the m18-m25 goldens, replayed here through the
//   desktop's engine; this test fails when the engine no longer produces them (regenerate with
//   TRADELAB_UPDATE_DEBRIEFS=1, then `node scripts/gen_web_fixtures.mjs`);
// - tests/fixtures/web/debriefV14.json is what the web's objectiveChecks.ts, analysis.ts and
//   EpisodesPanel.svelte formatting make of those inputs, in both locales; the C++ ports must agree.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "model/engine_run.hpp"
#include "model/i18n.hpp"
#include "model/json.hpp"
#include "model/storage/actions.hpp"
#include "model/storage/json_writer.hpp"
#include "support/fake_engine.hpp"
#include "support/shell_helpers.hpp"
#include "support/web_fixture.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/objective_checks.hpp"
#include "viewmodel/report_texts.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::test_support;

namespace {

const std::vector<std::string> kGoldens = {"m18-gamma-scalp", "m19-funding-carry", "m20-event-vol", "m21-calendar",
                                           "m22-scale-manage", "m23-second-chance", "m24-fast-market", "m25-the-dip"};

const std::string kInputs = std::string(TRADELAB_ENGINE_FIXTURES_DIR) + "/debriefs-w2.json";

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

// The golden's run replayed through the desktop's action adapter, as its debrief inputs.
std::string engine_input(const std::string& name) {
  const auto fx = json::parse(read_file(std::string(TRADELAB_GOLDEN_DIR) + "/" + name + ".json"));
  REQUIRE(fx);
  auto run = EngineRun::create((*fx)["mission"].as_string(), static_cast<std::uint32_t>((*fx)["seed"].as_int()), {});
  REQUIRE(run);
  for (const auto& a : (*fx)["actions"].items()) storage::apply_action(*run, a);
  const auto snap = run->snapshot();
  REQUIRE(snap.debrief);
  const json::Value& d = snap.debrief->raw;
  storage::JsonWriter w;
  w.begin_object().field("mission", name).field("initialBalance", snap.initial_balance);
  w.key("findings").raw(storage::dump(d["findings"])).key("episodes").raw(storage::dump(d["episodes"]));
  w.key("missionRules").begin_object().key("objectives").raw(storage::dump(d["missionRules"]["objectives"]));
  w.field("liqWarningBp", static_cast<std::int64_t>(d["missionRules"]["liqWarningBp"].as_int(-1))).end_object();
  return w.end_object().text();
}

std::string engine_inputs() {
  std::string out = "[\n";
  for (std::size_t i = 0; i < kGoldens.size(); ++i) out += engine_input(kGoldens[i]) + (i + 1 < kGoldens.size() ? ",\n" : "\n");
  return out + "]\n";
}

Catalog catalog_in(const Value& l) {
  Catalog c = builtin_catalog();
  c.set_locale(locale(l));
  return c;
}

}  // namespace

TEST_CASE("debrief v14: the m18-m25 goldens' engine debriefs are the parity inputs") {
  const std::string now = engine_inputs();
  if (std::getenv("TRADELAB_UPDATE_DEBRIEFS") != nullptr) {
    std::ofstream(kInputs, std::ios::binary) << now;
    MESSAGE("rewrote " << kInputs << ": run node scripts/gen_web_fixtures.mjs");
  }
  CHECK_MESSAGE(read_file(kInputs) == now, "engine debriefs changed: TRADELAB_UPDATE_DEBRIEFS=1, then gen_web_fixtures.mjs");
}

TEST_CASE("debrief v14: the goldens show every bias finding with evidence and episodes") {
  const auto inputs = json::parse(read_file(kInputs));
  REQUIRE(inputs);
  std::vector<std::string> codes;
  int episodes = 0;
  for (const auto& in : inputs->items()) {
    for (const auto& f : in["findings"].items()) {
      if (f["evidence"].kind() == json::Value::Kind::Object) codes.push_back(f["code"].as_string());
    }
    episodes += static_cast<int>(in["episodes"].items().size());
  }
  for (const char* code : {"revenge_trade", "fomo_entry", "averaging_down"}) {
    INFO(code);
    CHECK(std::find(codes.begin(), codes.end(), code) != codes.end());
  }
  CHECK(episodes > 0);
}

// The m23 golden (a revenge entry at twice the size right after a losing close) played into the shell:
// the debrief names the bias with its evidence, reads the objective as missed at that bar, lists the
// bar among its moments and the two positions as episodes; picking the moment marks it on the chart.
TEST_CASE("debrief v14: m23's revenge trade, its objective, moment and episodes on the debrief screen") {
  testing::FakeEngine engine;
  AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 2026u; }};
  testing::tutorials_seen(vm);
  vm.select(22);
  vm.start_selected();
  const auto fx = json::parse(read_file(std::string(TRADELAB_GOLDEN_DIR) + "/m23-second-chance.json"));
  REQUIRE(fx);
  auto run = EngineRun::create("m23-second-chance", 2026, {});
  Snapshot started = run->snapshot();
  started.results.push_back(CommandResult{CommandKind::Start, true, "none"});
  vm.on_snapshot(std::make_shared<Snapshot>(std::move(started)));
  REQUIRE(vm.screen() == Screen::Briefing);
  vm.open_terminal();
  for (const auto& a : (*fx)["actions"].items()) storage::apply_action(*run, a);
  vm.on_snapshot(std::make_shared<Snapshot>(run->snapshot()));
  REQUIRE(vm.screen() == Screen::Debrief);

  const ScreenModel m = vm.screen_model();
  const WidgetLines* findings = m.lines_of(names::kDebriefFindings);
  REQUIRE(findings != nullptr);
  CHECK(std::any_of(findings->lines.begin(), findings->lines.end(), [](const Line& l) {
    return l.text.find("Vingança: o trade 1 fechou com perda de 112,81 cr na barra 83") != std::string::npos &&
           l.text.find("sem plano registrado (2,00×") != std::string::npos;
  }));
  const WidgetLines* objectives = m.lines_of(names::kDebriefObjectives);
  REQUIRE(objectives != nullptr);
  REQUIRE(objectives->lines.size() == 2);
  CHECK(objectives->lines[1].text.starts_with("\xE2\x9A\xA0 Nenhuma entrada nova nas 3 barras seguintes a um stop"));
  CHECK(objectives->lines[1].tone == Tone::Attention);
  const WidgetItems* moments = m.items_of(names::kDebriefMoments);
  REQUIRE(moments != nullptr);
  REQUIRE(moments->items.size() == 4);  // the finding, the objective, two episodes
  CHECK(moments->items[0] == "ver barra 83 \xC2\xB7 Trade de vingança");
  CHECK(moments->selected == -1);
  const WidgetTable* episodes = m.table_of(names::kDebriefEpisodes);
  REQUIRE(episodes != nullptr);
  CHECK(episodes->rows.size() == 2);
  CHECK(m.is_visible(names::kDebriefEpisodesPanel, false));

  vm.debrief_show_moment(0);
  CHECK(vm.debrief_moment() == 83);
  CHECK(vm.screen_model().items_of(names::kDebriefMoments)->selected == 0);
  const game::ChartScene scene = vm.debrief_scene(900, 320);
  CHECK(std::any_of(scene.texts.begin(), scene.texts.end(), [](const game::SceneText& t) { return t.text == "\xE2\x96\xBC"; }));
  CHECK(std::any_of(scene.rects.begin(), scene.rects.end(), [](const game::SceneRect& r) { return r.alpha < 0.2f && r.ink == game::Ink::Attention; }));
  vm.debrief_show_moment(-1);
  CHECK_FALSE(vm.debrief_moment());
}

namespace {

// A golden's run played into the shell up to its debrief.
void golden_debrief(AppViewModel& vm, const std::string& name, int index) {
  testing::tutorials_seen(vm);
  vm.select(index);
  vm.start_selected();
  const auto fx = json::parse(read_file(std::string(TRADELAB_GOLDEN_DIR) + "/" + name + ".json"));
  REQUIRE(fx);
  auto run = EngineRun::create(name, static_cast<std::uint32_t>((*fx)["seed"].as_int()), {});
  Snapshot started = run->snapshot();
  started.results.push_back(CommandResult{CommandKind::Start, true, "none"});
  vm.on_snapshot(std::make_shared<Snapshot>(std::move(started)));
  vm.open_terminal();
  for (const auto& a : (*fx)["actions"].items()) storage::apply_action(*run, a);
  vm.on_snapshot(std::make_shared<Snapshot>(run->snapshot()));
  REQUIRE(vm.screen() == Screen::Debrief);
}

}  // namespace

// Regression: m18-m22's structure names ("Tudo de uma vez: entrada após a manchete, saída inteira em 2R")
// were cut by the comparison table's first column, the [best] tag with them; two m18 rows began with the
// same words. The table keeps a short name (told apart by its order) and the rule is listed under it.
TEST_CASE("debrief v14: the m18 and m22 comparisons keep short distinct names with the rules listed") {
  for (const auto& [name, index] : {std::pair<std::string, int>{"m22-scale-manage", 21}, {"m18-gamma-scalp", 17}}) {
    INFO(name);
    testing::FakeEngine engine;
    AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 1u; }};
    golden_debrief(vm, name, index);
    const ScreenModel m = vm.screen_model();
    const WidgetTable* t = m.table_of(names::kDebriefComparison);
    REQUIRE(t != nullptr);
    REQUIRE(t->rows.size() == 4);
    std::vector<std::string> heads;
    for (std::size_t i = 1; i < t->rows.size(); ++i) {
      const std::string& cell = t->rows[i][0];
      const std::string head = cell.substr(0, cell.find(" ["));
      CHECK(head.size() <= 32);
      CHECK(std::find(heads.begin(), heads.end(), head) == heads.end());
      heads.push_back(head);
    }
    CHECK(std::any_of(t->rows.begin(), t->rows.end(), [](const auto& r) { return r[0].find("[melhor aqui]") != std::string::npos; }));
    const WidgetLines* notes = m.lines_of(names::kDebriefComparisonNotes);
    REQUIRE(notes != nullptr);
    CHECK(notes->lines.size() == 3);
  }
  testing::FakeEngine engine;
  AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 1u; }};
  golden_debrief(vm, "m22-scale-manage", 21);
  const ScreenModel model = vm.screen_model();
  const WidgetLines* notes = model.lines_of(names::kDebriefComparisonNotes);
  REQUIRE(notes != nullptr);
  CHECK(notes->lines[0].text == "Tudo de uma vez: entrada após a manchete, saída inteira em 2R");
}

// A mission without v14 facts (m01) shows none of these panels.
TEST_CASE("debrief v14: a debrief without objectives to read, moments or episodes hides their panels") {
  testing::FakeEngine engine;
  AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 1u; }};
  testing::tutorials_seen(vm);
  vm.start_selected();
  auto run = EngineRun::create("m01-false-breakout", 1, {});
  Snapshot started = run->snapshot();
  started.results.push_back(CommandResult{CommandKind::Start, true, "none"});
  vm.on_snapshot(std::make_shared<Snapshot>(std::move(started)));
  vm.open_terminal();
  run->advance(500);
  vm.on_snapshot(std::make_shared<Snapshot>(run->snapshot()));
  REQUIRE(vm.screen() == Screen::Debrief);
  const ScreenModel m = vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kDebriefObjectives));
  CHECK_FALSE(m.is_visible(names::kDebriefMomentsBox));
  CHECK_FALSE(m.is_visible(names::kDebriefEpisodesPanel));
}

TEST_CASE("web parity: objectiveChecks.ts, the v14 finding texts and the episodes panel") {
  replay("debriefV14",
         {{"objectiveReadings",
           [](const Value& a, const Value& o) {
             const auto got = objective_readings(a[0], a[1].as_double());
             REQUIRE(got.size() == o.size());
             for (std::size_t i = 0; i < got.size(); ++i) {
               CHECK(got[i].code == o[i]["code"].as_string());
               CHECK(got[i].met == (o[i]["verdict"].as_string() == "met"));
               CHECK(got[i].step.has_value() == o[i].has("step"));
               if (got[i].step) CHECK(*got[i].step == o[i]["step"].as_int());
             }
           }},
          {"findingMoment",
           [](const Value& a, const Value& o) {
             const auto got = finding_moment(a[0]);
             REQUIRE(got.has_value() == !o.is_null());
             if (got) CHECK(*got == o.as_int());
           }},
          {"describeFindings",
           [](const Value& a, const Value& o) {
             const Catalog c = catalog_in(a[1]);
             check_strings(describe_findings(c, a[0], static_cast<int>(a[2].as_int())), o);
           }},
          {"episodeCells", [](const Value& a, const Value& o) { check_strings(episode_cells(catalog_in(a[1]), a[0]), o); }}});
}
