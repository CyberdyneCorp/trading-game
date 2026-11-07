// Run files on the desktop (add-run-sharing-and-journal-stats D1-D5, D9): every shared fixture of
// engine/tests/runfiles gets its expected verdict through the desktop's engine, the desktop's exporter
// writes the same canonical files as the web (cross-client), and imports live in their own table.
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "model/json.hpp"
#include "model/run_files.hpp"
#include "model/storage/json_writer.hpp"
#include "model/storage/profile_store.hpp"
#include "model/storage/run_store.hpp"
#include "support/storage_fixtures.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::TempDir;

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

json::Value load(const std::string& path) {
  auto v = json::parse(read_file(path));
  REQUIRE_MESSAGE(v.has_value(), path);
  return *v;
}

const EngineStamp kStamp{17, 1, "0.1.0"};

// The record a desktop run of this golden fixture would have.
storage::StoredRun golden_run(const std::string& name) {
  const json::Value fx = load(std::string(TRADELAB_GOLDEN_DIR) + "/" + name + ".json");
  storage::StoredRun run;
  run.record.run_id = "run-desktop-1";
  run.record.mission_id = fx["mission"].as_string();
  run.record.seed = static_cast<std::uint32_t>(fx["seed"].as_int());
  run.record.execution_policy = 17;
  run.record.created_at = 1'700'000'000'000;
  if (fx.has("options")) run.record.options = storage::options_from_json(storage::dump(fx["options"]));
  for (const json::Value& a : fx["actions"].items()) run.actions.push_back(storage::dump(a));
  return run;
}

std::string expected_digest(const json::Value& index, const std::string& file) {
  for (const json::Value& e : index["files"].items()) {
    if (e["name"].as_string() == file) return e["expected"]["fileDigest"].as_string();
  }
  return {};
}

}  // namespace

TEST_CASE("run files: every shared fixture gets its expected verdict through the desktop's engine") {
  const json::Value index = load(std::string(TRADELAB_RUNFILES_DIR) + "/index.json");
  REQUIRE(index["files"].size() > 10);
  for (const json::Value& entry : index["files"].items()) {
    const std::string name = entry["name"].as_string();
    INFO(name);
    const json::Value verdict = verify_run_file(read_file(std::string(TRADELAB_RUNFILES_DIR) + "/" + name));
    for (const auto& [key, want] : entry["expected"].fields()) CHECK(storage::dump(verdict[key]) == storage::dump(want));
  }
}

TEST_CASE("run files: the desktop exporter writes the web's canonical files (cross-client)") {
  const json::Value index = load(std::string(TRADELAB_RUNFILES_DIR) + "/index.json");
  for (const auto& [golden, file] : {std::pair{"m01-false-breakout", "m01-finished.tlrun"}, std::pair{"m13-call-spread", "m13-call-spread.tlrun"},
                                     std::pair{"m08-regime-switch", "m08-rejections.tlrun"}, std::pair{"m02-advisor-options", "m02-advice-notes.tlrun"}}) {
    INFO(golden);
    const storage::StoredRun run = golden_run(golden);
    const std::string draft = run_file_draft(run.record, run.actions, kStamp, false);
    CHECK(draft.find("run-desktop-1") == std::string::npos);  // no run id
    CHECK(draft.find("1700000000000") == std::string::npos);  // no timestamps
    const json::Value sealed = seal_run_file(draft);
    REQUIRE(sealed["ok"].as_bool());
    if (std::string(file) != "m02-advice-notes.tlrun") CHECK(sealed["fileDigest"].as_string() == expected_digest(index, file));
    CHECK(verify_run_file(sealed["run"].as_string())["ok"].as_bool());
  }
}

TEST_CASE("run files: plan notes only on request, amounts as decimal text") {
  storage::StoredRun run = golden_run("m01-false-breakout");
  run.record.journal =
      R"([{"step":70,"side":"buy","thesis":"range low","invalidation":97.5,"target":104,"confidence":60,"plannedRisk":100.004,"skipped":false},)"
      R"({"step":80,"side":"sell","thesis":"","invalidation":null,"target":null,"confidence":0,"plannedRisk":null,"skipped":true}])";
  const json::Value without = *json::parse(run_file_draft(run.record, run.actions, kStamp, false));
  CHECK(without["journal"].size() == 0);
  const json::Value with = *json::parse(run_file_draft(run.record, run.actions, kStamp, true));
  REQUIRE(with["journal"].size() == 2);
  CHECK(with["journal"][0]["invalidation"].as_string() == "97.5");
  CHECK(with["journal"][0]["plannedRisk"].as_string() == "100.004");
  CHECK(with["journal"][1]["target"].is_null());
  CHECK(seal_run_file(storage::dump(with))["ok"].as_bool());
  CHECK(decimal_text(0.123456789) == std::optional<std::string>("0.12345679"));
  CHECK(decimal_text(100) == std::optional<std::string>("100"));
  CHECK_FALSE(decimal_text(-1).has_value());
}

TEST_CASE("run files: private packs and empty runs are not exportable; the file name") {
  storage::RunRecord r;
  std::vector<storage::StoredPack> packs(2);
  packs[0].digest = "a";
  packs[0].is_private = true;
  packs[1].digest = "b";
  packs[1].origin = "catalog";
  CHECK(export_block(r, 0, packs) == std::optional<std::string>("no_actions"));
  CHECK_FALSE(export_block(r, 3, packs).has_value());
  r.pack_digest = "a";
  CHECK(export_block(r, 3, packs) == std::optional<std::string>("private_pack"));
  r.pack_digest = "b";
  CHECK_FALSE(export_block(r, 3, packs).has_value());
  r.pack_digest = "c";
  CHECK(export_block(r, 3, packs) == std::optional<std::string>("pack_missing"));
  CHECK(run_file_name("m01-false-breakout", 42) == "tradelab-m01-false-breakout-42.tlrun");
}

TEST_CASE("storage: imported runs in their own table, one row per file, never a finished mission; summaries") {
  TempDir dir;
  storage::RunStore store(dir.db());
  const std::string id(64, 'a');
  store.save_import({id, "{}", "m01-false-breakout", 7, std::nullopt, "finished", 10});
  store.save_import({id, "{}", "m01-false-breakout", 7, std::nullopt, "finished", 99});
  store.save_import({std::string(64, 'b'), "{}", "m06-thin-book", 8, std::nullopt, "active", 20});
  const auto rows = store.imports();
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].mission == "m06-thin-book");
  CHECK(rows[1].imported_at == 10);
  CHECK(store.finished_missions().empty());
  CHECK(store.runs().empty());
  store.delete_import(id);
  CHECK(store.imports().size() == 1);

  storage::RunRecord r;
  r.run_id = "r1";
  r.mission_id = "m01-false-breakout";
  store.create_run(r);
  store.append_action("r1", 0, R"({"k":"adv","n":2})");
  CHECK(store.stored_run("r1")->actions.size() == 1);
  CHECK_FALSE(store.stored_run("nope").has_value());
  store.set_summary("r1", R"({"v":1})");
  REQUIRE(store.summaries().size() == 1);
  CHECK(store.summaries()[0].second == R"({"v":1})");
}

TEST_CASE("storage: the shell's run summary is saved to runs.summary and read back by a new connection") {
  TempDir dir;
  {
    storage::RunStore store(dir.db());
    storage::RunRecord r;
    r.run_id = "r1";
    r.mission_id = "m01-false-breakout";
    store.create_run(r);
  }
  {
    storage::ProfileStore profile(dir.path() / "tradelab.db");
    Progress p = profile.load_progress();
    REQUIRE(p.runs.size() == 1);
    CHECK(p.runs[0].summary.empty());
    p.runs[0].summary = R"({"v":1,"mission":"m01-false-breakout"})";
    profile.save_progress(p);
  }
  storage::ProfileStore again(dir.path() / "tradelab.db");
  CHECK(again.load_progress().runs[0].summary == R"({"v":1,"mission":"m01-false-breakout"})");
}
