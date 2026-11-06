// The Academy lesson runner (web LessonRunner.svelte + academy.ts firstTryScore): concept,
// demonstration, decisions with feedback after each answer, and a transfer exercise. A wrong answer
// explains and lets the player choose again; the first try is what counts for the score.
#pragma once

#include <map>
#include <optional>
#include <string>

#include "model/content.hpp"

namespace tradelab::desktop {

class LessonRunner {
 public:
  LessonRunner() = default;
  explicit LessonRunner(const AcademyLesson* lesson) : lesson_(lesson) {}

  [[nodiscard]] const AcademyLesson* lesson() const { return lesson_; }
  [[nodiscard]] int index() const { return index_; }
  [[nodiscard]] const LessonStep* step() const;
  [[nodiscard]] bool finished() const { return finished_; }
  [[nodiscard]] std::optional<int> picked() const { return picked_; }
  // A step without a question, or one whose right answer was picked.
  [[nodiscard]] bool solved() const;
  [[nodiscard]] bool last() const;

  // Picks choice k of the current question (ignored once solved). Returns whether it changed anything.
  bool pick(int k);
  // Next step, or the end screen after the last one; only once solved.
  bool next();
  bool prev();

  struct Score {
    int right = 0;
    int total = 0;
  };
  [[nodiscard]] Score score() const;

 private:
  const AcademyLesson* lesson_ = nullptr;
  int index_ = 0;
  std::optional<int> picked_;
  std::map<std::string, bool> first_try_;
  bool finished_ = false;
};

}  // namespace tradelab::desktop
