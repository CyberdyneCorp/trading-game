// Headless Academy (Raven null platform + null backend; add-academy-and-strategies tasks D3, D6, D7,
// D8): every lesson played from the keyboard (choices, typed numbers, the ticket sandbox), the review
// prompt, the glossary with its in-place definitions, and the guided first trade's keys, all driven by
// injected input (tests/support/academy_flows.hpp).
#include <doctest/doctest.h>

#include <raven/raven_accessibility.h>
#include <raven/raven_widget.h>

#include "support/academy_flows.hpp"
#include "support/driver.hpp"
#include "view/app.hpp"
#include "viewmodel/academy_names.hpp"

using namespace tradelab::desktop;
namespace names = tradelab::desktop::names;
using tradelab::desktop::testing::Driver;

namespace {

struct RavenSession {
  RavenSession() { REQUIRE(init_headless()); }
  ~RavenSession() { Rshutdown(); }
};

}  // namespace

TEST_CASE("headless academy: every lesson from the keyboard, each question wrong first, then right") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  testing::flow_every_lesson(app, d, true, true);
  CHECK(app.vm().progress().lesson_reviews.size() == web_content().lessons.size());
}

TEST_CASE("headless academy: the glossary, its search and a lesson term's definition on focus") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  testing::flow_glossary(app, d);
}

TEST_CASE("headless academy: the review prompt opens the Academy and the review there") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  std::int64_t now = 1'767'268'800'000;
  app.vm().set_clock([&now] { return now; });
  Driver d(app);
  d.frames(4);
  CHECK_FALSE(testing::visible(d, names::kMenuReviewRow));
  app.vm().open_lesson("risk-expectancy", Screen::Menu);
  d.frames(2);
  testing::play_lesson(app, d, false, false);
  REQUIRE(app.vm().screen() == Screen::Menu);
  now += 24LL * 3600 * 1000;
  app.vm().back_to_menu();  // a redraw on the next day
  d.frames(3);
  REQUIRE(testing::visible(d, names::kMenuReviewRow));
  REQUIRE(d.click(names::kMenuReviewOpen));
  REQUIRE(app.vm().screen() == Screen::Academy);
  CHECK(testing::visible(d, names::kAcademyDueBox));
  REQUIRE(d.click(names::kAcademyReviewStart));
  REQUIRE(app.vm().shell().lesson.mode() == LessonMode::Review);
  testing::play_lesson(app, d, false, true);
  REQUIRE(app.vm().screen() == Screen::Academy);
  CHECK(app.vm().progress().lesson_reviews.at("risk-expectancy").box == 2);
  CHECK_FALSE(testing::visible(d, names::kAcademyDueBox));
  REQUIRE(d.click(names::kAcademyBack));
}

TEST_CASE("headless academy: the guided first trade by keyboard: start, Space on the callout, skip, replay") {
  RavenSession raven;
  App app(builtin_catalog(), [] { return 1u; });
  REQUIRE(app.build(1280, 800));
  Driver d(app);
  d.frames(4);
  testing::flow_coach(app, d);
}
