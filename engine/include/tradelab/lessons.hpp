#pragma once

// Lesson questions and their answer keys (task 4.5, spec mission-scoring "No subjective LLM grading").
// Reviewed data, not code: the source is engine/rubrics/lessons.json, embedded at build time. The
// question and option texts live in the client catalogs under the same ids. A rubric scores the
// understanding dimension only once a human reviewer has set its `reviewed` flag.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tradelab/fixed.hpp"
#include "tradelab/state.hpp"

namespace tradelab {

struct LessonQuestion {
  std::string id;
  bool numeric = false;
  std::uint32_t choices = 0;  // multiple choice: number of options (2..6)
  std::uint32_t answer = 0;   // multiple choice: index of the right option
  Money value;                // numeric: the right value
  Money tolerance;            // numeric: the accepted absolute error
  std::uint32_t points = 1;
};

struct Rubric {
  std::string mission;
  bool reviewed = false;
  std::string reviewer;  // who reviewed it; empty until a human has
  std::vector<LessonQuestion> questions;
};

// Every embedded rubric (empty when the data does not parse, which test_lessons.cpp catches).
const std::vector<Rubric>& rubrics();
// The rubric of a mission, or nullptr.
const Rubric* rubric_for(std::string_view mission);
bool lesson_correct(const LessonQuestion& q, const LessonAnswer& a);

// Parses rubric data (the embedded file, or a test's own); false on any schema error.
bool parse_rubrics(std::string_view json, std::vector<Rubric>& out);

}  // namespace tradelab
