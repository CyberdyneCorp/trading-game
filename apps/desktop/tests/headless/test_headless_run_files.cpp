// Run files on Raven's null platform (add-run-sharing-and-journal-stats): import, replay and debrief of
// the web-checked shared file, a tampered file refused, and an own run exported and watched again.
#include <doctest/doctest.h>

#include "model/json.hpp"
#include "support/run_file_flows.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

struct RavenSession {
  RavenSession() { REQUIRE(init_headless()); }
  ~RavenSession() { Rshutdown(); }
};

std::string expected_digest(const std::string& file) {
  const auto index = json::parse(testing::read_text(testing::runfile_path("index.json")));
  for (const json::Value& e : (*index)["files"].items()) {
    if (e["name"].as_string() == file) return e["expected"]["digest"].as_string();
  }
  return {};
}

}  // namespace

TEST_CASE("headless run files: a tampered file refused, the shared file imported, replayed bar by bar and debriefed") {
  testing::TempDir dir;
  RavenSession raven;
  App app(builtin_catalog(), [] { return 11u; }, std::make_unique<storage::Storage>(dir.path() / "data"));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  testing::flow_run_files(app, d, expected_digest("m01-finished.tlrun"));
}

TEST_CASE("headless run files: an own run exported from the journal verifies, and is watched again") {
  testing::TempDir dir;
  RavenSession raven;
  App app(builtin_catalog(), [] { return 12u; }, std::make_unique<storage::Storage>(dir.path() / "data"));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  testing::flow_export_own(app, d, t, dir.path() / "exports");
}
