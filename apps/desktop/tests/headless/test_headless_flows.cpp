// The shell flows of tests/support/shell_flows.hpp on Raven's null platform (every CI job runs them; the
// windowed suite plays the same flows on the Vulkan renderer with captures).
#include <doctest/doctest.h>

#include "support/shell_flows.hpp"

using namespace tradelab::desktop;
using tradelab::desktop::testing::Driver;
using tradelab::desktop::testing::Terminal;

namespace {

struct RavenSession {
  RavenSession() { REQUIRE(init_headless()); }
  ~RavenSession() { Rshutdown(); }
};

}  // namespace

TEST_CASE("headless flows: the tutorial before the first briefing") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  testing::flow_tutorial(app, d);
}

TEST_CASE("headless flows: assisted practice, rewind from the terminal and another market from the debrief") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 2u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  testing::flow_assisted(app, d, t);
}

TEST_CASE("headless flows: the debrief's chart, candle table and lesson results") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 3u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  Terminal t(app, d);
  testing::flow_debrief_chart_and_lessons(app, d, t);
}

TEST_CASE("headless flows: CSV import with a gap, then practice on the pack") {
  testing::TempDir dir;
  const auto csv = testing::write_gap_csv(dir.path());
  RavenSession raven;
  App app(builtin_catalog(), [] { return 4u; }, std::make_unique<storage::Storage>(dir.path() / "data"));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  testing::flow_csv_import(app, d, csv);
}

TEST_CASE("headless flows: catalog download cut, resumed, verified, installed and played") {
  testing::TempDir dir;
  testing::CatalogServer server;
  RavenSession raven;
  App app(builtin_catalog(), [] { return 5u; }, std::make_unique<storage::Storage>(dir.path()));
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(2);
  testing::flow_catalog_download(app, d, server);
}

TEST_CASE("headless flows: a campaign mission and the settings saved in one launch, resumed in the next") {
  testing::TempDir dir;
  testing::flow_relaunch_campaign<RavenSession>(dir.path());
}

TEST_CASE("headless flows: a pack run saved in one launch, resumed in the next") {
  testing::TempDir dir;
  const auto csv = testing::write_gap_csv(dir.path());
  testing::flow_relaunch_pack<RavenSession>(dir.path() / "data", csv);
}
