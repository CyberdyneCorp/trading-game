// Catalog and CSV-import screens (web Catalog.svelte, CsvImport.svelte) with a fake PackCommands: what
// each state shows, which command each button sends, and the ports of catalog.ts / packs.ts helpers.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "support/fake_engine.hpp"
#include "support/storage_fixtures.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using packs::CatalogStatus;
using packs::DownloadPhase;
using packs::PackEvent;
using testing::FakeEngine;
using testing::TempDir;

namespace {

struct FakePacks final : packs::PackCommands {
  std::vector<std::string> log;
  std::string csv;
  std::string meta;
  void refresh() override { log.emplace_back("refresh"); }
  void load_catalog() override { log.emplace_back("catalog"); }
  void download(const packs::CatalogEntry& e, const std::string& label) override { log.push_back("download " + e.id + " " + label); }
  void pause(const std::string& digest) override { log.push_back("pause " + digest); }
  void remove(const std::string& digest) override { log.push_back("remove " + digest); }
  void validate_csv(const std::string& c, const std::string& m) override {
    csv = c;
    meta = m;
    log.emplace_back("validate");
  }
  void import_csv(const std::string& /*c*/, const std::string& m, const std::string& label) override {
    meta = m;
    log.push_back("import " + label);
  }
};

packs::CatalogEntry entry(const std::string& id, const std::string& digest, std::int64_t size) {
  packs::CatalogEntry e;
  e.id = id;
  e.digest = digest;
  e.size = size;
  e.interval_seconds = 3600;
  e.bars = 4740;
  e.leverage_cap = 5;
  e.mode = "synthetic";
  e.title = {"Mercado " + id, "Market " + id};
  e.summary = {"Resumo", "Summary"};
  return e;
}

PackEvent catalog_event(CatalogStatus status, std::vector<packs::CatalogEntry> entries) {
  PackEvent e;
  e.kind = PackEvent::Kind::Catalog;
  e.status = status;
  e.entries = std::move(entries);
  return e;
}

PackEvent library_event(std::vector<storage::StoredPack> installed, std::map<std::string, std::int64_t> partial = {},
                        std::vector<std::string> missing = {}) {
  PackEvent e;
  e.kind = PackEvent::Kind::Library;
  e.packs = std::move(installed);
  e.partial = std::move(partial);
  e.missing = std::move(missing);
  return e;
}

PackEvent download_event(const std::string& digest, DownloadPhase phase, std::int64_t received, std::int64_t size,
                         std::string reason = {}) {
  PackEvent e;
  e.kind = PackEvent::Kind::Download;
  e.digest = digest;
  e.download = {phase, received, size, std::move(reason)};
  return e;
}

struct Fixture {
  FakeEngine engine;
  FakePacks packs;
  AppViewModel vm{builtin_catalog(), {{"m01-false-breakout", 140, 60, 5, true}}, engine, [] { return 7u; }};
  Fixture() { vm.attach_packs(&packs); }
};

}  // namespace

TEST_CASE("catalog helpers: sizes, labels, guessed metadata (packs.ts, catalog.ts)") {
  CHECK(fmt_bytes(77312, Locale::PtBR) == "76 KB");
  CHECK(fmt_bytes(512, Locale::En) == "1 KB");
  CHECK(fmt_bytes(1572864, Locale::PtBR) == "1,5 MB");
  CHECK(fmt_bytes(2 * 1024 * 1024, Locale::En) == "2 MB");
  CHECK(interval_label(14400) == "4h");
  CHECK(interval_label(7) == "7s");
  CHECK(iso_utc(1'700'000'000) == "2023-11-14T22:13:20Z");
  const CsvMeta ms = guess_meta("timestamp,open\n1700000000000,1\n1700000300000,1\n", CsvMeta{});
  CHECK(ms.timestamp_unit == "ms");
  CHECK(ms.interval_seconds == 300);
  const CsvMeta s = guess_meta("\"timestamp\",open\r\n\"1700000000\",1\r\n1700086400,1\r\n", CsvMeta{});
  CHECK(s.timestamp_unit == "s");
  CHECK(s.interval_seconds == 86400);
  CHECK(guess_meta("timestamp,open\n2024-01-01,1\n", CsvMeta{}) == CsvMeta{});  // ISO dates: nothing to guess
  CsvMeta named;
  CHECK(default_label(named, "binance-btc.csv") == "binance-btc \xC2\xB7 1h");
  named.pair = "BTC/USDT";
  CHECK(default_label(named, "x.csv") == "BTC/USDT \xC2\xB7 1h");
  named.pair = std::string(70, 'a');
  CHECK(default_label(named, "x.csv") == std::string(60, 'a'));
}

TEST_CASE("catalog screen: loading, then entries with their installed / partial / missing states") {
  Fixture f;
  CHECK(f.packs.log == std::vector<std::string>{"refresh"});
  f.vm.open_catalog();
  CHECK(f.vm.screen() == Screen::Catalog);
  CHECK(f.packs.log.back() == "catalog");
  f.vm.on_pack_event(catalog_event(CatalogStatus::Loading, {}));
  ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kCatalogState) == "Carregando o catálogo…");
  f.vm.on_pack_event(catalog_event(CatalogStatus::Ready, {entry("a", "da", 77312), entry("b", "db", 2 * 1024 * 1024), entry("c", "dc", 1000)}));
  f.vm.on_pack_event(library_event({storage::StoredPack{"da", "csv-a", "Mercado a"}}, {{"db", 65536}}));
  m = f.vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kCatalogState, true));
  const WidgetItems* items = m.items_of(names::kCatalogList);
  REQUIRE(items);
  CHECK(items->items == std::vector<std::string>{"Mercado a  \xE2\x9C\x93", "Mercado b  \xE2\x80\xA6", "Mercado c"});
  // Installed: play.
  CHECK(m.text_of(names::kCatalogEntryTitle) == "Mercado a  [INSTALADO]");
  CHECK(m.text_of(names::kCatalogEntryTags) == "[SINTÉTICO] · 1h · 4.740 barras · Alavancagem máx. 5x · 76 KB");
  CHECK(m.is_visible(names::kCatalogPlay, false));
  CHECK_FALSE(m.is_visible(names::kCatalogDownload, true));
  f.vm.catalog_play();
  CHECK(f.engine.log.back() == "start-pack da 7");
  // Partial: resume with what arrived.
  f.vm.catalog_select(1);
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kCatalogDownload) == "Retomar download (64 KB de 2 MB)");
  f.vm.catalog_download();
  CHECK(f.packs.log.back() == "download b Mercado b");
  // Not downloaded: download with its size.
  f.vm.catalog_select(2);
  CHECK(f.vm.screen_model().text_of(names::kCatalogDownload) == "Baixar (1 KB)");
}

TEST_CASE("catalog screen: a download's progress, pause, refusal and the next attempt's text") {
  Fixture f;
  f.vm.open_catalog();
  f.vm.on_pack_event(catalog_event(CatalogStatus::Ready, {entry("b", "db", 2 * 1024 * 1024)}));
  f.vm.catalog_download();
  f.vm.on_pack_event(download_event("db", DownloadPhase::Downloading, 1024 * 1024, 2 * 1024 * 1024));
  ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kCatalogDownloadState) == "Baixando… 1 MB de 2 MB");
  CHECK(m.is_visible(names::kCatalogPause, false));
  CHECK_FALSE(m.is_visible(names::kCatalogDownload, true));
  REQUIRE(m.values.size() == 1);
  CHECK(m.values[0].value == doctest::Approx(0.5));
  f.vm.catalog_pause();
  CHECK(f.packs.log.back() == "pause db");
  f.packs.log.clear();
  f.vm.back_to_menu();  // leaving the catalog pauses a running download
  CHECK(f.packs.log == std::vector<std::string>{"pause db"});
  f.vm.open_catalog();
  f.vm.on_pack_event(download_event("db", DownloadPhase::Failed, 1024 * 1024, 2 * 1024 * 1024, "aborted"));
  f.vm.on_pack_event(library_event({}, {{"db", 1024 * 1024}}));
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kCatalogDownloadState) == "Download pausado em 1 MB de 2 MB. Retomar continua de onde parou.");
  CHECK(m.text_of(names::kCatalogDownload) == "Retomar download (1 MB de 2 MB)");
  f.vm.on_pack_event(download_event("db", DownloadPhase::Refused, 2 * 1024 * 1024, 2 * 1024 * 1024, "bad_signature"));
  CHECK(f.vm.screen_model().text_of(names::kCatalogDownloadState).starts_with("Pacote recusado: "));
  f.vm.on_pack_event(download_event("db", DownloadPhase::Installed, 2 * 1024 * 1024, 2 * 1024 * 1024));
  f.vm.on_pack_event(library_event({storage::StoredPack{"db", "csv-b", "Mercado b"}}));
  m = f.vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kCatalogDownloadState, true));
  CHECK(m.is_visible(names::kCatalogPlay, false));
}

TEST_CASE("catalog screen: offline keeps the last catalog with downloads disabled; unavailable offers a retry") {
  Fixture f;
  f.vm.open_catalog();
  f.vm.on_pack_event(catalog_event(CatalogStatus::Offline, {entry("a", "da", 1000)}));
  ScreenModel m = f.vm.screen_model();
  CHECK(m.text_of(names::kCatalogState).starts_with("[OFFLINE] O catálogo e os downloads precisam de conexão."));
  CHECK(m.is_visible(names::kCatalogCached, false));
  CHECK(m.is_visible(names::kCatalogNeedsConnection, false));
  CHECK_FALSE(m.is_visible(names::kCatalogDownload, true));
  f.vm.catalog_download();
  CHECK(f.packs.log.back() == "catalog");  // nothing sent
  f.vm.on_pack_event(catalog_event(CatalogStatus::Unavailable, {}));
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kCatalogState) == "Catálogo indisponível agora. Os pacotes já instalados continuam jogáveis.");
  CHECK(m.is_visible(names::kCatalogRetry, false));
  f.vm.catalog_retry();
  CHECK(f.packs.log.back() == "catalog");
}

TEST_CASE("installed packs: practice, remove, and a pack whose file is gone") {
  Fixture f;
  f.vm.open_catalog();
  f.vm.on_pack_event(library_event({storage::StoredPack{"d1", "csv-1", "BTC · 1h"}, storage::StoredPack{"d2", "csv-2", "ETH · 4h"}}, {}, {"d2"}));
  ScreenModel m = f.vm.screen_model();
  CHECK(m.items_of(names::kLibraryList)->items[1] == "ETH · 4h  [CONHECIDO]  [NÃO ESTÁ NO DISPOSITIVO]");
  CHECK_FALSE(m.is_visible(names::kLibraryMissing, true));
  f.vm.library_select(1);
  m = f.vm.screen_model();
  CHECK(m.is_visible(names::kLibraryMissing, false));
  CHECK_FALSE(m.is_enabled(names::kLibraryPlay));
  f.vm.library_play();
  CHECK(f.engine.log.empty());  // a missing file is never played
  f.vm.library_remove();
  CHECK(f.packs.log.back() == "remove d2");
  f.vm.library_select(0);
  f.vm.library_play();
  CHECK(f.engine.log.back() == "start-pack d1 7");
}

TEST_CASE("csv import: file, declared data, the engine's report with gaps, then a private pack") {
  Fixture f;
  TempDir dir;
  const auto path = dir.path() / "btc.csv";
  {
    std::ofstream out(path);
    out << testing::make_csv(60, 1'700'000'000, 900);
  }
  f.vm.open_import();
  CHECK(f.vm.screen() == Screen::Import);
  ScreenModel m = f.vm.screen_model();
  CHECK_FALSE(m.is_visible(names::kCsvMeta, true));
  f.vm.import_set(ImportField::Path, (dir.path() / "nope.csv").string());
  f.vm.import_open_file();
  CHECK(f.vm.screen_model().text_of(names::kCsvError) == "Não foi possível ler o arquivo: confira o caminho.");
  f.vm.import_set(ImportField::Path, path.string());
  f.vm.import_open_file();
  m = f.vm.screen_model();
  CHECK(m.is_visible(names::kCsvMeta, false));
  CHECK(m.text_of(names::kCsvFileName) == "btc.csv");
  CHECK(m.items_of(names::kCsvInterval)->selected == 2);  // 15m guessed from the rows
  f.vm.import_set(ImportField::Pair, "BTC/USDT");
  f.vm.import_set(ImportField::Leverage, "3");
  f.vm.import_choose(ImportChoice::VolumeUnit, 1);
  CHECK(f.vm.screen_model().text_of(names::kCsvNameLabel) == "Nome no menu (BTC/USDT \xC2\xB7 15m)");
  f.vm.import_validate();
  CHECK(f.packs.log.back() == "validate");
  CHECK(f.packs.meta.find(R"("pair":"BTC/USDT")") != std::string::npos);
  CHECK(f.packs.meta.find(R"("leverageCap":3)") != std::string::npos);
  CHECK(f.packs.meta.find(R"("volumeUnit":"quote")") != std::string::npos);
  CHECK(f.vm.screen_model().is_visible(names::kCsvWorking, false));

  PackEvent report;
  report.kind = PackEvent::Kind::CsvReport;
  report.report_json = R"({"ok":true,"rows":60,"errors":[],"errorCount":0,"gaps":[{"line":12,"missing":2}],"gapCount":1,)"
                       R"("hasFunding":false,"firstTime":1700000000,"lastTime":1700053100})";
  f.vm.on_pack_event(report);
  m = f.vm.screen_model();
  const WidgetLines* lines = m.lines_of(names::kCsvReport);
  REQUIRE(lines);
  CHECK(lines->lines[0].text == "\xE2\x9C\x94 Arquivo válido.");
  CHECK(lines->lines[1].text == "60 barras · De 2023-11-14T22:13:20Z a 2023-11-15T12:58:20Z · sem funding (taxa zero)");
  CHECK(lines->lines[2].text == "\xE2\x9A\xA0 1 lacuna: barras ausentes não são inventadas; o preço salta sobre ela.");
  CHECK(lines->lines[3].text == "\xE2\x80\xA2 linha 12: 2 barras ausentes");
  CHECK(m.is_visible(names::kCsvGapRow, false));
  CHECK(m.items_of(names::kCsvGapFill)->items.size() == 3);
  CHECK(m.is_visible(names::kCsvCreate, false));
  CHECK_FALSE(m.is_enabled(names::kCsvPair));  // the declared data are fixed on the report
  f.vm.import_choose(ImportChoice::GapFill, 2);
  f.vm.import_create();
  CHECK(f.packs.log.back() == "import BTC/USDT \xC2\xB7 15m");
  CHECK(f.packs.meta.find(R"("gapFill":"carryForward")") != std::string::npos);
  PackEvent done;
  done.kind = PackEvent::Kind::CsvImported;
  done.ok = true;
  f.vm.on_pack_event(done);
  CHECK(f.vm.screen() == Screen::Menu);
  m = f.vm.screen_model();
  CHECK(m.text_of(names::kMenuNotice) == "Pacote instalado: BTC/USDT \xC2\xB7 15m");
}

TEST_CASE("csv import: errors with their lines, a refused declared field, and the back step") {
  Fixture f;
  TempDir dir;
  const auto path = dir.path() / "bad.csv";
  {
    std::ofstream out(path);
    out << "timestamp,open\n1,2\n";
  }
  f.vm.open_import();
  f.vm.import_set(ImportField::Path, path.string());
  f.vm.import_open_file();
  f.vm.import_validate();
  PackEvent meta;
  meta.kind = PackEvent::Kind::CsvReport;
  meta.report_json = R"({"ok":false,"metaError":"asset"})";
  f.vm.on_pack_event(meta);
  CHECK_FALSE(f.vm.screen_model().text_of(names::kCsvError).empty());
  f.vm.import_validate();
  PackEvent report;
  report.kind = PackEvent::Kind::CsvReport;
  report.report_json = R"({"ok":false,"rows":1,"errors":[{"line":0,"code":"missing_column:close"},{"line":2,"code":"invalid_number"}],)"
                       R"("errorCount":5,"gaps":[],"gapCount":0,"hasFunding":false,"firstTime":0,"lastTime":0})";
  f.vm.on_pack_event(report);
  ScreenModel m = f.vm.screen_model();
  const WidgetLines* lines = m.lines_of(names::kCsvReport);
  REQUIRE(lines);
  CHECK(lines->lines[0].text == "\xE2\x9C\x96 5 erros — corrija o arquivo ou os dados declarados:");
  CHECK(lines->lines[1].text == "\xE2\x80\xA2 falta a coluna close");
  CHECK(lines->lines[2].text.starts_with("\xE2\x80\xA2 linha 2: número inválido"));
  CHECK(lines->lines[3].text == "… e mais 3");
  CHECK_FALSE(m.is_visible(names::kCsvCreate, true));
  CHECK(m.is_visible(names::kCsvBack, false));
  f.vm.import_back();
  CHECK(f.vm.screen_model().is_visible(names::kCsvValidate, false));
  f.vm.import_cancel();
  CHECK(f.vm.screen() == Screen::Menu);
}
