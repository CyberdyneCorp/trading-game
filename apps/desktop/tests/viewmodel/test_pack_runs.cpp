// Pack runs played through the shell like a player (scripted), on real engine snapshots: a schema-2
// pack with a mark-price series (apps/web/e2e/fixtures/mark-v2.tlpack, the web e2e fixture, signed
// with the device key of its seed) and the web catalog's liquidity-shock pack (Market C), from the
// briefing to the debrief. The shell starts the run as the catalog screen does (AppViewModel::start_pack);
// the recorder stands in for the worker (EngineHost::start_pack).
#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "model/packs/files.hpp"
#include "model/packs/pack_service.hpp"
#include "model/storage/run_recorder.hpp"
#include "model/storage/storage.hpp"
#include "support/storage_fixtures.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/ticket_vm.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::web_catalog_dir;

namespace {

constexpr const char* kFixtureDeviceSeed = "0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e0e2e";
constexpr const char* kSampleC = "3090a1aae3e3416923ad8dafa72f0c21bbad9f5d3558116356a8f9f37fed82c7";

struct NoEngine final : EngineCommands {
  void start(const std::string& /*mission*/, std::uint32_t /*seed*/) override {}
  void advance(int /*steps*/) override {}
  void market(Side /*side*/, const std::string& /*qty*/) override {}
};

std::shared_ptr<const Snapshot> as(Snapshot s, CommandKind kind) {
  s.results.push_back(CommandResult{kind, true, "none"});
  return std::make_shared<Snapshot>(std::move(s));
}

std::vector<std::uint8_t> read(const std::filesystem::path& p) {
  auto bytes = packs::read_file(p);
  REQUIRE(bytes);
  return *bytes;
}

std::string objective(const ScreenModel& m, std::size_t i) {
  const WidgetLines* l = m.lines_of(names::kBriefObjectives);
  return l != nullptr && i < l->lines.size() ? l->lines[i].text : std::string();
}

struct PackSession {
  TempDir dir;
  storage::Storage storage{dir.path()};
  storage::RunRecorder rec{storage};
  NoEngine engine;
  AppViewModel vm{builtin_catalog(), engine_missions(), engine, [] { return 5u; }};
  Catalog catalog = builtin_catalog();
  game::Texts texts{catalog};
  std::unique_ptr<EngineRun> run;

  PackSession() { storage.store().set_meta("deviceSeed", kFixtureDeviceSeed); }

  // Installs the pack, starts a run on it and opens the briefing.
  void start(const std::vector<std::uint8_t>& bytes, const std::string& origin) {
    const packs::PackCheck check = storage.packs().install(bytes, "pack", origin);
    INFO(check.reason);
    REQUIRE(check.ok);
    REQUIRE(check.entry);
    vm.on_pack_event(packs::PackEvent{packs::PackEvent::Kind::Library, {*check.entry}});
    vm.start_pack(check.entry->digest);
    run = rec.start_pack(check.entry->digest, 5);
    REQUIRE(run);
    vm.on_snapshot(as(run->snapshot(), CommandKind::StartPack));
    REQUIRE(vm.screen() == Screen::Briefing);
  }
  [[nodiscard]] GameState state() const { return game_state_from_json(run->snapshot().state); }
  [[nodiscard]] int turn() const {
    const GameState g = state();
    return g.step - g.warmup_bars + 1;
  }
};

}  // namespace

TEST_CASE("pack runs: a v2 pack with a mark series briefs with its texts, tiers and note; orders price at the last price") {
  PackSession p;
  p.start(read(web_catalog_dir().parent_path().parent_path() / "e2e" / "fixtures" / "mark-v2.tlpack"), "import");
  ScreenModel m = p.vm.screen_model();
  CHECK(m.text_of(names::kBriefMission) == "Treino v2 · preço de marcação");
  CHECK(m.text_of(names::kBriefTagline).starts_with("A margem deste mercado segue o preço de marcação"));
  CHECK(m.text_of(names::kBriefTiers) == "Faixas por tamanho: ≥ 20.000: 2,0% (até 4x) · ≥ 60.000: 5,0% (até 2x)");
  CHECK(m.text_of(names::kBriefMark).ends_with("usam o preço de marcação; as execuções usam o último preço."));
  CHECK_FALSE(m.is_visible(names::kBriefShocks));
  CHECK(objective(m, 1) == "• Mantenha a distância até a liquidação acima de 15%.");
  CHECK(objective(m, 3) == "• Uma limitada que não executou é um trade perdido, não um prejuízo.");
  p.vm.set_locale(Locale::En);
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kBriefMission) == "Practice v2 · mark price");
  CHECK(m.text_of(names::kBriefTiers) == "Tiers by size: ≥ 20,000: 2.0% (up to 4x) · ≥ 60,000: 5.0% (up to 2x)");
  p.vm.open_terminal();
  CHECK(p.vm.screen_model().text_of(names::kGameMission) == "Practice v2 · mark price");

  const GameState g = p.state();
  REQUIRE(g.account.mark < g.last_price());  // the fixture's mark sits below the last price
  game::TicketViewModel tk;
  tk.size_by = game::SizeBy::Qty;
  CHECK(fmt::parse_decimal(tk.view(p.texts, g, false).est_entry) > g.last_price());
}

TEST_CASE("pack runs: the liquidity-shock pack warns on the bars before each shock and plays to the debrief") {
  PackSession p;
  p.start(read(web_catalog_dir() / "packs" / (std::string(kSampleC) + ".tlpack")), "catalog");
  ScreenModel m = p.vm.screen_model();
  CHECK(m.text_of(names::kBriefMission) == "Treino · choque de liquidez");
  CHECK(m.is_visible(names::kBriefShocks));
  CHECK(objective(m, 1) == "• Use stop: risco planejado de até 1% do patrimônio por ideia.");
  p.vm.open_terminal();
  game::TicketViewModel tk;
  std::vector<int> warned;
  std::string first, second;
  while (p.run->snapshot().active()) {
    const GameState g = p.state();
    const std::string shock = tk.view(p.texts, g, false).shock;
    if (!shock.empty()) {
      warned.push_back(p.turn());
      (first.empty() ? first : second) = shock;
    }
    p.run->advance(1);
    p.vm.on_snapshot(as(p.run->snapshot(), CommandKind::Advance));
  }
  std::vector<int> expected;
  for (int t = 15; t <= 20; ++t) expected.push_back(t);  // headline at bar 15, shock on bars 16-21
  for (int t = 171; t <= 180; ++t) expected.push_back(t);
  CHECK(warned == expected);
  CHECK(first.find("×4,0") != std::string::npos);
  CHECK(first.find("25%") != std::string::npos);
  CHECK(second.find("×6,0") != std::string::npos);
  REQUIRE(p.vm.screen() == Screen::Debrief);
  m = p.vm.screen_model();
  CHECK(m.text_of(names::kDebriefTitle) == "Missão concluída");
  CHECK(m.text_of(names::kDebriefText) == "Treino · choque de liquidez");
}
