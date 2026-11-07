// Run files on the desktop's storage and the replay viewer in the worker (add-run-sharing-and-journal-stats
// D2, D5, D6, D7): import (verified, deduplicated, refusals stored nowhere), export of a stored run (the
// web's canonical file), recomputed summaries, and a watched run that plays its log bar by bar to the
// recorded digest without touching storage.
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "model/engine_host.hpp"
#include "model/run_sharing.hpp"
#include "model/storage/json_writer.hpp"
#include "model/storage/storage.hpp"
#include "support/storage_fixtures.hpp"
#include "support/ui_queue.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::TempDir;
using tradelab::desktop::testing::UiQueue;

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string runfile(const std::string& name) { return read_file(std::string(TRADELAB_RUNFILES_DIR) + "/" + name); }

json::Value index_entry(const std::string& name) {
  const auto index = json::parse(runfile("index.json"));
  for (const json::Value& e : (*index)["files"].items()) {
    if (e["name"].as_string() == name) return e["expected"];
  }
  return {};
}

const EngineStamp kStamp{17, 1, "0.1.0"};

// The golden m01 run stored as the recorder would have stored it.
void store_golden_m01(storage::RunStore& store, const std::string& run_id, const std::string& digest) {
  const auto fx = json::parse(read_file(std::string(TRADELAB_GOLDEN_DIR) + "/m01-false-breakout.json"));
  storage::RunRecord r;
  r.run_id = run_id;
  r.mission_id = "m01-false-breakout";
  r.seed = static_cast<std::uint32_t>((*fx)["seed"].as_int());
  r.execution_policy = 17;
  r.status = "finished";
  r.digest = digest;
  store.create_run(r);
  std::int64_t seq = 0;
  for (const json::Value& a : (*fx)["actions"].items()) store.append_action(run_id, seq++, storage::dump(a));
  store.update_run(run_id, storage::RunPatch{.digest = digest, .status = std::string("finished")});
}

struct Harness {
  UiQueue ui;
  std::shared_ptr<const Snapshot> last;
  EngineHost host{[this](EngineHost::Task t) { ui.post(std::move(t)); }, [this](std::shared_ptr<const Snapshot> s) { last = std::move(s); }};
  std::shared_ptr<const Snapshot> settle() {
    host.wait_idle();
    ui.drain();
    return last;
  }
};

}  // namespace

TEST_CASE("run sharing: import verifies, keeps one row per file and stores nothing it refuses") {
  TempDir dir;
  storage::Storage s(dir.path() / "data");
  const auto ok = runfiles::import_text(s, runfile("m01-finished.tlrun"));
  REQUIRE(ok.ok);
  CHECK(ok.record.id == index_entry("m01-finished.tlrun")["fileDigest"].as_string());
  CHECK(runfiles::import_text(s, runfile("m01-finished.tlrun")).ok);  // the same file again
  CHECK(s.store().imports().size() == 1);
  const auto tampered = runfiles::import_text(s, runfile("tampered-digest.tlrun"));
  CHECK_FALSE(tampered.ok);
  CHECK(tampered.reason == "digest_mismatch");
  const auto field = runfiles::import_text(s, runfile("unknown-member.tlrun"));
  CHECK(field.reason == "invalid_field");
  CHECK(field.path == "device");
  CHECK(runfiles::import_text(s, runfile("other-policy.tlrun")).detail == "16");
  CHECK(runfiles::import_text(s, runfile("pack-missing.tlrun")).reason == "pack_required");
  CHECK(runfiles::import_text(s, std::string(kRunFileMaxBytes + 1, ' ')).reason == "too_large");
  CHECK(s.store().imports().size() == 1);
  CHECK(s.store().runs().empty());  // an import is never one of the player's runs
}

TEST_CASE("run sharing: a stored run exports as the web's canonical file; older runs get their summaries") {
  TempDir dir;
  storage::Storage s(dir.path() / "data");
  const std::string digest = index_entry("m01-finished.tlrun")["digest"].as_string();
  store_golden_m01(s.store(), "r-old", digest);
  const auto out = runfiles::export_run(s, "r-old", kStamp, false);
  REQUIRE(out.ok);
  CHECK_FALSE(out.mismatch);
  CHECK(out.name.starts_with("tradelab-m01-false-breakout-"));
  CHECK(verify_run_file(out.text)["fileDigest"].as_string() == index_entry("m01-finished.tlrun")["fileDigest"].as_string());
  CHECK(runfiles::export_run(s, "nope", kStamp, false).reason == "unknown_run");

  store_golden_m01(s.store(), "r-damaged", std::string(64, '0'));  // its log no longer gives its digest
  const auto re = runfiles::recompute_summaries(s, kStamp);
  CHECK(re.done == 1);
  CHECK(re.skipped == 1);
  REQUIRE(s.store().summaries().size() == 1);
  CHECK(s.store().summaries()[0].first == "r-old");
  CHECK(s.store().summaries()[0].second.find(R"("mission":"m01-false-breakout")") != std::string::npos);
  CHECK(runfiles::recompute_summaries(s, kStamp).done == 0);  // done once
}

TEST_CASE("replay viewer in the worker: a run file plays bar by bar to its recorded digest; other commands are refused") {
  Harness h;
  const std::string text = verify_run_file(runfile("m01-finished.tlrun"))["run"].as_string();
  h.host.watch(text);
  auto s = h.settle();
  REQUIRE(s != nullptr);
  REQUIRE(s->results.size() == 1);
  CHECK(s->results[0].kind == CommandKind::Watch);
  CHECK(s->results[0].accepted);
  CHECK(s->watching);
  CHECK(s->watch_bar == 0);
  CHECK(s->watch_bars > 0);
  CHECK(s->run_id.empty());
  const int start = s->step;
  h.host.advance(1);
  s = h.settle();
  CHECK(s->watch_bar == 1);
  CHECK(s->step == start + 1);
  h.host.market(Side::Buy, "1");
  s = h.settle();
  CHECK(s->results.back().reason == "replay");
  h.host.advance(100000);
  s = h.settle();
  CHECK(s->status == "finished");
  CHECK(s->watch_done);
  CHECK(s->digest == index_entry("m01-finished.tlrun")["digest"].as_string());
  REQUIRE(s->debrief.has_value());
  h.host.start("m01-false-breakout", 1);  // a new run leaves the viewer
  s = h.settle();
  CHECK_FALSE(s->watching);
  h.host.watch("{}");
  s = h.settle();
  CHECK_FALSE(s->results.back().accepted);
}

TEST_CASE("run sharing: a file larger than the limit is not even read") {
  TempDir dir;
  const auto path = dir.path() / "big.tlrun";
  {
    std::ofstream out(path);
    out << std::string(kRunFileMaxBytes + 10, ' ');
  }
  std::string why;
  CHECK_FALSE(runfiles::read_run_file(path.string(), &why).has_value());
  CHECK(why == "too_large");
  CHECK_FALSE(runfiles::read_run_file((dir.path() / "missing.tlrun").string(), &why).has_value());
  CHECK(why == "unreadable");
}
