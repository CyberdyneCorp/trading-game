#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "../src/pack_codec.hpp"
#include "pack_fixtures.hpp"
#include "tradelab/c_api.h"
#include "tradelab/engine.hpp"

using namespace tradelab;

namespace {

const std::string kBaseManifest = R"({"schemaVersion":1,"scenarioId":"hist-test","packVersion":1,"mode":"historical","private":false,
"engineCompatibility":{"min":"0.1.0","max":"0.1.0"},"executionPolicyVersion":10,"scoringVersion":3,
"timeframe":"1h","stepSeconds":3600,"displayPrecision":2,"aliasPolicy":"per-run",
"instruments":{"perpetual":{"multiplier":"1","tickSize":"0.01","lotSize":"0.1","minQty":"0.1","maxLeverage":50}},
"costModel":{"makerFee":"0.0002","takerFee":"0.0005","halfSpread":"0.0001","slippage":"0.0002"},
"liquidityModel":{"capacityFraction":"0.1"},
"fundingModel":{"intervalSteps":8,"source":"none"},
"ivModel":{"kind":"none"},
"marginModel":{"maintenanceMargin":"0.005"},
"missionRules":{"initialBalance":"10000","leverageCap":5,"warmupBars":30,"allowShort":true},
"data":{"bars":100,"gaps":0},
"files":[],"signingKeyId":"",
"provenance":{"source":"test"},
"license":{"name":"CC-BY-4.0","attribution":"Test Exchange","transformation":true,"commercial":false,"offlineRedistribution":false},
"distribution":{"downloadable":true}})";

const KeyPair& publisher() {
  static const KeyPair k = ed25519_keypair(test::seed_of(9));
  return k;
}

std::vector<TrustedKey> trusted() { return {{"publisher-test", publisher().public_key}}; }

std::vector<Bar> flat_bars(std::uint32_t n) {
  std::vector<Bar> bars;
  for (std::uint32_t i = 0; i < n; ++i) {
    const auto c = Price::from_raw((10'000 + static_cast<std::int64_t>(i % 7) * 3) * (Price::kScale / 100));
    bars.push_back({c, Price::from_raw(c.raw + Price::kScale), Price::from_raw(c.raw - Price::kScale), c, Qty::from_raw(500 * Qty::kScale), false});
  }
  return bars;
}

std::string replaced(std::string s, std::string_view from, std::string_view to) {
  const auto pos = s.find(from);
  REQUIRE(pos != std::string::npos);
  return s.replace(pos, from.size(), to);
}

std::vector<std::uint8_t> make_pack(const std::string& manifest, std::vector<PackFile> files = {}) {
  if (files.empty()) {
    const auto raw = encode_bars(flat_bars(100));
    files.push_back({"bars.bin", detail::zstd_store(raw), raw.size()});
  }
  return write_pack(manifest, files, publisher(), "publisher-test");
}

// Offset of a member's data inside the archive.
std::size_t data_offset(const std::vector<std::uint8_t>& pack, std::string_view name) {
  std::vector<detail::TarEntry> entries;
  REQUIRE(detail::tar_read(pack, 8, &entries) == detail::TarStatus::Ok);
  for (const auto& e : entries) {
    if (e.name == name) return static_cast<std::size_t>(e.data.data() - pack.data());
  }
  FAIL("no such member");
  return 0;
}

std::string reason_of(const std::vector<std::uint8_t>& pack, PackIntent intent = PackIntent::Install) {
  return open_pack(pack, trusted(), intent).reason;
}

}  // namespace

TEST_CASE("pack: a signed pack opens into a validated scenario") {
  const auto pack = make_pack(kBaseManifest);
  const PackOpen p = open_pack(pack, trusted(), PackIntent::Install);
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  CHECK(p.scenario.id == "hist-test");
  CHECK(p.scenario.bars.size() == 100);
  CHECK(p.scenario.mission.warmup_bars == 30);
  CHECK(p.scenario.funding_rate.size() == 100);
  CHECK(p.info.signing_key_id == "publisher-test");
  CHECK(p.info.digest.size() == 64);
  CHECK(make_pack(kBaseManifest) == pack);  // deterministic bytes
}

TEST_CASE("pack: one changed data byte is refused with hash_mismatch (DATA02)") {
  auto pack = make_pack(kBaseManifest);
  pack[data_offset(pack, "bars.bin") + 40] ^= 0x01U;
  CHECK(reason_of(pack) == "hash_mismatch");
  CHECK(open_pack(pack, trusted(), PackIntent::Install).detail == "bars.bin");
}

TEST_CASE("pack: manifest edits break the signature; unknown keys are refused") {
  auto pack = make_pack(kBaseManifest);
  const std::size_t at = data_offset(pack, "manifest.json");
  const std::string text(pack.begin() + static_cast<std::ptrdiff_t>(at), pack.begin() + static_cast<std::ptrdiff_t>(at + 2000));
  const auto pos = text.find("\"leverageCap\":5");
  REQUIRE(pos != std::string::npos);
  pack[at + pos + 14] = '9';  // same length, header checksum unaffected (data is not checksummed by tar)
  CHECK(reason_of(pack) == "bad_signature");
  CHECK(open_pack(make_pack(kBaseManifest), {}, PackIntent::Install).reason == "untrusted_key");
}

TEST_CASE("pack: a missing model is named in the error") {
  const auto pack = make_pack(replaced(kBaseManifest, "\"fundingModel\":{\"intervalSteps\":8,\"source\":\"none\"},", ""));
  const PackOpen p = open_pack(pack, trusted(), PackIntent::Install);
  CHECK(p.reason == "missing_field");
  CHECK(p.detail == "fundingModel");
  CHECK(open_pack(make_pack(replaced(kBaseManifest, "\"takerFee\":\"0.0005\",", "")), trusted(), PackIntent::Install).detail ==
        "costModel.takerFee");
  // A float where a decimal string belongs is outside the JSON subset (write_pack would refuse it).
  const std::string floaty = replaced(kBaseManifest, "\"tickSize\":\"0.01\"", "\"tickSize\":0.01");
  const auto raw = detail::tar_write({{"manifest.json", {floaty.begin(), floaty.end()}}, {"manifest.sig", std::vector<std::uint8_t>(64)}});
  CHECK(reason_of(raw) == "manifest_invalid");
}

TEST_CASE("pack: engine compatibility range is enforced") {
  CHECK(reason_of(make_pack(replaced(kBaseManifest, "\"min\":\"0.1.0\",\"max\":\"0.1.0\"", "\"min\":\"0.2.0\",\"max\":\"0.3.0\""))) ==
        "incompatible_engine");
  CHECK(reason_of(make_pack(replaced(kBaseManifest, "\"executionPolicyVersion\":10", "\"executionPolicyVersion\":999"))) ==
        "incompatible_engine");
}

TEST_CASE("pack: limits, decompression bombs and unexpected files") {
  const auto pack = make_pack(kBaseManifest);
  PackLimits small;
  small.max_pack_bytes = pack.size() - 1;
  CHECK(open_pack(pack, trusted(), PackIntent::Install, small).reason == "too_large");

  // bars.bin declares the right decompressed size for 100 bars, but its frame expands to 9 MiB.
  std::vector<std::uint8_t> zeros(9U * 1024U * 1024U, 0);
  const auto bomb = detail::zstd_store(zeros);
  CHECK(reason_of(make_pack(kBaseManifest, {{"bars.bin", bomb, 16 + 40 * 100}})) == "decompression_limit");
  CHECK(reason_of(make_pack(kBaseManifest, {{"bars.bin", bomb, zeros.size()}})) == "decompression_limit");  // above the cap

  const auto raw = encode_bars(flat_bars(100));
  // mission.json and i18n.json stay reserved; options.bin needs the option parameters in the manifest.
  const std::vector<PackFile> with_mission = {{"bars.bin", detail::zstd_store(raw), raw.size()}, {"mission.json", {'{', '}'}, 2}};
  CHECK(reason_of(make_pack(kBaseManifest, with_mission)) == "unsupported_feature");
  const std::vector<PackFile> with_options = {{"bars.bin", detail::zstd_store(raw), raw.size()}, {"options.bin", {1}, 1}};
  CHECK(reason_of(make_pack(kBaseManifest, with_options)) == "missing_field");
  CHECK(reason_of(make_pack(replaced(kBaseManifest, "\"bars\":100", "\"bars\":99"))) == "malformed_data");
}

TEST_CASE("pack: publishing needs offline rights when downloadable (LIC01)") {
  const auto pack = make_pack(kBaseManifest);
  CHECK(reason_of(pack) == "");  // installing is fine
  const PackOpen p = open_pack(pack, trusted(), PackIntent::Publish);
  CHECK(p.reason == "license_incomplete");
  CHECK(p.detail == "license.offlineRedistribution");
  const auto cleared = make_pack(replaced(kBaseManifest, "\"offlineRedistribution\":false", "\"offlineRedistribution\":true"));
  CHECK(reason_of(cleared, PackIntent::Publish) == "");
  const auto no_attribution = replaced(replaced(kBaseManifest, "\"offlineRedistribution\":false", "\"offlineRedistribution\":true"),
                                       "\"attribution\":\"Test Exchange\"", "\"attribution\":\"\"");
  CHECK(reason_of(make_pack(no_attribution), PackIntent::Publish) == "license_incomplete");
}

TEST_CASE("pack: CSV import builds a private, masked, device-signed pack") {
  const SecretSeed device = test::seed_of(42);
  const PackBuild b = build_csv_pack(test::make_csv(200, 1'700'000'000, 3600, 30'000.0, true), test::hourly_meta(), {}, device);
  REQUIRE_MESSAGE(b.ok(), b.reason);
  const PublicKey pub = ed25519_keypair(device).public_key;
  const std::vector<TrustedKey> keys = {{key_id("device", pub), pub}};
  const PackOpen p = open_pack(b.bytes, keys, PackIntent::Install);
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  CHECK(p.info.private_pack);
  CHECK(p.info.scenario_id == b.scenario_id);
  CHECK(p.scenario.mission.warmup_bars == 50);  // a quarter of 200 rows
  CHECK(p.scenario.bars[50].open == Price::from_raw(100 * Price::kScale));
  CHECK(p.scenario.contract.funding_interval == 8);
  CHECK(p.scenario.funding_rate[0] == Rate::from_raw(1'000'000));
  CHECK(p.info.reveal.pair == "BTC/USDT");
  CHECK(p.info.reveal.period_from == "2023-11-14T22:13:20Z");
  CHECK(open_pack(b.bytes, keys, PackIntent::Publish).reason == "private_pack");
  CHECK(build_csv_pack(test::make_csv(200, 1'700'000'000, 3600, 30'000.0, true), test::hourly_meta(), {}, device).bytes == b.bytes);
  CHECK(build_csv_pack(test::make_csv(10), test::hourly_meta(), {}, device).reason == "csv_invalid");
  ImportRules bad;
  bad.leverage_cap = 50;
  CHECK(build_csv_pack(test::make_csv(200), test::hourly_meta(), bad, device).reason == "invalid_rules");
}

TEST_CASE("pack C ABI: import, play to the end, debrief reveal, checkpoint restore") {
  const std::string seed_hex(64, 'a');
  const std::string meta = R"({"asset":"BTC","pair":"BTC/USDT","venue":"test","volumeUnit":"base","intervalSeconds":3600,"timestampUnit":"s","leverageCap":3})";
  const std::string csv = test::make_csv(120);
  CHECK(std::string(tl_csv_validate(csv.c_str(), meta.c_str())).find("\"ok\":true") != std::string::npos);
  CHECK(std::string(tl_csv_validate(csv.c_str(), "{}")).find("\"metaError\":\"volumeUnit\"") != std::string::npos);

  const std::string built = tl_pack_build_csv(csv.c_str(), meta.c_str(), seed_hex.c_str());
  REQUIRE(built.find("\"ok\":true") != std::string::npos);
  const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const std::string key = tl_device_key(seed_hex.c_str());
  const std::string pub = key.substr(key.find("publicKey\":\"") + 12, 64);
  const std::string id = key.substr(key.find("\"id\":\"") + 6, 23);
  const std::string keys = "[{\"id\":\"" + id + "\",\"key\":\"" + pub + "\"}]";

  const std::string summary = tl_pack_validate(pack.data(), static_cast<uint32_t>(pack.size()), keys.c_str(), 0);
  CHECK(summary.find("\"ok\":true") != std::string::npos);
  CHECK(summary.find("\"leverageCap\":3") != std::string::npos);
  CHECK(summary.find("BTC") == std::string::npos);  // no reveal before the debrief
  CHECK(std::string(tl_pack_validate(pack.data(), static_cast<uint32_t>(pack.size()), "[]", 0)).find("untrusted_key") != std::string::npos);

  tl_run* run = tl_run_create_pack(pack.data(), static_cast<uint32_t>(pack.size()), keys.c_str(), 7);
  REQUIRE(run != nullptr);
  const std::string state = tl_state(run);
  CHECK(state.find("\"knownScenario\":true") != std::string::npos);
  CHECK(state.find("BTC") == std::string::npos);
  CHECK(std::string(tl_debrief(run)) == "{\"error\":\"run_active\"}");
  tl_place_order(run, 1, 0, 0, "1", nullptr, 0);
  tl_advance(run, 10);
  const std::string cp = tl_checkpoint(run);
  const std::string hex_cp = cp.substr(cp.find(":\"") + 2, cp.find("\",") - cp.find(":\"") - 2);
  std::string digest_before = tl_state(run);
  digest_before = digest_before.substr(digest_before.find("\"digest\""), 80);
  tl_run* restored = tl_run_restore_pack(pack.data(), static_cast<uint32_t>(pack.size()), keys.c_str(), 7, hex_cp.c_str());
  REQUIRE(restored != nullptr);
  CHECK(std::string(tl_state(restored)).find(digest_before) != std::string::npos);
  CHECK(tl_run_restore("m01-false-breakout", 7, hex_cp.c_str()) == nullptr);  // another scenario

  tl_advance(run, 1000);
  const std::string debrief = tl_debrief(run);
  CHECK(debrief.find("\"syntheticMarket\":false") != std::string::npos);
  CHECK(debrief.find("\"pair\":\"BTC/USDT\"") != std::string::npos);
  CHECK(debrief.find("\"knownScenario\":true") != std::string::npos);
  tl_run_destroy(restored);
  tl_run_destroy(run);
}

namespace {

// Same text as bindings/python/tests/test_packs.py golden_csv(): prices in integer tenths.
std::string golden_csv(int step_seconds = 3600) {
  const auto tenths = [](int v) { return std::to_string(v / 10) + "." + std::to_string(v % 10); };
  std::string out = "timestamp,open,high,low,close,volume,funding\n";
  for (int i = 0; i < 150; ++i) {
    const int open = 1000 + (i * 37) % 50;
    const int close = open + (i * 13) % 21 - 10;
    out += std::to_string(1'700'000'000 + i * step_seconds) + "," + tenths(open) + "," + tenths(std::max(open, close) + 5) + "," +
           tenths(std::min(open, close) - 5) + "," + tenths(close) + "," + std::to_string(1000 + i) + (i % 8 == 0 ? ",0.0001\n" : ",\n");
  }
  return out;
}

std::string json_text(const std::string& json, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":\"";
  const auto at = json.find(needle) + needle.size();
  return json.substr(at, json.find('"', at) - at);
}

}  // namespace

TEST_CASE("Golden: scripted run on an imported CSV pack (cross-platform, mirrored in Python)") {
  const std::string meta =
      R"({"asset":"X","pair":"X/USD","venue":"v","volumeUnit":"base","intervalSeconds":3600,"timestampUnit":"s","leverageCap":5})";
  const std::string seed_hex(64, '7');
  const std::string built = tl_pack_build_csv(golden_csv().c_str(), meta.c_str(), seed_hex.c_str());
  REQUIRE(built.find("\"ok\":true") != std::string::npos);
  const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const std::string key = tl_device_key(seed_hex.c_str());
  const std::string keys = "[{\"id\":\"" + json_text(key, "id") + "\",\"key\":\"" + json_text(key, "publicKey") + "\"}]";
  tl_run* run = tl_run_create_pack(pack.data(), static_cast<uint32_t>(pack.size()), keys.c_str(), 2026);
  REQUIRE(run != nullptr);
  tl_set_leverage(run, 1, 3);
  tl_place_order(run, 2, 0, 0, "10", nullptr, 0);
  tl_advance(run, 20);
  tl_place_order(run, 3, 0, 1, "25", nullptr, 0);
  tl_advance(run, 30);
  tl_close_position(run, 4);
  const std::string state = tl_advance(run, 1000);
  tl_run_destroy(run);
  CHECK(json_text(state, "digest") == "0980f8b127e05a58d4e3aae3145db0d7664fb0be853654fc90b9862d0055a834");
  CHECK(json_text(state, "eventsDigest") == "e3e42dedf85efb5b7fa7b55835e1998d82b3c552100f3c017214b6b585d382da");
}

TEST_CASE("Regression: the state names the bar interval (an imported 15-minute market is not '1h')") {
  const std::string meta =
      R"({"asset":"X","pair":"X/USD","venue":"v","volumeUnit":"base","intervalSeconds":900,"timestampUnit":"s","leverageCap":5})";
  const std::string seed_hex(64, '7');
  REQUIRE(std::string(tl_pack_build_csv(golden_csv(900).c_str(), meta.c_str(), seed_hex.c_str())).find("\"ok\":true") != std::string::npos);
  const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const std::string key = tl_device_key(seed_hex.c_str());
  const std::string keys = "[{\"id\":\"" + json_text(key, "id") + "\",\"key\":\"" + json_text(key, "publicKey") + "\"}]";
  tl_run* run = tl_run_create_pack(pack.data(), static_cast<uint32_t>(pack.size()), keys.c_str(), 1);
  REQUIRE(run != nullptr);
  CHECK(std::string(tl_state(run)).find("\"stepSeconds\":900,") != std::string::npos);
  tl_run_destroy(run);
  tl_run* mission = tl_run_create("m01-false-breakout", 1);
  CHECK(std::string(tl_state(mission)).find("\"stepSeconds\":3600,") != std::string::npos);
  tl_run_destroy(mission);
}
