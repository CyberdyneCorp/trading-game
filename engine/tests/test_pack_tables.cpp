// events.bin and options.bin (spec scenario-packs "Pack manifest", "Pack safety limits"), branch
// packs (spec market-generator "Branch labeling") and the packer's engine side: pack sources,
// writing a pack from stored files and publisher re-signing (design D5).

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "../src/c_api_run.hpp"
#include "../src/pack_codec.hpp"
#include "pack_fixtures.hpp"
#include "tradelab/c_api.h"
#include "tradelab/engine.hpp"
#include "tradelab/generator.hpp"

using namespace tradelab;

namespace {

const SecretSeed& publisher_seed() {
  static const SecretSeed s = test::seed_of(42);
  return s;
}

std::vector<TrustedKey> publisher_keys() {
  const KeyPair k = ed25519_keypair(publisher_seed());
  return {{key_id("publisher", k.public_key), k.public_key}};
}

// The source's data files as stored zstd frames, in an archive (what the packer hands the engine).
std::vector<std::uint8_t> stored_archive(const PackSource& src) {
  std::vector<detail::TarFile> files;
  for (const PackFile& f : src.files) {
    const bool bin = f.name.ends_with(".bin");
    files.push_back({f.name, bin ? detail::zstd_store(f.stored) : f.stored});
  }
  return detail::tar_write(files);
}

std::vector<std::uint8_t> build(const PackSource& src, const std::string& manifest = {}) {
  REQUIRE_MESSAGE(src.ok(), src.reason);
  const PackBuild b = assemble_pack(manifest.empty() ? src.manifest : manifest, stored_archive(src), publisher_seed(), "publisher");
  REQUIRE_MESSAGE(b.ok(), b.reason);
  return b.bytes;
}

PackOpen open(const std::vector<std::uint8_t>& pack, const PackLimits& limits = {}) {
  return open_pack(pack, publisher_keys(), PackIntent::Install, limits);
}

std::string replaced(std::string s, std::string_view from, std::string_view to) {
  const auto pos = s.find(from);
  REQUIRE(pos != std::string::npos);
  return s.replace(pos, from.size(), to);
}

// A source whose file `name` is replaced by `bytes` (still raw; stored as a frame by build()).
PackSource with_file(PackSource src, std::string_view name, std::vector<std::uint8_t> bytes) {
  for (PackFile& f : src.files) {
    if (f.name == name) {
      f.decompressed_size = bytes.size();
      f.stored = bytes;
    }
  }
  return src;
}

bool same_news(const NewsItem& a, const NewsItem& b) {
  return a.release_step == b.release_step && a.kind == b.kind && a.tone == b.tone && a.credibility == b.credibility &&
         a.expectation == b.expectation && a.actual == b.actual && a.announce_lead == b.announce_lead;
}

void check_same_scenario(const Scenario& a, const Scenario& b) {
  REQUIRE(a.bars.size() == b.bars.size());
  for (std::size_t i = 0; i < a.bars.size(); ++i) {
    const Bar& x = a.bars[i];
    const Bar& y = b.bars[i];
    CHECK((x.open == y.open && x.high == y.high && x.low == y.low && x.close == y.close && x.volume == y.volume && x.synthetic == y.synthetic &&
           x.continuity == y.continuity));
  }
  CHECK(a.funding_rate == b.funding_rate);
  REQUIRE(a.news.size() == b.news.size());
  for (std::size_t i = 0; i < a.news.size(); ++i) CHECK(same_news(a.news[i], b.news[i]));
  REQUIRE(a.options.series.size() == b.options.series.size());
  for (std::size_t i = 0; i < a.options.series.size(); ++i) {
    const OptionSeries& x = a.options.series[i];
    const OptionSeries& y = b.options.series[i];
    CHECK((x.id == y.id && x.kind == y.kind && x.strike == y.strike && x.list_step == y.list_step && x.expiry_step == y.expiry_step));
  }
  CHECK(a.options.iv_base == b.options.iv_base);
  CHECK(a.options.spread == b.options.spread);
  CHECK(a.options.skew == b.options.skew);
  CHECK(a.options.allow_short == b.options.allow_short);
  CHECK(a.options.settlement_minutes == b.options.settlement_minutes);
  CHECK(a.contract.funding_interval == b.contract.funding_interval);
  CHECK(a.costs.capacity_fraction == b.costs.capacity_fraction);
  CHECK(a.mission.leverage_cap == b.mission.leverage_cap);
}

std::string json_at(const std::string& json, const std::string& key) {
  const auto at = json.find("\"" + key + "\":\"");
  REQUIRE(at != std::string::npos);
  const auto start = at + key.size() + 4;
  return json.substr(start, json.find('"', start) - start);
}

}  // namespace

TEST_CASE("Pack tables: every mission round-trips through events.bin and options.bin") {
  for (const MissionInfo& m : mission_catalog()) {
    CAPTURE(m.id);
    const PackOpen p = open(build(mission_pack_source(m.id, 7)));
    REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
    Scenario expected;
    REQUIRE(build_mission(m.id, 7, expected));
    check_same_scenario(p.scenario, expected);
    CHECK(p.info.mode == "synthetic");
  }
}

TEST_CASE("Pack tables: a run on a generated pack replays the mission run digest for digest") {
  const auto pack = build(mission_pack_source("m09-event-straddle", 11));
  const std::string keys = "[{\"id\":\"" + publisher_keys()[0].id + "\",\"key\":\"" + to_hex(publisher_keys()[0].key) + "\"}]";
  tl_run* from_pack = tl_run_create_pack(pack.data(), static_cast<std::uint32_t>(pack.size()), keys.c_str(), 11);
  tl_run* mission = tl_run_create("m09-event-straddle", 11);
  REQUIRE(from_pack != nullptr);
  REQUIRE(mission != nullptr);
  for (tl_run* r : {from_pack, mission}) {
    tl_place_option_order(r, 1, 6, 0, 0, "1", nullptr);  // buy a put of the first expiry
    tl_place_order(r, 2, 0, 0, "3", nullptr, 0);
    tl_advance(r, 40);
    tl_close_position(r, 3);
  }
  const std::string a = tl_advance(from_pack, 10'000);
  const std::string b = tl_advance(mission, 10'000);
  CHECK(json_at(a, "digest") == json_at(b, "digest"));
  CHECK(json_at(a, "eventsDigest") == json_at(b, "eventsDigest"));
  tl_run_destroy(from_pack);
  tl_run_destroy(mission);
}

TEST_CASE("Pack tables: limits, bombs and hostile tables are refused with a reason") {
  const PackSource src = mission_pack_source("m11-news-calendar", 3);
  REQUIRE(src.ok());
  const auto pack = build(src);
  PackLimits few;
  few.max_events = 2;
  CHECK(open(pack, few).reason == "too_many_events");
  const auto options_pack = build(mission_pack_source("m10-hedged-book", 3));
  PackLimits small_chain;
  small_chain.max_option_series = 4;
  CHECK(open(options_pack, small_chain).reason == "too_many_series");

  // events.bin declares its real size but its frame expands to 9 MiB.
  const auto events = std::find_if(src.files.begin(), src.files.end(), [](const PackFile& f) { return f.name == "events.bin"; });
  REQUIRE(events != src.files.end());
  std::vector<PackFile> listed;
  for (const PackFile& f : src.files) {
    const bool is_bomb = f.name == "events.bin";
    listed.push_back({f.name, detail::zstd_store(is_bomb ? std::vector<std::uint8_t>(9U << 20U, 0) : f.stored), f.decompressed_size});
  }
  const auto bomb = write_pack(src.manifest, listed, ed25519_keypair(publisher_seed()), publisher_keys()[0].id);
  CHECK(open(bomb).reason == "decompression_limit");
  CHECK(open(bomb).detail == "events.bin");

  // A tone of 5, a wrong magic and a trailing byte are malformed; a kind beyond the catalog is not a valid scenario.
  auto bad = events->stored;
  const std::size_t rows = (bad.size() - 32) / 8;  // two headers; the liquidity table is empty here
  REQUIRE(rows % 7 == 0);
  const std::size_t n = rows / 7;
  bad[16 + 8 * (2 * n)] = 5;  // first tone
  CHECK(open(build(with_file(src, "events.bin", bad))).reason == "malformed_data");
  auto magic = events->stored;
  magic[3] = '9';
  CHECK(open(build(with_file(src, "events.bin", magic))).reason == "malformed_data");
  auto trailing = events->stored;
  trailing.push_back(0);
  CHECK(open(build(with_file(src, "events.bin", trailing))).reason == "malformed_data");
  auto kind = events->stored;
  kind[16 + 8 * n] = 200;  // first kind
  CHECK(open(build(with_file(src, "events.bin", kind))).reason == "invalid_scenario");
}

TEST_CASE("Pack tables: the manifest and the files must agree on events and options") {
  const PackSource src = mission_pack_source("m09-event-straddle", 5);
  REQUIRE(src.ok());
  // Options without their parameters, and parameters without options.bin.
  PackSource no_options = src;
  no_options.files.erase(std::remove_if(no_options.files.begin(), no_options.files.end(), [](const PackFile& f) { return f.name == "options.bin"; }),
                         no_options.files.end());
  PackOpen p = open(build(no_options));
  CHECK(p.reason == "missing_file");
  CHECK(p.detail == "options.bin");
  const std::string manifest = src.manifest;
  const auto at = manifest.find(",\"options\":{");
  REQUIRE(at != std::string::npos);
  const auto end = manifest.find('}', at) + 1;
  const std::string stripped = replaced(replaced(manifest.substr(0, at) + manifest.substr(end), "\"kind\":\"file\"", "\"kind\":\"none\""), "\"optionSeries\":", "\"x\":");
  p = open(build(src, stripped));
  CHECK(p.reason == "missing_field");
  CHECK(p.detail == "instruments.options");
  // Events counted but no events.bin.
  PackSource no_events = src;
  no_events.files.erase(std::remove_if(no_events.files.begin(), no_events.files.end(), [](const PackFile& f) { return f.name == "events.bin"; }),
                        no_events.files.end());
  p = open(build(no_events));
  CHECK(p.reason == "missing_file");
  CHECK(p.detail == "events.bin");
  // mission.json and i18n.json stay reserved.
  PackSource reserved = src;
  reserved.files.push_back({"mission.json", {'{', '}'}, 2});
  CHECK(open(build(reserved)).reason == "unsupported_feature");
}

TEST_CASE("Pack tables: liquidity shocks travel in events.bin and reach execution") {
  Scenario sc;
  REQUIRE(build_mission("m06-thin-book", 2, sc));
  CHECK(sc.liquidity.empty());  // no built-in mission declares spread or capacity channels (design D8.1)
  // A hand-made source: m06's market with a shock over bars 70-74.
  PackSource src = mission_pack_source("m06-thin-book", 2);
  REQUIRE(src.ok());
  sc.liquidity = {{70, 5, 250, 40}};
  src = with_file(src, "events.bin", encode_events(sc.news, sc.liquidity));
  const std::string manifest = replaced(src.manifest, "\"events\":", "\"liquidityShocks\":1,\"events\":");
  const PackOpen p = open(build(src, manifest));
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  REQUIRE(p.scenario.liquidity.size() == 1);
  CHECK(costs_at(p.scenario, 72).half_spread.raw == sc.costs.half_spread.raw * 5 / 2);
  CHECK(costs_at(p.scenario, 75).half_spread == sc.costs.half_spread);
  // A shock that runs past the market is an invalid scenario.
  sc.liquidity = {{70, 5000, 250, 40}};
  CHECK(open(build(with_file(src, "events.bin", encode_events(sc.news, sc.liquidity)), manifest)).reason == "invalid_scenario");
}

TEST_CASE("Branch labeling: every bar after the branch point is synthetic, before it none") {
  CsvMeta meta = test::hourly_meta();
  ImportRules rules;
  rules.warmup_bars = 40;
  const std::string csv = test::make_csv(150);
  const PackSource src = csv_pack_source(csv, meta, rules, false, BranchRequest{90, "m08-regime-switch", 4});
  REQUIRE_MESSAGE(src.ok(), src.reason);
  const PackOpen p = open(build(src));
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  CHECK(p.info.mode == "branch");
  for (std::size_t i = 0; i < p.scenario.bars.size(); ++i) {
    CAPTURE(i);
    CHECK(p.scenario.bars[i].synthetic == (i > 90));
  }
  // The recorded part is the masked history; the branch continues from its close at the alt's pace.
  const PackSource hist = csv_pack_source(csv, meta, rules, false, std::nullopt);
  const PackOpen h = open(build(hist));
  REQUIRE(h.ok());
  for (std::size_t i = 0; i <= 90; ++i) CHECK(h.scenario.bars[i].close == p.scenario.bars[i].close);
  Scenario alt;
  REQUIRE(build_mission("m08-regime-switch", 4, alt));
  const double alt_gap = static_cast<double>(alt.bars[91].open.raw) / static_cast<double>(alt.bars[90].close.raw);
  const double gap = static_cast<double>(p.scenario.bars[91].open.raw) / static_cast<double>(p.scenario.bars[90].close.raw);
  CHECK(std::abs(gap - alt_gap) < 0.0002);  // within a tick at the masked price level

  // The state view carries the flag (6th element of each bar; the 7th is the continuity flag), so
  // clients can label those bars.
  tl_run run;
  run.scenario = p.scenario;
  run.from_pack = true;
  run.pack = p.info;
  run.state = start_run(run.scenario, 1);
  tl_advance(&run, 50);  // step 90: the branch bar is the last visible one
  std::string state = tl_state(&run);
  CHECK(state.find(",false,false]]") != std::string::npos);
  state = tl_advance(&run, 1);
  CHECK(state.find(",true,false]]") != std::string::npos);
}

TEST_CASE("Branch labeling: the manifest names the branch point only in branch mode") {
  CsvMeta meta = test::hourly_meta();
  const std::string csv = test::make_csv(150);
  const PackSource src = csv_pack_source(csv, meta, {}, false, BranchRequest{90, "m08-regime-switch", 4});
  REQUIRE(src.ok());
  PackOpen p = open(build(src, replaced(src.manifest, ",\"branchStep\":90", "")));
  CHECK(p.reason == "missing_field");
  CHECK(p.detail == "data.branchStep");
  p = open(build(src, replaced(src.manifest, "\"mode\":\"branch\"", "\"mode\":\"historical\"")));
  CHECK(p.reason == "invalid_field");
  CHECK(p.detail == "data.branchStep");
  p = open(build(src, replaced(src.manifest, "\"branchStep\":90", "\"branchStep\":149")));
  CHECK(p.reason == "invalid_field");
  // Unknown branch mission, a branch before the first decision and a market longer than the branch.
  CHECK(csv_pack_source(csv, meta, {}, false, BranchRequest{90, "m99", 4}).reason == "unknown_mission");
  CHECK(csv_pack_source(csv, meta, {}, false, BranchRequest{10, "m08-regime-switch", 4}).reason == "invalid_branch");
  CHECK(csv_pack_source(test::make_csv(1000), meta, {}, false, BranchRequest{90, "m08-regime-switch", 4}).reason == "invalid_branch");
}

TEST_CASE("Pack tables: options packs carry the settlement window in minutes (policy v17)") {
  const PackSource src = mission_pack_source("m09-event-straddle", 7);
  REQUIRE(src.ok());
  CHECK(src.manifest.find("\"settlementBars\"") == std::string::npos);
  const PackOpen p = open(build(src, replaced(src.manifest, "\"settlementMinutes\":30", "\"settlementMinutes\":90")));
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  CHECK(p.scenario.options.settlement_minutes == 90);
  CHECK(open(build(src, replaced(src.manifest, "\"settlementMinutes\":30", "\"settlementMinutes\":0"))).detail ==
        "instruments.options.settlementMinutes");
}

TEST_CASE("Branch labeling: continuity bars (policy v17) survive a branch after the gap, not one before it") {
  const std::string first = test::make_csv(60);
  const std::string second = test::make_csv(60, 1'700'000'000 + 62 * 3600);  // two missing hours
  const std::string csv = first + second.substr(second.find('\n') + 1);
  ImportRules rules;
  rules.fill_gaps = GapPolicy::CarryForward;
  const PackSource after = csv_pack_source(csv, test::hourly_meta(), rules, false, BranchRequest{80, "m08-regime-switch", 4});
  REQUIRE_MESSAGE(after.ok(), after.reason);
  CHECK(after.manifest.find("\"gapPolicy\":\"carryForward\"") != std::string::npos);
  PackOpen p = open(build(after));
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  REQUIRE(p.scenario.bars.size() == 122);
  CHECK((p.scenario.bars[60].continuity && p.scenario.bars[61].continuity && !p.scenario.bars[62].continuity));
  CHECK(p.scenario.mission.gap_policy == GapPolicy::CarryForward);
  CHECK((!p.scenario.bars[80].synthetic && p.scenario.bars[81].synthetic));
  // Branching before the gap replaces the continuity bars with generated ones: no gap policy declared.
  const PackSource before = csv_pack_source(csv, test::hourly_meta(), rules, false, BranchRequest{40, "m08-regime-switch", 4});
  REQUIRE_MESSAGE(before.ok(), before.reason);
  CHECK(before.manifest.find("gapPolicy") == std::string::npos);
  p = open(build(before));
  REQUIRE_MESSAGE(p.ok(), (p.reason + " " + p.detail));
  CHECK(std::none_of(p.scenario.bars.begin(), p.scenario.bars.end(), [](const Bar& b) { return b.continuity; }));
}

TEST_CASE("Packer engine side: stored files, real frames and publisher re-signing") {
  const PackSource src = mission_pack_source("m01-false-breakout", 9);
  REQUIRE(src.ok());
  CHECK(src.scenario_id == "m01-false-breakout-s9");
  // A .bin that is not a zstd frame with a declared size is refused before signing.
  const auto raw = detail::tar_write({{"bars.bin", src.files.front().stored}});
  CHECK(assemble_pack(src.manifest, raw, publisher_seed(), "publisher").reason == "malformed_data:bars.bin");
  CHECK(assemble_pack("{not json", stored_archive(src), publisher_seed(), "publisher").reason == "manifest_invalid");

  const auto pack = build(src);
  const PackBuild again = resign_pack(pack, test::seed_of(77), "studio");
  REQUIRE_MESSAGE(again.ok(), again.reason);
  const KeyPair studio = ed25519_keypair(test::seed_of(77));
  const std::vector<TrustedKey> studio_keys = {{key_id("studio", studio.public_key), studio.public_key}};
  CHECK(open_pack(again.bytes, studio_keys, PackIntent::Publish).ok());
  CHECK(open(again.bytes).reason == "untrusted_key");
  CHECK(open_pack(again.bytes, studio_keys, PackIntent::Install).scenario.bars.size() == open(pack).scenario.bars.size());

  // Private device imports are never re-signed for publishing.
  const PackBuild device = build_csv_pack(test::make_csv(120), test::hourly_meta(), {}, test::seed_of(3));
  REQUIRE(device.ok());
  CHECK(resign_pack(device.bytes, publisher_seed(), "publisher").reason == "private_pack");
}

TEST_CASE("Packer C ABI: source, write and re-sign through blobs") {
  const std::string src_json = tl_pack_source_mission("m04-options-buyer", 3);
  REQUIRE(src_json.find("\"ok\":true") != std::string::npos);
  CHECK(src_json.find("\"options.bin\"") != std::string::npos);
  const std::vector<std::uint8_t> archive(tl_blob_data(), tl_blob_data() + tl_blob_size());
  std::vector<detail::TarEntry> entries;
  REQUIRE(detail::tar_read(archive, 8, &entries) == detail::TarStatus::Ok);
  REQUIRE(entries.front().name == "manifest.json");
  const std::string manifest(entries.front().data.begin(), entries.front().data.end());
  std::vector<detail::TarFile> stored;
  for (std::size_t i = 1; i < entries.size(); ++i) {
    stored.push_back({entries[i].name, detail::zstd_store({entries[i].data.begin(), entries[i].data.end()})});
  }
  const auto files = detail::tar_write(stored);
  const std::string seed(64, 'a');
  const std::string out = tl_pack_write(manifest.c_str(), files.data(), static_cast<std::uint32_t>(files.size()), seed.c_str(), "publisher");
  REQUIRE(out.find("\"ok\":true") != std::string::npos);
  const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
  const std::string key = tl_signing_key(seed.c_str(), "publisher");
  const std::string id = json_at(key, "id");
  CHECK(id.starts_with("publisher-"));
  CHECK(json_at(out, "signingKeyId") == id);
  const std::string keys = "[{\"id\":\"" + id + "\",\"key\":\"" + json_at(key, "publicKey") + "\"}]";
  const std::string check = tl_pack_validate(pack.data(), static_cast<std::uint32_t>(pack.size()), keys.c_str(), 1);
  CHECK(check.find("\"ok\":true") != std::string::npos);
  CHECK(std::string(tl_signing_key(seed.c_str(), "Bad")).find("invalid_prefix") != std::string::npos);
  CHECK(std::string(tl_pack_write(manifest.c_str(), files.data(), 1, seed.c_str(), "publisher")).find("malformed_container") != std::string::npos);
  CHECK(std::string(tl_pack_source_mission("nope", 1)).find("unknown_mission") != std::string::npos);
  const std::string resigned = tl_pack_resign(pack.data(), static_cast<std::uint32_t>(pack.size()), std::string(64, 'b').c_str(), "studio");
  CHECK(resigned.find("\"signingKeyId\":\"studio-") != std::string::npos);
}
