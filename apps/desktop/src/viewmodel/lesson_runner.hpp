// The Academy lesson runner (port of the web's lessonRun.svelte.ts LessonRun): the steps in order (all
// of them, or only the decision and transfer questions in a spaced review), the answer to the current
// question and the first try of each one. Choice, typed number (either decimal mark, the locale's
// thousands groups, an optional unit) and `set` questions (a ticket-sandbox value within a tolerance
// of a target) are graded here. A wrong answer explains and leaves the question open; Next unlocks once
// it is answered right, and a step answered right stays solved when the player comes back to it.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "model/content.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop {

enum class LessonMode : std::uint8_t { Lesson, Review };
enum class AnswerStatus : std::uint8_t { Idle, Right, Wrong };

// A typed answer as a number: "−1,20", "2.340" (PT-BR thousands), "6,25%" or "2 R"; NaN when invalid
// (lessonRun.svelte.ts readTypedNumber).
double read_typed_number(std::string_view input, Locale locale);

// The steps a spaced review asks: the decision and transfer questions, in order (academy.ts reviewSteps).
std::vector<const LessonStep*> review_steps(const AcademyLesson& lesson);

class LessonRunner {
 public:
  LessonRunner() = default;
  explicit LessonRunner(const AcademyLesson* lesson, LessonMode mode = LessonMode::Lesson);

  [[nodiscard]] const AcademyLesson* lesson() const { return lesson_; }
  [[nodiscard]] LessonMode mode() const { return mode_; }
  [[nodiscard]] int index() const { return index_; }
  [[nodiscard]] int count() const { return static_cast<int>(steps_.size()); }
  [[nodiscard]] const LessonStep* step() const;
  [[nodiscard]] bool finished() const { return finished_; }
  [[nodiscard]] std::optional<int> picked() const { return picked_; }
  [[nodiscard]] AnswerStatus status() const { return status_; }
  // A step without a question, or one answered right.
  [[nodiscard]] bool solved() const;
  [[nodiscard]] bool last() const;

  // Picks choice k of the current choice question (ignored once solved). Returns whether it changed anything.
  bool pick(int k);
  // A typed answer to a number question; nothing is graded until it reads as a number.
  bool submit_number(std::string_view input, Locale locale);
  // A set question checked against the sandbox's current value.
  bool check_set(std::optional<double> value);
  // Next step, or the end screen after the last one; only once solved.
  bool next();
  bool prev();

  struct Score {
    int right = 0;
    int total = 0;
  };
  [[nodiscard]] Score score() const;
  [[nodiscard]] bool all_right_first_try() const;

 private:
  void grade(bool right);
  void go(int i);

  const AcademyLesson* lesson_ = nullptr;
  LessonMode mode_ = LessonMode::Lesson;
  std::vector<const LessonStep*> steps_;
  int index_ = 0;
  std::optional<int> picked_;
  AnswerStatus status_ = AnswerStatus::Idle;
  std::map<std::string, bool> first_try_;
  std::set<std::string> solved_ids_;
  bool finished_ = false;
};

}  // namespace tradelab::desktop
