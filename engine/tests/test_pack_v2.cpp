// Pack schema v2 (spec scenario-packs "Pack manifest", "Pack safety limits"): mission texts and
// objectives, mark-price bars, settlement sub-bars and maintenance-margin tiers, with v1 packs still
// loading byte for byte.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "../src/pack_codec.hpp"
#include "pack_fixtures.hpp"
#include "tradelab/c_api.h"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"
#include "tradelab/options.hpp"

using namespace tradelab;

namespace {

const SecretSeed& seed() {
  static const SecretSeed s = test::seed_of(77);
  return s;
}

std::vector<TrustedKey> keys() {
  const KeyPair k = ed25519_keypair(seed());
  return {{key_id("publisher", k.public_key), k.public_key}};
}

std::string replaced(std::string s, std::string_view from, std::string_view to) {
  const auto pos = s.find(from);
  REQUIRE_MESSAGE(pos != std::string::npos, from);
  return s.replace(pos, from.size(), to);
}

std::vector<std::uint8_t> bytes_of(const std::string& s) { return {s.begin(), s.end()}; }

// Writes a pack from a source: .bin files as stored zstd frames, other files as they are.
std::vector<std::uint8_t> build(const std::string& manifest, const std::vector<PackFile>& raw) {
  std::vector<PackFile> files;
  for (const PackFile& f : raw) {
    const bool bin = f.name.ends_with(".bin");
    files.push_back({f.name, bin ? detail::zstd_store(f.stored) : f.stored, f.stored.size()});
  }
  const KeyPair k = ed25519_keypair(seed());
  return write_pack(manifest, files, k, keys()[0].id);
}

PackOpen open(const std::vector<std::uint8_t>& pack, const PackLimits& limits = {}) {
  return open_pack(pack, keys(), PackIntent::Install, limits);
}

PackFile raw_file(std::string name, std::vector<std::uint8_t> bytes) {
  const std::uint64_t n = bytes.size();
  return {std::move(name), std::move(bytes), n};
}

// `files` with the file of the same name replaced by `f`.
std::vector<PackFile> replaced_file(std::vector<PackFile> files, const PackFile& f) {
  for (PackFile& x : files) {
    if (x.name == f.name) x = f;
  }
  return files;
}

std::vector<PackText> texts() {
  return {{"pt-BR", "Treino · choque de liquidez", "Um fundo grande sai do livro: o spread abre e a profundidade cai por algumas barras."},
          {"en", "Practice · liquidity shock", "A large fund leaves the book: the spread widens and depth drops for a few bars."}};
}

// m05 (options) with every v2 feature: mark bars one tick above the traded high, 5-minute sub-bars
// tiling the 30-minute settlement window of the first expiry, the mission's tiers and objectives.
struct V2Source {
  Scenario sc;
  std::string manifest;
  std::vector<PackFile> files;
};

V2Source v2_source() {
  V2Source out;
  REQUIRE(build_mission("m05-options-writer", 3, out.sc));
  const PackSource src = mission_pack_source("m05-options-writer", 3);
  REQUIRE(src.ok());
  Scenario& sc = out.sc;
  const Price tick = sc.contract.tick_size;
  sc.mark = sc.bars;
  for (Bar& b : sc.mark) {
    b.high = Price::from_raw(b.high.raw + tick.raw);
    b.volume = Qty{};
    b.synthetic = false;
  }
  const std::uint32_t expiry = sc.options.series.front().expiry_step;
  const Bar& eb = sc.bars[expiry];
  sc.sub_bars = {{expiry, 5, {eb.open, eb.open, eb.low, eb.high, eb.close, eb.close}}};
  out.manifest = replaced(replaced(src.manifest, "\"markBars\":false", "\"markBars\":true"), "\"subBars\":0", "\"subBars\":6");
  out.files = src.files;
  out.files.push_back(raw_file("mark.bin", encode_mark(sc.mark)));
  out.files.push_back(raw_file("subbars.bin", encode_sub_bars(sc.sub_bars)));
  out.files.push_back(raw_file("mission.json", bytes_of(encode_texts(texts()))));
  return out;
}

}  // namespace

TEST_CASE("Pack v2: a generated mission pack carries the mission's tiers and objectives as schema v2") {
  const PackSource src = mission_pack_source("m02-funding-squeeze", 4);
  REQUIRE(src.ok());
  CHECK(src.manifest.find("\"schemaVersion\":2") != std::string::npos);
  const PackOpen p = open(build(src.manifest, src.files));
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  Scenario expected;
  REQUIRE(build_mission("m02-funding-squeeze", 4, expected));
  CHECK(p.info.schema_version == 2);
  REQUIRE(p.scenario.contract.margin_tiers.size() == expected.contract.margin_tiers.size());
  for (std::size_t i = 0; i < expected.contract.margin_tiers.size(); ++i) {
    CHECK(p.scenario.contract.margin_tiers[i].notional_floor == expected.contract.margin_tiers[i].notional_floor);
    CHECK(p.scenario.contract.margin_tiers[i].maintenance_margin == expected.contract.margin_tiers[i].maintenance_margin);
    CHECK(p.scenario.contract.margin_tiers[i].max_leverage == expected.contract.margin_tiers[i].max_leverage);
  }
  REQUIRE(p.scenario.mission.objectives.size() == expected.mission.objectives.size());
  for (std::size_t i = 0; i < expected.mission.objectives.size(); ++i) {
    CHECK(p.scenario.mission.objectives[i].code == expected.mission.objectives[i].code);
    CHECK(p.scenario.mission.objectives[i].value_bp == expected.mission.objectives[i].value_bp);
  }
  CHECK(p.scenario.mission.liq_warning_bp == expected.mission.liq_warning_bp);
  CHECK(p.scenario.mission.drawdown_budget_bp == expected.mission.drawdown_budget_bp);
  CHECK(p.info.texts.empty());
}

TEST_CASE("Pack v2: mark bars, settlement sub-bars and mission texts round-trip into the scenario") {
  const V2Source v = v2_source();
  const PackOpen p = open(build(v.manifest, v.files));
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  REQUIRE(p.scenario.mark.size() == v.sc.mark.size());
  for (std::size_t i = 0; i < v.sc.mark.size(); ++i) {
    CHECK(p.scenario.mark[i].high == v.sc.mark[i].high);
    CHECK(p.scenario.mark[i].close == v.sc.mark[i].close);
  }
  REQUIRE(p.scenario.sub_bars.size() == 1);
  CHECK(p.scenario.sub_bars[0].bar == v.sc.sub_bars[0].bar);
  CHECK(p.scenario.sub_bars[0].minutes == 5);
  CHECK(p.scenario.sub_bars[0].closes == v.sc.sub_bars[0].closes);
  bool approx = true;
  const std::uint32_t expiry = v.sc.sub_bars[0].bar;
  CHECK(settlement_price(p.scenario, expiry, &approx) == settlement_price(v.sc, expiry, &approx));
  CHECK_FALSE(approx);  // the sub-bars tile the 30-minute window
  REQUIRE(p.info.texts.size() == 2);
  CHECK(p.info.texts[0].locale == "pt-BR");
  CHECK(p.info.texts[1].title == "Practice · liquidity shock");

  // The C ABI: the summary names the schema and the texts; the state marks against the mark series.
  const auto pack = build(v.manifest, v.files);
  const KeyPair k = ed25519_keypair(seed());
  const std::string trusted = R"([{"id":")" + keys()[0].id + R"(","key":")" + to_hex(k.public_key) + "\"}]";
  const std::string summary = tl_pack_validate(pack.data(), static_cast<std::uint32_t>(pack.size()), trusted.c_str(), 0);
  CHECK(summary.find("\"schemaVersion\":2") != std::string::npos);
  CHECK(summary.find(R"("texts":[{"locale":"pt-BR","title":"Treino · choque de liquidez")") != std::string::npos);
  tl_run* run = tl_run_create_pack(pack.data(), static_cast<std::uint32_t>(pack.size()), trusted.c_str(), 1);
  REQUIRE(run != nullptr);
  const std::string state = tl_state(run);
  CHECK(state.find("\"markFromLastPrice\":false") != std::string::npos);
  tl_run_destroy(run);
}

TEST_CASE("Pack v2: v1 packs still load, and a v1 manifest cannot use v2 fields or files") {
  // The CSV import writes v1 exactly as before.
  const PackBuild csv = build_csv_pack(test::make_csv(120), test::hourly_meta(), {}, test::seed_of(5));
  REQUIRE(csv.ok());
  const KeyPair dev = ed25519_keypair(test::seed_of(5));
  const std::vector<TrustedKey> device = {{key_id("device", dev.public_key), dev.public_key}};
  const PackOpen p = open_pack(csv.bytes, device, PackIntent::Install);
  REQUIRE(p.ok());
  CHECK(p.info.schema_version == 1);
  CHECK(p.scenario.mission.objectives.size() == common_objectives().size());

  const V2Source v = v2_source();
  const std::string v1 = replaced(v.manifest, "\"schemaVersion\":2", "\"schemaVersion\":1");
  PackOpen r = open(build(v1, v.files));
  CHECK(r.reason == "invalid_field");
  CHECK(r.detail == "marginModel.tiers");
  CHECK(open(build(replaced(v.manifest, "\"schemaVersion\":2", "\"schemaVersion\":3"), v.files)).reason == "unsupported_schema");

  // A v1 manifest (no v2 fields) with a v2 file.
  const PackSource m01 = mission_pack_source("m01-false-breakout", 2);
  std::string plain = m01.manifest;
  const auto rules = plain.find(",\"objectives\":");
  REQUIRE(rules != std::string::npos);
  plain.erase(rules, plain.find('}', plain.find("confirmStopBeyondLiq")) - rules);
  const auto tiers = plain.find(",\"tiers\":");
  plain.erase(tiers, plain.find(']', tiers) + 1 - tiers);
  plain = replaced(replaced(replaced(plain, "\"schemaVersion\":2", "\"schemaVersion\":1"), ",\"markBars\":false", ""), ",\"subBars\":0", "");
  REQUIRE(open(build(plain, m01.files)).ok());
  std::vector<PackFile> with_texts = m01.files;
  with_texts.push_back(raw_file("mission.json", bytes_of(encode_texts(texts()))));
  r = open(build(plain, with_texts));
  CHECK(r.reason == "unsupported_feature");
  CHECK(r.detail == "mission.json");
}

TEST_CASE("Pack v2: limits and a mark.bin bomb are refused before the data is used") {
  const V2Source v = v2_source();
  // Too many sub-bar rows for the limit: refused before anything is decompressed.
  PackLimits small;
  small.max_sub_bars = 5;
  CHECK(open(build(v.manifest, v.files), small).reason == "too_many_sub_bars");

  // mark.bin declares the size of its table but its frame expands to 9 MiB.
  std::vector<PackFile> stored;
  for (const PackFile& f : v.files) {
    const bool bomb = f.name == "mark.bin";
    const auto data = bomb ? std::vector<std::uint8_t>(9U << 20U, 0) : f.stored;
    stored.push_back({f.name, f.name.ends_with(".bin") ? detail::zstd_store(data) : data, f.stored.size()});
  }
  const KeyPair k = ed25519_keypair(seed());
  const PackOpen r = open(write_pack(v.manifest, stored, k, keys()[0].id));
  CHECK(r.reason == "decompression_limit");
  CHECK(r.detail == "mark.bin");
}

TEST_CASE("Pack v2: mission.json over its limit, or not exactly one pt-BR and one en text, is refused") {
  const V2Source v = v2_source();
  const auto open_with = [&v](const std::string& text) {
    return open(build(v.manifest, replaced_file(v.files, raw_file("mission.json", bytes_of(text)))));
  };
  CHECK(open_with(std::string(17'000, ' ') + encode_texts(texts())).reason == "mission_too_large");
  CHECK(open_with(encode_texts({{texts()[0], texts()[0]}})).detail == "mission.json");
  CHECK(open_with(encode_texts({{texts()[0], PackText{"es", "Título", "Texto"}}})).reason == "malformed_data");
  CHECK(open_with(encode_texts({{texts()[0], PackText{"en", std::string(kPackTitleMax + 1, 'x'), "Text"}}})).reason == "malformed_data");
  CHECK(open_with(R"({"texts":[],"extra":1})").reason == "malformed_data");
}

TEST_CASE("Pack v2: hostile mark bars, sub-bars, tiers and objectives are refused with a reason") {
  const V2Source v = v2_source();
  const auto scenario_error = [](ScenarioError e) { return std::to_string(static_cast<int>(e)); };
  // Mark bars that are not bars (high below close), sub-bars that do not end at their bar's close.
  Scenario bad = v.sc;
  bad.mark[10].high = Price::from_raw(bad.mark[10].close.raw - bad.contract.tick_size.raw);
  PackOpen r = open(build(v.manifest, replaced_file(v.files, raw_file("mark.bin", encode_mark(bad.mark)))));
  CHECK(r.reason == "invalid_scenario");
  CHECK(r.detail == scenario_error(ScenarioError::InvalidMark));
  bad = v.sc;
  bad.sub_bars[0].closes.back() = Price::from_raw(bad.sub_bars[0].closes.back().raw + bad.contract.tick_size.raw);
  r = open(build(v.manifest, replaced_file(v.files, raw_file("subbars.bin", encode_sub_bars(bad.sub_bars)))));
  CHECK(r.detail == scenario_error(ScenarioError::InvalidSubBars));

  // The manifest and the files must agree.
  std::vector<PackFile> files = v.files;
  files.erase(std::remove_if(files.begin(), files.end(), [](const PackFile& f) { return f.name == "mark.bin"; }), files.end());
  r = open(build(v.manifest, files));
  CHECK(r.reason == "missing_file");
  CHECK(r.detail == "mark.bin");
  CHECK(open(build(replaced(v.manifest, "\"markBars\":true", "\"markBars\":false"), v.files)).detail == "data.markBars");

  // Objectives and tiers are bounded and checked.
  CHECK(open(build(replaced(v.manifest, "\"code\":\"writer_worst_case\"", "\"code\":\"get_rich\""), v.files)).detail ==
        "missionRules.objectives.code");
  std::string many = "\"tiers\":[";
  for (int i = 0; i < 17; ++i) {
    many += std::string(i == 0 ? "" : ",") + R"({"notionalFloor":")" + std::to_string(50'000 * (i + 1)) +
            R"(","maintenanceMargin":"0.01","maxLeverage":5})";
  }
  const auto at = v.manifest.find("\"tiers\":[");
  REQUIRE(at != std::string::npos);
  std::string too_many = v.manifest;
  too_many.replace(at, too_many.find(']', at) + 1 - at, many + "]");
  CHECK(open(build(too_many, v.files)).detail == "marginModel.tiers");
  r = open(build(replaced(v.manifest, "\"maxLeverage\":20}", "\"maxLeverage\":60}"), v.files));
  CHECK(r.reason == "invalid_scenario");
  CHECK(r.detail == scenario_error(ScenarioError::InvalidContract));
}

TEST_CASE("C ABI version: tl_abi_version reports kAbiVersion") {
  CHECK(tl_abi_version() == kAbiVersion);
  CHECK(kAbiVersion == 1);
}
