// Scripted shell flows of the tutorial, assisted practice, debrief chart and lesson results, the pack
// catalog and the CSV import, and runs resumed by a later launch, driven by injected input like the
// web's Playwright specs. Shared by the headless tests (null platform) and the windowed tests (Vulkan,
// with captures); a flow over two launches takes the Raven session type.
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_widget.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include "model/packs/files.hpp"
#include "model/storage/storage.hpp"
#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "support/http_server.hpp"
#include "support/storage_fixtures.hpp"
#include "viewmodel/screen_names.hpp"

namespace tradelab::desktop::testing {

using Shot = std::function<void(const std::string&)>;

inline bool shown(Driver& d, std::string_view name) { return Rwidget_get_visibility(d.widget(name)) == R_VISIBLE; }

// A button's text as the screen shows it (Raven has no getter for a button's text).
inline std::string shown_text(App& app, std::string_view name) { return app.vm().screen_model().text_of(name); }

// Shift+Space until the debrief; calibration questions skipped.
inline void play_to_debrief(App& app, Driver& d, Terminal& t) {
  for (int guard = 0; guard < 200 && app.vm().screen() == Screen::Game; ++guard) {
    t.skip_question();
    d.focus(names::kGameAdvance);
    d.key(R_KEY_SPACE, R_MOD_SHIFT);
    REQUIRE(d.idle());
  }
  REQUIRE(app.vm().screen() == Screen::Debrief);
}

// Tutorial.svelte: shown by the first start, keys move between cards, Esc goes on to the briefing; the
// next start goes straight to the briefing, which can replay it.
inline void flow_tutorial(App& app, Driver& d, const Shot& shot = {}) {
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Tutorial; }));
  CHECK(d.text(names::kTutTitle) == "Leia o gráfico");
  CHECK(d.text(names::kTutStep).starts_with("1 de "));
  if (shot) shot("flow-tutorial-card1");
  d.key(R_KEY_RIGHT);
  CHECK(d.text(names::kTutTitle) == "Long ou short");
  d.key(R_KEY_LEFT);
  CHECK(d.text(names::kTutTitle) == "Leia o gráfico");
  REQUIRE(d.click(names::kTutNext));
  CHECK(app.vm().tutorial_index() == 1);
  d.key(R_KEY_ESCAPE);
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Briefing; }));
  CHECK(app.vm().game().tutorial_seen("m01-false-breakout"));
  REQUIRE(d.click(names::kBriefBack));
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Briefing; }));  // seen: no tutorial
  REQUIRE(shown(d, names::kBriefTutorial));
  REQUIRE(d.click(names::kBriefTutorial));
  CHECK(app.vm().screen() == Screen::Tutorial);
  REQUIRE(d.click(names::kTutSkip));
  CHECK(app.vm().screen() == Screen::Briefing);
}

// AssistPanel.svelte: rewind from the terminal to a turn typed in its field, play the attempt to its
// debrief (banner, no points), then the same mission on another market from the debrief.
inline void flow_assisted(App& app, Driver& d, Terminal& t, const Shot& shot = {}) {
  t.open(0);
  for (int i = 0; i < 6; ++i) t.advance();
  const int warmup = t.g().warmup_bars;
  REQUIRE(shown(d, names::kGameAssist.panel));
  CHECK(shown(d, names::game::kAdvisorBox));
  CHECK(d.text(names::kGameAssist.turn_label) == "Voltar à barra (1–6)");
  d.type_into(names::kGameAssist.turn, "3");
  REQUIRE(d.click(names::kGameAssist.rewind));
  REQUIRE(d.until([&] { return app.vm().run_assisted().has_value() && !app.vm().waiting(); }));
  CHECK(app.vm().screen() == Screen::Game);
  CHECK(t.g().step == warmup + 2);
  REQUIRE(shown(d, names::kGameAssistedTag));
  CHECK(d.text(names::kGameAssistedTag) == "ASSISTIDA");
  CHECK_FALSE(app.vm().game().advisor_offered());  // started without the advisor (Terminal.svelte)
  CHECK_FALSE(shown(d, names::game::kAdvisorBox));
  if (shot) shot("flow-assisted-terminal");
  const int points = app.vm().game().prefs().advisor_points;
  play_to_debrief(app, d, t);
  REQUIRE(shown(d, names::kDebriefAssisted));
  CHECK(d.text(names::kDebriefAssisted).starts_with("[ASSISTIDA]"));
  CHECK(app.vm().game().prefs().advisor_points == points);
  CHECK(app.vm().progress().completed.empty());
  if (shot) shot("flow-assisted-debrief");
  REQUIRE(d.click(names::kDebriefAssist.new_seed));
  REQUIRE(d.to_briefing());
  REQUIRE(app.vm().run_assisted());
  CHECK(app.vm().run_assisted()->kind == "seed");
}

// Debrief: lesson results with each question's wording (questions skipped in the terminal's lesson
// dialog), the run's chart and its candle table by pages.
inline void flow_debrief_chart_and_lessons(App& app, Driver& d, Terminal& t, const Shot& shot = {}) {
  t.open(0);
  REQUIRE(d.click(names::game::kLessons));
  // A count, not a reference into the state: each skip delivers a new snapshot and frees the old state.
  const int questions = static_cast<int>(t.g().lesson.questions.size());
  REQUIRE(questions > 0);
  for (int q = 0; q < questions; ++q) {
    app.vm().game().lesson_skip(q);  // the dialog's Skip of each question
    REQUIRE(d.idle());
  }
  if (app.vm().game().modal() == game::Modal::Lesson) REQUIRE(d.click(names::game::kLessonClose));
  play_to_debrief(app, d, t);
  const ScreenModel debrief = app.vm().screen_model();
  const auto* lessons = debrief.lines_of(names::kDebriefLessons);
  REQUIRE(lessons != nullptr);
  REQUIRE(lessons->lines.size() > 3);
  const auto prompt = std::find_if(lessons->lines.begin(), lessons->lines.end(),
                                   [](const Line& l) { return l.text.starts_with("1. O preço rompe o topo de um range"); });
  REQUIRE(prompt != lessons->lines.end());
  REQUIRE(prompt + 1 != lessons->lines.end());
  CHECK((prompt + 1)->text.find("Sua resposta: Pulada") != std::string::npos);
  REQUIRE(shown(d, names::kDebriefChart));
  CHECK_FALSE(app.vm().debrief_scene(900, 320).empty);
  const std::string last_page = d.text(names::kDebriefBarsPage);
  CHECK(last_page.starts_with("Barras #"));
  REQUIRE(d.click(names::kDebriefBarsPrev));
  CHECK(d.text(names::kDebriefBarsPage) != last_page);
  if (shot) shot("flow-debrief-chart");
}

// A practice CSV with one gap: 300 hourly rows less the 120th.
inline std::filesystem::path write_gap_csv(const std::filesystem::path& dir) {
  std::string csv = make_csv(300);
  std::size_t at = 0;
  for (int line = 0; line < 120; ++line) at = csv.find('\n', at) + 1;
  csv.erase(at, csv.find('\n', at) + 1 - at);
  const auto path = dir / "btc-1h.csv";
  std::ofstream(path) << csv;
  return path;
}

// CsvImport.svelte: path, declared data, the engine's report with its gap and the gap-fill choice, the
// pack created, then practised from the installed packs.
inline void flow_csv_import(App& app, Driver& d, const std::filesystem::path& csv, const Shot& shot = {}) {
  REQUIRE(d.click(names::kMenuImport));
  REQUIRE(app.vm().screen() == Screen::Import);
  d.type_into(names::kCsvPath, csv.string());
  REQUIRE(d.click(names::kCsvOpen));
  REQUIRE(shown(d, names::kCsvMeta));
  CHECK(d.text(names::kCsvFileName) == csv.filename().string());
  d.type_into(names::kCsvAsset, "BTC");
  d.type_into(names::kCsvPair, "BTC/USDT");
  REQUIRE(d.click(names::kCsvValidate));
  REQUIRE(d.until([&] { return app.vm().packs().report.has_value(); }));
  REQUIRE(shown(d, names::kCsvGapRow));
  d.choose(names::kCsvGapFill, 1);
  CHECK(app.vm().packs().meta.gap_fill == "skip");
  if (shot) shot("flow-csv-report");
  REQUIRE(d.click(names::kCsvCreate));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Menu; }));
  CHECK(d.text(names::kMenuNotice) == "Pacote instalado: BTC/USDT \xC2\xB7 1h");
  REQUIRE(d.click(names::kMenuCatalog));
  REQUIRE(d.until([&] { return !app.vm().packs().installed.empty(); }));
  REQUIRE(d.click(names::kLibraryPlay));
  REQUIRE(d.to_briefing());
  CHECK(d.text(names::kBriefMission) == "BTC/USDT \xC2\xB7 1h");
  REQUIRE(d.click(names::kBriefGo));
  REQUIRE(app.vm().screen() == Screen::Game);
  CHECK(d.text(names::kGameMission) == "BTC/USDT \xC2\xB7 1h");
  if (shot) shot("flow-csv-terminal");
}

// The web's sample catalog on a loopback server: the manifest at /catalog/catalog.json, pack A at any
// other path. Set as $TRADELAB_CATALOG_URL before the App is built.
struct CatalogServer {
  LoopbackHttpServer server;
  CatalogServer()
      : server(*packs::read_file(web_catalog_dir() / "packs" / "ec9140b18db6c1997d9ebefa494be5d040fc91052236045d2a6336d2263602ee.tlpack")) {
    server.serve("/catalog/catalog.json", *packs::read_file(web_catalog_dir() / "catalog.json"));
    ::setenv("TRADELAB_CATALOG_URL", server.url("/catalog/catalog.json").c_str(), 1);
  }
  ~CatalogServer() { ::unsetenv("TRADELAB_CATALOG_URL"); }
  CatalogServer(const CatalogServer&) = delete;
  CatalogServer& operator=(const CatalogServer&) = delete;
};

// Catalog.svelte: the catalog read, a download cut after its first chunk (resume offered with what
// arrived), resumed to the end, verified and installed, then practised.
inline void flow_catalog_download(App& app, Driver& d, CatalogServer& cs, const Shot& shot = {}) {
  REQUIRE(d.click(names::kMenuCatalog));
  REQUIRE(d.until([&] { return app.vm().packs().status == packs::CatalogStatus::Ready; }));
  REQUIRE(app.vm().packs().entries.size() == 3);  // the web sample catalog: Markets A, B and C
  CHECK(shown_text(app, names::kCatalogDownload) == "Baixar (76 KB)");
  if (shot) shot("flow-catalog");
  cs.server.set_remaining(1);  // the connection goes away after the first range
  REQUIRE(d.click(names::kCatalogDownload));
  REQUIRE(d.until([&] {
    const auto& dl = app.vm().packs().downloads;
    const auto it = dl.find(app.vm().packs().entries[0].digest);
    return it != dl.end() && it->second.phase == packs::DownloadPhase::Failed && !app.vm().packs().partial.empty();
  }));
  CHECK(d.text(names::kCatalogDownloadState) == "Download interrompido em 64 KB de 76 KB. Retomar continua de onde parou.");
  CHECK(shown_text(app, names::kCatalogDownload) == "Retomar download (64 KB de 76 KB)");
  if (shot) shot("flow-catalog-interrupted");
  cs.server.set_remaining(1 << 20);
  REQUIRE(d.click(names::kCatalogDownload));
  REQUIRE(d.until([&] { return !app.vm().packs().installed.empty(); }));
  REQUIRE(shown(d, names::kCatalogPlay));
  CHECK(d.text(names::kCatalogEntryTitle).find("[INSTALADO]") != std::string::npos);
  if (shot) shot("flow-catalog-installed");
  REQUIRE(d.click(names::kCatalogPlay));
  REQUIRE(d.to_briefing());
  REQUIRE(d.click(names::kBriefGo));
  REQUIRE(app.vm().screen() == Screen::Game);
  CHECK(d.text(names::kGameMission) == "Mercado A \xC2\xB7 1h");
}

// Two launches on one data folder (Session: the Raven session of the test, created per launch): what the
// first saves (language, tutorial seen, a campaign mission in play) is there in the second, whose menu
// resumes the campaign mission as one.
template <class Session>
void flow_relaunch_campaign(const std::filesystem::path& dir, const Shot& shot = {}) {
  int step = 0;
  std::string mission;
  {
    Session raven;
    App app(builtin_catalog(), [] { return 21u; }, std::make_unique<storage::Storage>(dir));
    REQUIRE(app.build(1280, 800));
    Driver d(app);
    d.frames(2);
    REQUIRE(d.click(names::kMenuSettings));
    REQUIRE(d.click(names::kSettingsLangEn));
    REQUIRE(d.click(names::kSettingsBack));
    REQUIRE(d.click(names::kCampaignStart));
    REQUIRE(d.to_briefing());
    REQUIRE(d.click(names::kBriefGo));
    Terminal t(app, d);
    for (int i = 0; i < 3; ++i) t.advance();
    step = t.g().step;
    mission = t.g().mission;
    REQUIRE(d.click(names::kGameMenu));
  }
  Session raven;
  App app(builtin_catalog(), [] { return 99u; }, std::make_unique<storage::Storage>(dir));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  CHECK(app.vm().locale() == Locale::En);
  CHECK(app.vm().game().tutorial_seen(mission));
  REQUIRE(shown(d, names::kMenuResumeRow));
  CHECK(d.text(names::kMenuResumeText) == "[CAMPAIGN] " + app.vm().tr("mission." + mission + ".title") + " \xC2\xB7 seed 21");
  CHECK(shown(d, names::kCampaignProgress));
  if (shot) shot("flow-relaunch-menu");
  REQUIRE(d.click(names::kCampaignContinue));  // the campaign's Continue resumes its mission
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Game; }));
  CHECK(app.vm().game().state()->step == step);
  REQUIRE(app.vm().shell().run_campaign);
  CHECK(app.vm().shell().run_campaign->index == 0);
}

// A pack run left mid-run is offered by the next launch with its pack's label and resumes.
template <class Session>
void flow_relaunch_pack(const std::filesystem::path& dir, const std::filesystem::path& csv) {
  int step = 0;
  {
    Session raven;
    App app(builtin_catalog(), [] { return 5u; }, std::make_unique<storage::Storage>(dir));
    REQUIRE(app.build(1280, 800));
    Driver d(app);
    d.frames(2);
    flow_csv_import(app, d, csv);
    Terminal t(app, d);
    for (int i = 0; i < 2; ++i) t.advance();
    step = t.g().step;
  }
  Session raven;
  App app(builtin_catalog(), [] { return 6u; }, std::make_unique<storage::Storage>(dir));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  REQUIRE(d.until([&] { return !app.vm().packs().installed.empty(); }));  // the label comes with the library
  REQUIRE(shown(d, names::kMenuResumeRow));
  CHECK(d.text(names::kMenuResumeText) == "BTC/USDT \xC2\xB7 1h \xC2\xB7 seed 5");
  REQUIRE(d.click(names::kMenuResume));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Game; }));
  CHECK(app.vm().game().state()->step == step);
  CHECK(app.vm().run_pack() == app.vm().packs().installed[0].digest);
}

}  // namespace tradelab::desktop::testing
