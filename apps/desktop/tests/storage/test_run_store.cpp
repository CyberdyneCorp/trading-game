#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <string>

#include "model/json.hpp"
#include "model/storage/paths.hpp"
#include "model/storage/run_store.hpp"
#include "model/storage/schema.hpp"
#include "model/storage/sqlite.hpp"
#include "support/storage_fixtures.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::storage;
using tradelab::desktop::testing::TempDir;

namespace {

RunRecord record(const std::string& id, const std::string& mission = "m01-false-breakout") {
  RunRecord r;
  r.run_id = id;
  r.mission_id = mission;
  r.seed = 7;
  r.created_at = now_ms();
  r.execution_policy = 17;
  r.engine = "1.0.0";
  return r;
}

std::vector<std::string> tables(Database& db) {
  std::vector<std::string> out;
  Statement s = db.prepare("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name");
  while (s.step()) out.push_back(s.text(0));
  return out;
}

GetEnv env_of(std::map<std::string, std::string> vars) {
  return [vars = std::move(vars)](const char* name) -> std::optional<std::string> {
    const auto it = vars.find(name);
    return it == vars.end() ? std::nullopt : std::optional<std::string>(it->second);
  };
}

}  // namespace

TEST_CASE("storage: an empty database migrates to the current schema with WAL and synchronous FULL") {
  TempDir dir;
  RunStore store(dir.db());
  CHECK(store.opened_version() == 0);
  CHECK(store.db().user_version() == kSchemaVersion);
  CHECK(store.db().journal_mode() == "wal");
  Statement sync = store.db().prepare("PRAGMA synchronous");
  REQUIRE(sync.step());
  CHECK(sync.int64(0) == 2);  // FULL
  CHECK(tables(store.db()) == std::vector<std::string>{"actions", "checkpoints", "downloads", "imports", "meta", "packs", "runs"});
  CHECK(sqlite_version() == "3.54.1");
}

TEST_CASE("storage: a v1 database keeps its runs and actions through the migrations") {
  TempDir dir;
  {
    Database db(dir.db());
    migrate_to(db, 1);
    db.exec(
        "INSERT INTO runs (run_id, mission_id, seed, created_at, execution_policy, engine, status, actions, digest) "
        "VALUES ('run-old', 'm01-false-breakout', 3, 1, 16, '0.9.0', 'active', 2, 'abc')");
    db.exec(R"(INSERT INTO actions VALUES ('run-old', 0, '{"k":"adv","n":1}'), ('run-old', 1, '{"k":"close"}'))");
    db.exec(R"(INSERT INTO meta VALUES ('active', 'run-old'), ('progress', '{"tutorialsSeen":["m01-false-breakout"],"advisorPoints":5}'))");
  }
  RunStore store(dir.db());
  CHECK(store.opened_version() == 1);
  CHECK(store.db().user_version() == kSchemaVersion);
  const auto run = store.active_run();
  REQUIRE(run);
  CHECK(run->record.run_id == "run-old");
  CHECK(run->record.engine == "0.9.0");
  CHECK_FALSE(run->record.pack_digest);  // column added by v2, empty for old rows
  CHECK_FALSE(run->record.engine_abi);   // column added by v5: recorded before it (ABI 1)
  CHECK(run->actions == std::vector<std::string>{R"({"k":"adv","n":1})", R"({"k":"close"})"});
  const Progress p = store.progress();  // progress v1 -> v2
  CHECK(p.tutorials_seen == std::vector<std::string>{"m01-false-breakout"});
  CHECK(p.advisor_points == 5);
  CHECK(p.campaign_history.empty());
}

TEST_CASE("storage: a database from a newer build is refused, not rewritten") {
  TempDir dir;
  {
    Database db(dir.db());
    db.set_user_version(kSchemaVersion + 1);
  }
  CHECK_THROWS_AS(RunStore{dir.db()}, SchemaTooNew);
  Database db(dir.db());
  CHECK(db.user_version() == kSchemaVersion + 1);
}

TEST_CASE("storage: a new run leaves the unfinished one it replaces") {
  TempDir dir;
  RunStore store(dir.db());
  store.create_run(record("run-a"));
  store.append_action("run-a", 0, R"({"k":"adv","n":3})");
  store.create_run(record("run-b"));
  CHECK(store.run("run-a")->status == "left");
  CHECK(store.run("run-a")->actions == 1);
  const auto active = store.active_run();
  REQUIRE(active);
  CHECK(active->record.run_id == "run-b");
  CHECK(active->actions.empty());
  store.update_run("run-b", RunPatch{std::string("d1"), std::string("finished"), std::string("10500"), {}, {}, 2});
  store.create_run(record("run-c"));
  CHECK(store.run("run-b")->status == "finished");  // finished runs are kept as they are
  CHECK(store.run("run-b")->stop_warnings == 2);
  CHECK(store.finished_missions() == std::vector<std::string>{"m01-false-breakout"});
}

TEST_CASE("storage: the action log is append-only, ordered and read as a gap-free prefix") {
  TempDir dir;
  RunStore store(dir.db());
  store.create_run(record("run-a"));
  store.append_action("run-a", 0, "{\"k\":\"close\"}");
  store.append_action("run-a", 1, "{\"k\":\"advice\"}");
  CHECK_THROWS_AS(store.append_action("run-a", 1, "{\"k\":\"close\"}"), Error);  // (run_id, seq) is unique
  store.append_action("run-a", 3, "{\"k\":\"adv\",\"n\":1}");                     // a hole at 2
  const auto run = store.active_run();
  REQUIRE(run);
  CHECK(run->actions.size() == 2);
  CHECK(run->record.actions == 4);
  CHECK(run->record.digest.empty());  // cleared by each append until the new state is recorded
}

TEST_CASE("storage: only the latest checkpoint of a run is kept, and pruning keeps the resumable run's") {
  TempDir dir;
  RunStore store(dir.db());
  store.create_run(record("run-a"));
  store.save_checkpoint(Checkpoint{"run-a", 40, "aa", "d40", "1.0.0", std::nullopt});
  store.save_checkpoint(Checkpoint{"run-a", 20, "bb", "d20", "1.0.0", std::nullopt});  // older: ignored
  CHECK(store.checkpoint("run-a")->actions == 40);
  store.save_checkpoint(Checkpoint{"run-a", 60, "cc", "d60", "1.0.0", std::string("[]")});
  CHECK(store.checkpoint("run-a")->hex == "cc");
  CHECK(store.checkpoint("run-a")->series_names == "[]");
  store.create_run(record("run-b"));
  store.save_checkpoint(Checkpoint{"run-b", 20, "dd", "d20", "1.0.0", std::nullopt});
  store.prune_checkpoints(std::string("run-b"));
  CHECK_FALSE(store.checkpoint("run-a"));
  CHECK(store.checkpoint("run-b"));
  store.prune_checkpoints(std::nullopt);
  CHECK_FALSE(store.checkpoint("run-b"));
}

TEST_CASE("storage: progress v2 round-trips with the campaign kept as the web's JSON") {
  TempDir dir;
  RunStore store(dir.db());
  CHECK(store.progress() == Progress{});  // nothing stored: the defaults (3 advisor points)
  Progress p;
  p.tutorials_seen = {"m01-false-breakout"};
  p.advisor_points = 9;
  p.settled_runs = {"run-a"};
  p.lessons_done = {"lesson-1"};
  p.campaign = R"({"capital":"10000","id":"c1","index":1,"legs":[],"startedAt":5,"status":"active"})";
  p.campaign_history = {R"({"id":"c0","legs":[{"missionId":"m01-false-breakout"}],"status":"complete"})"};
  p.assessed = {"m01-false-breakout"};
  store.save_progress(p);
  CHECK(store.progress() == p);
}

TEST_CASE("storage: progress v3 keeps the review schedule, the tours done and the glossary terms seen") {
  TempDir dir;
  RunStore store(dir.db());
  Progress p;
  p.lessons_done = {"risk-sizing"};
  p.lesson_reviews = R"({"risk-sizing":{"box":2,"due":"2026-03-04"}})";
  p.coach_done = {"first-trade"};
  p.glossary_seen = {"leverage", "strike"};
  store.save_progress(p);
  CHECK(store.progress() == p);
  CHECK(progress_json(p).find(R"("version":4)") != std::string::npos);
}

// Progress v4 (add-practice-and-advanced-strategies D20): the practice keys round-trip; a v3 row reads
// them empty, and damaged entries are dropped as the web's migrateProgress drops them.
TEST_CASE("storage: progress v4 keeps the practice keys; a v3 row reads them with their defaults") {
  TempDir dir;
  RunStore store(dir.db());
  Progress p;
  p.drill_reviews = R"({"greeks":{"box":3,"due":1700000000000}})";
  p.drill_history = R"([{"date":"2026-11-08","firstTry":4,"items":5}])";
  p.daily_challenges = R"({"2026-11-08":{"attempts":1,"best":null,"first":null,"mission":"m03-volatility-shock","seed":77}})";
  p.guess_history = R"([{"decision":"long","market":"m01","p":60,"score":80,"seed":3,"step":40}])";
  store.save_progress(p);
  CHECK(store.progress() == p);

  const auto v3 = json::parse(R"({"version":3,"lessonsDone":["risk-sizing"],"campaign":{"id":"c","legs":[],"index":4}})");
  REQUIRE(v3);
  const Progress old = migrate_progress(*v3);
  CHECK(old.drill_reviews == "{}");
  CHECK(old.drill_history == "[]");
  CHECK(old.daily_challenges == "{}");
  CHECK(old.guess_history == "[]");
  REQUIRE(old.campaign);
  CHECK(old.campaign->find("order") == std::string::npos);  // read as the 17-mission order (campaign_order)

  const auto damaged = json::parse(
      R"({"drillReviews":{"a":{"box":1,"due":2},"b":{"box":"x"}},"drillHistory":[{"date":"d","items":1,"firstTry":1},7,{"date":3}],)"
      R"("dailyChallenges":{"2026-01-01":{"mission":"m","seed":1},"bad":{"mission":"m","seed":1},"2026-01-02":{"seed":1}},)"
      R"("guessHistory":[{"market":"m","seed":1,"step":2,"decision":"up","p":1,"score":1}]})");
  REQUIRE(damaged);
  const Progress d = migrate_progress(*damaged);
  CHECK(d.drill_reviews == R"({"a":{"box":1,"due":2}})");
  CHECK(d.drill_history == R"([{"date":"d","firstTry":1,"items":1}])");
  CHECK(d.daily_challenges == R"({"2026-01-01":{"mission":"m","seed":1}})");
  CHECK(d.guess_history == "[]");
}

TEST_CASE("storage: progress v4 histories keep only their newest entries") {
  std::string rows;
  for (int i = 0; i < 35; ++i) rows += (i ? "," : "") + std::string(R"({"date":"d)") + std::to_string(i) + R"(","items":1,"firstTry":1})";
  const auto row = json::parse(R"({"drillHistory":[)" + rows + "]}");
  REQUIRE(row);
  const auto kept = json::parse(migrate_progress(*row).drill_history);
  REQUIRE(kept);
  REQUIRE(kept->items().size() == kDrillHistoryKeep);
  CHECK(kept->items().front()["date"].as_string() == "d5");
}

// A version-2 row (before add-academy-and-strategies) reads with the version-3 fields empty.
TEST_CASE("storage: a progress v2 row migrates to v4 with no reviews, tours or terms") {
  const auto row = json::parse(R"({"version":2,"tutorialsSeen":["m01-false-breakout"],"lessonsDone":["foundations-bar"]})");
  REQUIRE(row);
  const Progress p = migrate_progress(*row);
  CHECK(p.version == 4);
  CHECK(p.lessons_done == std::vector<std::string>{"foundations-bar"});
  CHECK(p.lesson_reviews == "{}");
  CHECK(p.coach_done.empty());
  CHECK(p.glossary_seen.empty());
}

TEST_CASE("storage: damaged progress fields fall back to their defaults (web migrateProgress)") {
  const auto row = json::parse(
      R"({"tutorialsSeen":["a",3,null],"advisorPoints":"x","campaign":{"id":"c"},"campaignHistory":[{"id":"h","legs":[]},7]})");
  REQUIRE(row);
  const Progress p = migrate_progress(*row);
  CHECK(p.tutorials_seen == std::vector<std::string>{"a"});
  CHECK(p.advisor_points == kStartPoints);
  CHECK_FALSE(p.campaign);  // no legs: not a campaign
  REQUIRE(p.campaign_history.size() == 1);
  CHECK(p.campaign_history[0] == R"({"id":"h","legs":[]})");
}

TEST_CASE("storage: settings, the device seed and packs") {
  TempDir dir;
  RunStore store(dir.db());
  CHECK_FALSE(store.setting("locale"));
  store.set_setting("locale", "en");
  store.set_setting("locale", "pt-BR");
  CHECK(store.setting("locale") == "pt-BR");

  int calls = 0;
  const auto random = [&] {
    ++calls;
    return std::string(64, 'a');
  };
  const std::string seed = store.device_seed(random);
  CHECK(store.device_seed(random) == seed);
  CHECK(calls == 1);

  StoredPack pk;
  pk.digest = std::string(64, 'f');
  pk.scenario_id = "csv-1";
  pk.label = "BTC 1h";
  pk.size = 1234;
  pk.added_at = 2;
  store.save_pack(pk);
  StoredPack older = pk;
  older.digest = std::string(64, '0');
  older.added_at = 1;
  older.origin = "catalog";
  older.mode = "synthetic";
  store.save_pack(older);
  const auto packs = store.packs();
  REQUIRE(packs.size() == 2);
  CHECK(packs[0].digest == older.digest);  // oldest first
  CHECK(packs[0].mode == "synthetic");
  store.delete_pack(pk.digest);
  CHECK_FALSE(store.pack(pk.digest));

  StoredPack bad = pk;
  bad.origin = "elsewhere";
  CHECK_THROWS_AS(store.save_pack(bad), Error);  // CHECK constraint
}

TEST_CASE("storage: runs() lists newest first and reads a stale active run as left") {
  TempDir dir;
  RunStore store(dir.db());
  RunRecord a = record("run-a");
  a.created_at = 1;
  store.create_run(a);
  store.db().exec("UPDATE meta SET value = 'none' WHERE key = 'active'");  // replaced before 'left' existed
  RunRecord b = record("run-b");
  b.created_at = 2;
  store.create_run(b);
  const auto runs = store.runs();
  REQUIRE(runs.size() == 2);
  CHECK(runs[0].run_id == "run-b");
  CHECK(runs[0].status == "active");
  CHECK(runs[1].status == "left");
}

TEST_CASE("storage: user-data folder per platform, with and without XDG_DATA_HOME") {
  CHECK(user_data_dir(Os::Linux, env_of({{"HOME", "/home/ana"}})) == "/home/ana/.local/share/tradelab");
  CHECK(user_data_dir(Os::Linux, env_of({{"HOME", "/home/ana"}, {"XDG_DATA_HOME", "/data"}})) == "/data/tradelab");
  // A relative XDG_DATA_HOME is invalid per the XDG spec and ignored.
  CHECK(user_data_dir(Os::Linux, env_of({{"HOME", "/home/ana"}, {"XDG_DATA_HOME", "rel"}})) ==
        "/home/ana/.local/share/tradelab");
  CHECK(user_data_dir(Os::MacOS, env_of({{"HOME", "/Users/ana"}, {"XDG_DATA_HOME", "/data"}})) ==
        "/Users/ana/Library/Application Support/TradeLab");
  CHECK(user_data_dir(Os::MacOS, env_of({{"TRADELAB_DATA_DIR", "/tmp/t"}})) == "/tmp/t");
  CHECK_FALSE(user_data_dir(Os::Linux, env_of({})));
}

TEST_CASE("storage: a second instance on the same folder does not get the lock") {
  TempDir dir;
  {
    InstanceLock first(dir.path());
    CHECK(first.held());
    InstanceLock second(dir.path());
    CHECK_FALSE(second.held());
  }
  InstanceLock again(dir.path());  // released with the first instance
  CHECK(again.held());
}
