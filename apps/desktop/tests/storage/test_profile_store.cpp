// Progress and settings in SQLite (storage::ProfileStore): what the shell and the terminal save comes back
// in the next launch (a new connection on the same file), the runs table is the shell's run list, and an
// earlier build's prefs.json is imported once.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "model/game_prefs.hpp"
#include "model/progress_json.hpp"
#include "model/storage/profile_store.hpp"
#include "support/storage_fixtures.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::TempDir;

namespace {

storage::RunRecord stored_run(const std::string& id, const std::string& mission, std::int64_t at) {
  storage::RunRecord r;
  r.run_id = id;
  r.mission_id = mission;
  r.seed = 7;
  r.created_at = at;
  r.engine = "test";
  return r;
}

CampaignRun campaign_with_leg() {
  CampaignRun c{"camp-1", 1'700'000'000'000, "10250.5", 1, CampaignStatus::Active, {}};
  LegReport report{{{"120.5", 15000, "trend"}, {"-30", std::nullopt, ""}}, "90.5", "4.2", "1.1", "-0.3"};
  c.legs.push_back({"run-a", "m01-false-breakout", "10000", "10250.5", LegResult::Finished, report});
  return c;
}

}  // namespace

TEST_CASE("profile store: progress written in one launch is read back in the next") {
  TempDir dir;
  tradelab::desktop::Progress p;
  p.lessons_done = {"L1-candles"};
  p.assessed = {"m01-false-breakout"};
  p.campaign = campaign_with_leg();
  CampaignRun ended = campaign_with_leg();
  ended.id = "camp-0";
  ended.status = CampaignStatus::Bankrupt;
  p.campaign_history = {ended};
  {
    storage::ProfileStore store(dir.path() / "tradelab.db");
    store.save_progress(p);
  }
  storage::ProfileStore again(dir.path() / "tradelab.db");
  const tradelab::desktop::Progress back = again.load_progress();
  CHECK(back.lessons_done == p.lessons_done);
  CHECK(back.assessed == p.assessed);
  REQUIRE(back.campaign);
  CHECK(back.campaign->id == "camp-1");
  CHECK(back.campaign->capital == "10250.5");
  REQUIRE(back.campaign->legs.size() == 1);
  REQUIRE(back.campaign->legs[0].report);
  CHECK(back.campaign->legs[0].report->trades[0].r_bp == 15000);
  CHECK_FALSE(back.campaign->legs[0].report->trades[1].r_bp);
  CHECK(back.campaign->legs[0].report->funding == "-0.3");
  REQUIRE(back.campaign_history.size() == 1);
  CHECK(back.campaign_history[0].status == CampaignStatus::Bankrupt);
  CHECK(again.durable());
}

TEST_CASE("profile store: the runs table is the shell's run list; the shell writes journal, campaign link and left") {
  TempDir dir;
  storage::ProfileStore store(dir.path() / "tradelab.db");
  storage::RunRecord a = stored_run("run-a", "m01-false-breakout", 1000);
  a.status = "finished";
  store.store().create_run(a);
  storage::RunRecord pack = stored_run("run-p", "csv-123", 2000);
  pack.pack_digest = "abc";
  pack.status = "finished";
  store.store().create_run(pack);
  storage::RunRecord assisted = stored_run("run-s", "m02-trend", 3000);
  assisted.assisted = R"({"from":"run-a","kind":"seed"})";
  assisted.status = "finished";
  store.store().create_run(assisted);
  store.store().create_run(stored_run("run-c", "m03-leverage", 4000));  // the active one

  tradelab::desktop::Progress p = store.load_progress();
  REQUIRE(p.runs.size() == 4);
  CHECK(p.runs[0].run_id == "run-a");  // oldest first, as the shell appends them
  CHECK(p.runs[1].pack_digest == "abc");
  CHECK(p.runs[2].assisted);
  CHECK(p.runs[3].status == RunStatus::Active);
  CHECK(p.completed == std::vector<std::string>{"m01-false-breakout"});  // packs and assisted practice never count

  p.runs[3].journal.push_back(PlanEntry{61, true, "rompimento", 99.5, 104.0, 70, 50.0, false, "", ""});
  p.runs[3].campaign = CampaignLink{"camp-9", 2};
  p.runs[3].status = RunStatus::Left;  // ending a campaign leaves its mission without starting another
  p.runs[3].equity = "9900";
  store.save_progress(p);

  const auto row = store.store().run("run-c");
  REQUIRE(row);
  CHECK(row->status == "left");
  CHECK(row->equity == std::optional<std::string>("9900"));
  const auto link = progress_json::link_from(*json::parse(row->campaign.value_or("{}")));
  REQUIRE(link);
  CHECK(*link == CampaignLink{"camp-9", 2});
  storage::ProfileStore next(dir.path() / "tradelab.db");
  const tradelab::desktop::Progress back = next.load_progress();
  REQUIRE(back.runs[3].journal.size() == 1);
  CHECK(back.runs[3].journal[0].thesis == "rompimento");
  CHECK(back.runs[3].journal[0].invalidation == std::optional<double>(99.5));
  CHECK(back.runs[3].journal[0].planned_risk == std::optional<double>(50.0));
  CHECK(back.runs[3].campaign == CampaignLink{"camp-9", 2});
}

TEST_CASE("profile store: a finished run's status is the worker's; the shell never rewrites it") {
  TempDir dir;
  storage::ProfileStore store(dir.path() / "tradelab.db");
  storage::RunRecord r = stored_run("run-f", "m01-false-breakout", 1000);
  r.status = "finished";
  store.store().create_run(r);
  tradelab::desktop::Progress p = store.load_progress();
  p.runs[0].status = RunStatus::Left;
  store.save_progress(p);
  CHECK(store.store().run("run-f")->status == "finished");
}

TEST_CASE("profile store: settings and the shared key map") {
  TempDir dir;
  {
    storage::ProfileStore store(dir.path() / "tradelab.db");
    CHECK_FALSE(store.load_settings());  // a first launch: the defaults (and the system language)
    Settings s;
    s.locale = Locale::En;
    s.crt = CrtSettings{true, false, true, false};
    s.colorblind = true;
    s.text_scale = 1.5f;
    s.keymap = {{"advance", "n"}};
    store.save_settings(s);
  }
  storage::ProfileStore store(dir.path() / "tradelab.db");
  const auto s = store.load_settings();
  REQUIRE(s);
  CHECK(s->locale == Locale::En);
  CHECK(s->crt == CrtSettings{true, false, true, false});
  CHECK(s->colorblind);
  CHECK(s->text_scale == doctest::Approx(1.5));
  CHECK(s->keymap.at("advance") == "n");
  CHECK(store.load_prefs().keymap.at("advance") == "n");  // the terminal reads the same map
}

TEST_CASE("profile store: the terminal's preferences and the shell's progress share one row without clobbering") {
  TempDir dir;
  storage::ProfileStore store(dir.path() / "tradelab.db");
  GamePrefs prefs = store.load_prefs();
  CHECK(prefs.advisor_points == 3);
  prefs.tutorials_seen = {"m01-false-breakout"};
  prefs.advisor_points = 5;
  prefs.settled_runs = {"run-a"};
  prefs.perks_introduced = {"calendar"};
  REQUIRE(store.save_prefs(prefs));
  tradelab::desktop::Progress p;
  p.lessons_done = {"L1-candles"};
  store.save_progress(p);  // after the prefs: they stay
  const GamePrefs back = store.load_prefs();
  CHECK(back.tutorials_seen == prefs.tutorials_seen);
  CHECK(back.advisor_points == 5);
  CHECK(back.settled_runs == prefs.settled_runs);
  CHECK(back.perks_introduced == prefs.perks_introduced);
  CHECK(store.load_progress().lessons_done == p.lessons_done);
}

TEST_CASE("profile store: sound and the docked layout are settings rows read back by a new connection") {
  TempDir dir;
  {
    storage::ProfileStore store(dir.path() / "tradelab.db");
    GamePrefs prefs = store.load_prefs();
    CHECK(prefs.audio == AudioPrefs{});
    CHECK(prefs.layout.empty());
    prefs.audio = AudioPrefs{true, 0.5, 0.25};
    prefs.layout = "h:0.7(game-panel-chart,game-panel-rail)";
    REQUIRE(store.save_prefs(prefs));
  }
  storage::ProfileStore again(dir.path() / "tradelab.db");
  const GamePrefs back = again.load_prefs();
  CHECK(back.audio == AudioPrefs{true, 0.5, 0.25});
  CHECK(back.layout == "h:0.7(game-panel-chart,game-panel-rail)");
}

TEST_CASE("profile store: prefs.json from an earlier build is imported once and renamed") {
  TempDir dir;
  const auto file = dir.path() / "prefs.json";
  {
    std::ofstream out(file);
    out << R"({"version":1,"keymap":{"advance":"n"},"tutorialsSeen":["m02-trend"],"advisorPoints":6,)"
        << R"("settledRuns":["r1"],"finished":["m01-false-breakout","m02-trend"],"perksIntroduced":["calendar"],)"
        << R"("audio":{"muted":true,"music":0.2,"sfx":0.9},"layout":"stored-layout"})";
  }
  storage::ProfileStore store(dir.path() / "tradelab.db");
  CHECK(store.import_prefs_file(file));
  CHECK_FALSE(std::filesystem::exists(file));
  CHECK(std::filesystem::exists(dir.path() / "prefs.json.imported"));
  CHECK_FALSE(store.import_prefs_file(file));  // nothing left to import
  const GamePrefs p = store.load_prefs();
  CHECK(p.keymap.at("advance") == "n");
  CHECK(p.tutorials_seen == std::vector<std::string>{"m02-trend"});
  CHECK(p.advisor_points == 6);
  CHECK(p.settled_runs == std::vector<std::string>{"r1"});
  CHECK(p.perks_introduced == std::vector<std::string>{"calendar"});
  CHECK(p.audio == AudioPrefs{true, 0.2, 0.9});
  CHECK(p.layout == "stored-layout");
  // Missions finished before the import still unlock perks and show as completed.
  CHECK(p.finished == std::vector<std::string>{"m01-false-breakout", "m02-trend"});
  CHECK(store.load_progress().completed == p.finished);
}

TEST_CASE("progress json: a journal entry keeps its numbers and its absent levels") {
  const std::vector<PlanEntry> in = {PlanEntry{70, false, "topo \"duplo\"", std::nullopt, 95.25, 40, std::nullopt, true, "perde se romper", "C 100"}};
  const auto parsed = json::parse(progress_json::journal(in));
  REQUIRE(parsed);
  const auto out = progress_json::journal_from(*parsed);
  REQUIRE(out.size() == 1);
  CHECK(out[0].step == 70);
  CHECK_FALSE(out[0].buy);
  CHECK(out[0].thesis == "topo \"duplo\"");
  CHECK_FALSE(out[0].invalidation);
  CHECK(out[0].target == std::optional<double>(95.25));
  CHECK_FALSE(out[0].planned_risk);
  CHECK(out[0].skipped);
  CHECK(out[0].lose_scenario == "perde se romper");
  CHECK(out[0].instrument == "C 100");
}
