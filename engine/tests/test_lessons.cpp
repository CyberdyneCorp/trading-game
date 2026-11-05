// Lesson questions and the understanding dimension (task 4.5, scoring v11).

#include <doctest/doctest.h>

#include <string>

#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/generator.hpp"
#include "tradelab/lessons.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

constexpr const char* kM01 = "m01-false-breakout";

Scenario m01() {
  Scenario sc;
  REQUIRE(build_mission(kM01, 7, sc));
  return sc;
}

Rubric reviewed(const Rubric& r) {
  Rubric copy = r;
  copy.reviewed = true;
  copy.reviewer = "test";
  return copy;
}

}  // namespace

TEST_CASE("Rubrics: the embedded data parses, covers every built-in mission and is not reviewed yet") {
  REQUIRE_FALSE(rubrics().empty());
  for (const MissionInfo& m : mission_catalog()) {
    const Rubric* r = rubric_for(m.id);
    REQUIRE_MESSAGE(r != nullptr, m.id);
    CHECK(r->questions.size() >= 2);
    // The flag is a human's sign-off: the questions ship unreviewed and do not score until then.
    CHECK_FALSE(r->reviewed);
    for (const LessonQuestion& q : r->questions) CHECK(q.points >= 1);
  }
  CHECK(rubric_for("csv-anything") == nullptr);
}

TEST_CASE("Rubrics: schema errors are refused") {
  std::vector<Rubric> out;
  const auto ok = [&out](const std::string& questions, const std::string& extra = "\"reviewed\":false,\"reviewer\":\"\"") {
    return parse_rubrics(R"({"version":1,"rubrics":[{"mission":"m","questions":[)" + questions + "]," + extra + "}]}", out);
  };
  CHECK(ok(R"({"id":"a","kind":"choice","choices":3,"answer":2,"points":1})"));
  CHECK(ok(R"({"id":"a","kind":"numeric","answer":"1.5","tolerance":"0.1","points":2})"));
  CHECK_FALSE(ok(R"({"id":"a","kind":"choice","choices":3,"answer":3,"points":1})"));   // answer out of range
  CHECK_FALSE(ok(R"({"id":"a","kind":"choice","choices":1,"answer":0,"points":1})"));   // one option is no question
  CHECK_FALSE(ok(R"({"id":"a","kind":"numeric","answer":"x","tolerance":"0","points":1})"));
  CHECK_FALSE(ok(R"({"id":"a","kind":"numeric","answer":"1","tolerance":"-1","points":1})"));
  CHECK_FALSE(ok(R"({"id":"a","kind":"text","points":1})"));                            // free text is a later, human step
  CHECK_FALSE(ok(R"({"id":"a","kind":"choice","choices":2,"answer":0,"points":0})"));
  CHECK_FALSE(ok(R"({"id":"a","kind":"choice","choices":2,"answer":0,"points":1},{"id":"a","kind":"choice","choices":2,"answer":0,"points":1})"));
  CHECK_FALSE(ok(R"({"id":"a","kind":"choice","choices":2,"answer":0,"points":1})", "\"reviewed\":true,\"reviewer\":\"\""));
  CHECK(ok(R"({"id":"a","kind":"choice","choices":2,"answer":0,"points":1})", "\"reviewed\":true,\"reviewer\":\"ana\""));
  CHECK_FALSE(parse_rubrics(R"({"version":2,"rubrics":[]})", out));
}

TEST_CASE("AnswerLesson: each question once, in range, recorded without touching the market") {
  const Scenario sc = m01();
  Driver d(sc);
  const RunState before = d.state();
  CHECK(d.lesson(0, 1).accepted);
  CHECK(d.lesson(0, 2).reason == Reason::AlreadyAnswered);
  CHECK(d.lesson(1, 0, "-5").reason == Reason::InvalidAnswer);
  CHECK(d.lesson(1, 0, "200").accepted);
  CHECK(d.lesson(2, 0).reason == Reason::UnknownQuestion);
  REQUIRE(d.state().lesson_answers.size() == 2);
  CHECK(d.state().lesson_answers[0].choice == 1);
  CHECK(d.state().lesson_answers[1].value == M("200"));
  CHECK(d.state().wallet == before.wallet);
  CHECK(d.state().events.size() == before.events.size());
  CHECK(d.state().step == before.step);
}

TEST_CASE("AnswerLesson: a scenario without a rubric has no questions") {
  const Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  Driver d(sc);
  CHECK(d.lesson(0, 0).reason == Reason::UnknownQuestion);
}

TEST_CASE("Understanding: not applicable until a reviewed rubric meets an answer") {
  const Scenario sc = m01();
  const Rubric& rubric = *rubric_for(kM01);
  Driver d(sc);
  d.step(3);
  // No answers: not applicable even with a reviewed rubric (the player never saw the questions).
  const Rubric checked = reviewed(rubric);
  CHECK_FALSE(score_run(sc, d.state(), &checked).understanding.score.has_value());
  CHECK(d.lesson(0, 1).accepted);  // right
  CHECK(d.lesson(1, 0, "150").accepted);  // wrong (2% of 10,000 is 200)
  // The shipped rubric is unreviewed: the answers are reported but not scored.
  const ScoreReport unreviewed = score_run(sc, d.state());
  CHECK_FALSE(unreviewed.understanding.score.has_value());
  CHECK_FALSE(unreviewed.lessons_reviewed);
  REQUIRE(unreviewed.lessons.size() == 2);
  CHECK(unreviewed.lessons[0].correct);
  CHECK_FALSE(unreviewed.lessons[1].correct);
  const ScoreReport scored = score_run(sc, d.state(), &checked);
  CHECK(scored.understanding.score == 50);
  CHECK(scored.lesson_points == 1);
  CHECK(scored.lesson_points_total == 2);
}

TEST_CASE("Understanding: a skipped question scores 0 and a numeric answer within tolerance is right") {
  const Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  std::vector<Rubric> rs;
  REQUIRE(parse_rubrics(R"({"version":1,"rubrics":[{"mission":"test","reviewed":true,"reviewer":"t","questions":[
    {"id":"a","kind":"numeric","answer":"90.45","tolerance":"0.5","points":3},
    {"id":"b","kind":"choice","choices":3,"answer":1,"points":1}]}]})",
                        rs));
  RunState st = start_run(sc, 1);
  st.lesson_answers.push_back({0, 0, 0, M("90.9"), false});
  st.lesson_answers.push_back({0, 1, 0, Money{}, true});
  const ScoreReport r = score_run(sc, st, &rs[0]);
  CHECK(r.lessons[0].correct);
  CHECK(r.lessons[1].skipped);
  CHECK(r.understanding.score == 75);  // 3 of 4 points
  st.lesson_answers[0].value = M("91");
  CHECK(score_run(sc, st, &rs[0]).understanding.score == 0);
}

TEST_CASE("Lesson answers enter the state digest only when present") {
  const Scenario sc = m01();
  Driver a(sc);
  Driver b(sc);
  CHECK(state_digest(a.state()) == state_digest(b.state()));
  CHECK(b.lesson(0, 1).accepted);
  CHECK(state_digest(a.state()) != state_digest(b.state()));
}

TEST_CASE("C ABI: tl_lesson lists questions without the key; the debrief reports answers and the key") {
  const std::string lesson = tl_lesson(kM01);
  CHECK(lesson.find(R"("id":"breakout-plan","kind":"choice","choices":3)") != std::string::npos);
  CHECK(lesson.find("\"answer\"") == std::string::npos);
  CHECK(std::string(tl_lesson("nope")) == R"({"error":"no_lesson"})");
  tl_run* run = tl_run_create(kM01, 7);
  REQUIRE(run != nullptr);
  CHECK(std::string(tl_answer_lesson(run, 1, 0, 1, nullptr, 0)).find("\"accepted\":true") != std::string::npos);
  CHECK(std::string(tl_answer_lesson(run, 2, 1, 0, "abc", 0)).find("invalid_answer") != std::string::npos);
  CHECK(std::string(tl_answer_lesson(run, 3, 1, 0, nullptr, 1)).find("\"accepted\":true") != std::string::npos);
  tl_advance(run, 10'000);
  const std::string d = tl_debrief(run);
  CHECK(d.find(R"("lessons":{"reviewed":false,"points":1,"total":2)") != std::string::npos);
  CHECK(d.find(R"("id":"breakout-plan","kind":"choice","points":1,"maxPoints":1,"answered":true,"skipped":false,"correct":true,"given":1,"answer":1)") !=
        std::string::npos);
  CHECK(d.find(R"("id":"risk-two-percent","kind":"numeric","points":0,"maxPoints":1,"answered":false,"skipped":true,"correct":false,"given":null,"answer":"200.00000000")") !=
        std::string::npos);
  CHECK(d.find(R"("understanding":{"weight":25,"score":null})") != std::string::npos);
  tl_run_destroy(run);
}
