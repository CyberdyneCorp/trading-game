#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "model/json.hpp"
#include "model/packs/catalog.hpp"
#include "model/packs/files.hpp"
#include "model/packs/pack_library.hpp"
#include "model/storage/run_recorder.hpp"
#include "model/storage/storage.hpp"
#include "support/storage_fixtures.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::packs;
using tradelab::desktop::testing::hourly_meta;
using tradelab::desktop::testing::make_csv;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::web_catalog_dir;

namespace {

std::vector<std::uint8_t> sample_pack(const std::string& digest) {
  auto bytes = read_file(web_catalog_dir() / "packs" / (digest + ".tlpack"));
  REQUIRE(bytes);
  return *bytes;
}

constexpr const char* kSampleA = "ec9140b18db6c1997d9ebefa494be5d040fc91052236045d2a6336d2263602ee";
constexpr const char* kSampleC = "3090a1aae3e3416923ad8dafa72f0c21bbad9f5d3558116356a8f9f37fed82c7";

std::string read_text(const std::filesystem::path& p) {
  const auto bytes = read_file(p);
  REQUIRE(bytes);
  return std::string(bytes->begin(), bytes->end());
}

}  // namespace

TEST_CASE("packs: a catalog pack signed by the pinned publisher key installs atomically") {
  TempDir dir;
  storage::Storage s(dir.path());
  const auto bytes = sample_pack(kSampleA);
  const PackCheck check = s.packs().install(bytes, "Mercado A", "catalog");
  REQUIRE(check.ok);
  REQUIRE(check.entry);
  CHECK(check.entry->digest == kSampleA);
  CHECK(check.entry->origin == "catalog");
  CHECK(check.entry->mode == "synthetic");
  CHECK(check.entry->total_bars - check.entry->warmup_bars >= 4740);  // the catalog lists the playable bars
  CHECK(std::filesystem::exists(s.packs().pack_path(kSampleA)));
  CHECK_FALSE(std::filesystem::exists(dir.path() / "packs" / (std::string(kSampleA) + ".tlpack.tmp")));
  CHECK(s.store().pack(kSampleA)->size == static_cast<std::int64_t>(bytes.size()));
  CHECK(s.packs().bytes(kSampleA) == bytes);
  CHECK(s.packs().missing().empty());
}

// Market C of the web catalog is a schema-v2 pack: its own mission texts and objectives, and a
// liquidity shock in its story. The loader reports the texts and every run on it carries them.
TEST_CASE("packs: a schema-v2 catalog pack installs with its mission texts and its runs carry them") {
  TempDir dir;
  storage::Storage s(dir.path());
  const auto bytes = sample_pack(kSampleC);
  const PackCheck check = s.packs().install(bytes, "Mercado C", "catalog");
  REQUIRE(check.ok);
  REQUIRE(check.texts.size() == 2);
  CHECK(check.texts[0].locale == "pt-BR");
  CHECK(check.texts[0].title == "Treino · choque de liquidez");
  CHECK(check.texts[1].title == "Practice · liquidity shock");
  storage::RunRecorder rec(s);
  {
    auto run = rec.start_pack(kSampleC, 9);
    REQUIRE(run);
    const Snapshot snap = run->snapshot();
    REQUIRE(snap.pack_texts.size() == 2);
    CHECK(snap.pack_texts[1].briefing.starts_with("A market maker is about to leave the book."));
    CHECK(snap.state["contract"]["liquidityShocks"].as_bool(false));
    CHECK(snap.state["missionRules"]["objectives"].items().size() == 3);
  }
  auto resumed = rec.resume();
  REQUIRE(resumed);
  REQUIRE(resumed->run);
  CHECK(resumed->run->snapshot().pack_texts.size() == 2);
}

TEST_CASE("packs: tampered, truncated and oversized packs are refused with the engine's reason") {
  TempDir dir;
  storage::Storage s(dir.path());
  auto bytes = sample_pack(kSampleA);
  auto tampered = bytes;
  tampered[tampered.size() / 2] ^= 0x01;
  const PackCheck t = s.packs().install(tampered, "x", "catalog");
  CHECK_FALSE(t.ok);
  CHECK_FALSE(t.reason.empty());
  CHECK(t.reason != "none");
  const PackCheck cut = s.packs().install(std::span(bytes).first(bytes.size() / 2), "x", "catalog");
  CHECK_FALSE(cut.ok);
  CHECK_FALSE(cut.reason.empty());
  std::vector<std::uint8_t> huge(kPackMaxBytes + 1, 0);
  CHECK(s.packs().install(huge, "x", "catalog").reason == "too_large");
  CHECK(s.store().packs().empty());
  CHECK(std::filesystem::is_empty(dir.path() / "packs"));
}

TEST_CASE("packs: CSV import builds a private pack signed by this device; another device refuses it") {
  TempDir mine;
  TempDir other;
  storage::Storage a(mine.path());
  storage::Storage b(other.path());
  const std::string csv = make_csv(300);
  const auto report = json::parse(a.packs().validate_csv(csv, hourly_meta()));
  REQUIRE(report);
  CHECK((*report)["ok"].as_bool());
  CHECK((*report)["rows"].as_int() == 300);

  const CsvImportResult r = a.packs().import_csv(csv, hourly_meta(), "BTC/USDT 1h");
  REQUIRE(r.ok);
  CHECK(r.digest.size() == 64);
  const auto entry = a.store().pack(r.digest);
  REQUIRE(entry);
  CHECK(entry->is_private);
  CHECK(entry->origin == "import");
  CHECK(entry->label == "BTC/USDT 1h");

  const auto bytes = a.packs().bytes(r.digest);
  REQUIRE(bytes);
  const PackCheck foreign = b.packs().install(*bytes, "x", "import");  // signed by a key b does not trust
  CHECK_FALSE(foreign.ok);
  CHECK_FALSE(foreign.reason.empty());
}

TEST_CASE("packs: a CSV the engine rejects is reported, nothing is installed") {
  TempDir dir;
  storage::Storage s(dir.path());
  const CsvImportResult few = s.packs().import_csv(make_csv(10), hourly_meta(), "x");
  CHECK_FALSE(few.ok);
  CHECK_FALSE(few.report_json.empty());
  const auto bad_meta = json::parse(s.packs().validate_csv(make_csv(60), R"({"asset":""})"));
  REQUIRE(bad_meta);
  CHECK_FALSE((*bad_meta)["ok"].as_bool(true));
  CHECK(s.store().packs().empty());
}

TEST_CASE("packs: an indexed pack whose file is gone or truncated is reported missing and never read") {
  TempDir dir;
  storage::Storage s(dir.path());
  REQUIRE(s.packs().install(sample_pack(kSampleA), "A", "catalog").ok);
  std::filesystem::resize_file(s.packs().pack_path(kSampleA), 1000);
  CHECK(s.packs().missing() == std::vector<std::string>{kSampleA});
  CHECK_FALSE(s.packs().bytes(kSampleA));
  std::filesystem::remove(s.packs().pack_path(kSampleA));
  CHECK(s.packs().missing() == std::vector<std::string>{kSampleA});
  s.packs().remove(kSampleA);
  CHECK(s.packs().missing().empty());
}

TEST_CASE("packs: the trusted keys are the pinned publisher keys plus this device's") {
  TempDir dir;
  storage::Storage s(dir.path());
  const auto keys = json::parse(s.packs().trusted_keys_json());
  REQUIRE(keys);
  REQUIRE(keys->size() == 2);
  CHECK((*keys)[0]["id"].as_string() == "publisher-4eec1d54ffe7656f");
  CHECK((*keys)[1]["id"].as_string().rfind("device-", 0) == 0);
  CHECK((*keys)[1]["key"].as_string().size() == 64);
  storage::Storage again(dir.path());  // the device seed persists
  CHECK(again.packs().trusted_keys_json() == s.packs().trusted_keys_json());
}

TEST_CASE("packs catalog: the web's static catalog parses with URLs resolved against the manifest") {
  const std::string text = read_text(web_catalog_dir() / "catalog.json");
  const auto c = parse_catalog(text, "https://tradelab.example/catalog/catalog.json");
  REQUIRE(c);
  REQUIRE(c->entries.size() == 3);
  const CatalogEntry& a = c->entries[0];
  CHECK(a.id == "sample-a-1h");
  CHECK(a.url == "https://tradelab.example/catalog/packs/" + std::string(kSampleA) + ".tlpack");
  CHECK(a.size == 77312);
  CHECK(a.bars == 4740);
  CHECK(a.mode == "synthetic");
  CHECK(localized(a.title, "pt-BR") == "Mercado A · 1h");
  CHECK(localized(a.title, "en") == "Market A · 1h");
}

TEST_CASE("packs catalog: wrong schema refuses the catalog, a bad entry is skipped, duplicates collapse") {
  CHECK_FALSE(parse_catalog(R"({"schemaVersion":2,"packs":[]})", "https://x/c.json"));
  CHECK_FALSE(parse_catalog(R"({"packs":[]})", "https://x/c.json"));
  CHECK_FALSE(parse_catalog("[]", "https://x/c.json"));
  const std::string good = R"({"id":"a","file":"/p/a.tlpack","digest":")" + std::string(64, 'a') +
                           R"(","size":10,"scenarioId":"s","keyId":"k","intervalSeconds":60,"bars":100,"leverageCap":2,"title":{"en":"A"}})";
  const std::string bad_digest = R"({"id":"b","file":"b","digest":"XYZ","size":10,"scenarioId":"s","keyId":"k","intervalSeconds":60,"bars":100,"leverageCap":2,"title":{"en":"B"}})";
  const std::string fraction = R"({"id":"c","file":"c","digest":")" + std::string(64, 'c') +
                               R"(","size":1.5,"scenarioId":"s","keyId":"k","intervalSeconds":60,"bars":100,"leverageCap":2,"title":{"en":"C"}})";
  const std::string text = R"({"schemaVersion":1,"packs":[)" + good + "," + bad_digest + "," + fraction + "," + good + "]}";
  const auto c = parse_catalog(text, "https://cdn.example:8443/catalog/c.json");
  REQUIRE(c);
  REQUIRE(c->entries.size() == 1);
  CHECK(c->entries[0].url == "https://cdn.example:8443/p/a.tlpack");
  CHECK(localized(c->entries[0].title, "pt-BR") == "A");  // falls back to the other language
}

TEST_CASE("packs catalog: URL resolution") {
  CHECK(resolve_url("https://h/a/b/c.json", "d.tlpack") == "https://h/a/b/d.tlpack");
  CHECK(resolve_url("https://h/a/b/c.json", "./d.tlpack") == "https://h/a/b/d.tlpack");
  CHECK(resolve_url("https://h/a/b/c.json", "/d.tlpack") == "https://h/d.tlpack");
  CHECK(resolve_url("https://h/a/b/c.json", "//cdn/d.tlpack") == "https://cdn/d.tlpack");
  CHECK(resolve_url("https://h/a/b/c.json", "http://o/d.tlpack") == "http://o/d.tlpack");
}
