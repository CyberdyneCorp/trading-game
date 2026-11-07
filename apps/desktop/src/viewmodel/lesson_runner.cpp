#include "viewmodel/lesson_runner.hpp"

#include <cctype>
#include <cmath>
#include <utility>

#include "viewmodel/academy/ticket_sandbox.hpp"

namespace tradelab::desktop {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

// One token of the input at its start: how many bytes it takes and what it keeps ("" = dropped).
std::pair<std::size_t, std::string_view> typed_token(std::string_view rest) {
  if (rest.starts_with("\xE2\x88\x92") || rest.starts_with("\xE2\x80\x93")) return {3, "-"};  // − –
  if (rest.starts_with("\xC3\x97")) return {2, ""};                                           // ×
  if (rest.size() >= 2 && lower(rest[0]) == 'c' && lower(rest[1]) == 'r') return {2, ""};
  if (rest.size() == 1 && lower(rest[0]) == 'r') return {1, ""};  // "R" at the end
  if (rest[0] == '%' || std::isspace(static_cast<unsigned char>(rest[0])) != 0) return {1, ""};
  return {1, rest.substr(0, 1)};
}

// What lessonRun.svelte.ts strips before parseAmount: whitespace, "%", "×", "cr" (any case) and a
// final "R"; the minus signs U+2212 and U+2013 become "-".
std::string typed_clean(std::string_view input) {
  std::string s;
  for (std::size_t i = 0; i < input.size();) {
    const auto [n, keep] = typed_token(input.substr(i));
    s += keep;
    i += n;
  }
  return s;
}

}  // namespace

double read_typed_number(std::string_view input, Locale locale) { return fmt::parse_amount(typed_clean(input), locale); }

std::vector<const LessonStep*> review_steps(const AcademyLesson& lesson) {
  std::vector<const LessonStep*> out;
  for (const auto& s : lesson.steps) {
    if (s.is_question() && (s.phase == "decision" || s.phase == "transfer")) out.push_back(&s);
  }
  return out;
}

LessonRunner::LessonRunner(const AcademyLesson* lesson, LessonMode mode) : lesson_(lesson), mode_(mode) {
  if (lesson_ == nullptr) return;
  if (mode_ == LessonMode::Review) {
    steps_ = review_steps(*lesson_);
  } else {
    for (const auto& s : lesson_->steps) steps_.push_back(&s);
  }
}

const LessonStep* LessonRunner::step() const {
  if (index_ < 0 || index_ >= count()) return nullptr;
  return steps_[static_cast<std::size_t>(index_)];
}

bool LessonRunner::solved() const {
  const LessonStep* s = step();
  return s == nullptr || !s->is_question() || status_ == AnswerStatus::Right;
}

bool LessonRunner::last() const { return index_ >= count() - 1; }

void LessonRunner::grade(bool right) {
  const LessonStep* s = step();
  if (s == nullptr) return;
  first_try_.try_emplace(s->id, right);
  if (right) solved_ids_.insert(s->id);
  status_ = right ? AnswerStatus::Right : AnswerStatus::Wrong;
}

bool LessonRunner::pick(int k) {
  const LessonStep* s = step();
  if (s == nullptr || !s->question || s->question->type != StepQuestion::Type::Choice || solved()) return false;
  if (k < 0 || k >= static_cast<int>(s->question->choices.size())) return false;
  picked_ = k;
  grade(k == s->question->answer);
  return true;
}

bool LessonRunner::submit_number(std::string_view input, Locale locale) {
  const LessonStep* s = step();
  if (s == nullptr || !s->question || s->question->type != StepQuestion::Type::Number || solved()) return false;
  const double v = read_typed_number(input, locale);
  if (std::isnan(v)) return false;
  grade(academy::reaches_target(v, s->question->value, s->question->tolerance));
  return true;
}

bool LessonRunner::check_set(std::optional<double> value) {
  const LessonStep* s = step();
  if (s == nullptr || !s->question || s->question->type != StepQuestion::Type::Set || solved()) return false;
  grade(academy::reaches_target(value, s->question->value, s->question->tolerance));
  return true;
}

bool LessonRunner::next() {
  if (!solved() || finished_ || lesson_ == nullptr) return false;
  if (last()) {
    finished_ = true;
  } else {
    go(index_ + 1);
  }
  return true;
}

bool LessonRunner::prev() {
  if (index_ == 0 || finished_) return false;
  go(index_ - 1);
  return true;
}

void LessonRunner::go(int i) {
  index_ = i;
  picked_.reset();
  status_ = solved_ids_.contains(steps_[static_cast<std::size_t>(i)]->id) ? AnswerStatus::Right : AnswerStatus::Idle;
}

LessonRunner::Score LessonRunner::score() const {
  Score out;
  for (const LessonStep* s : steps_) {
    if (!s->is_question()) continue;
    ++out.total;
    const auto it = first_try_.find(s->id);
    if (it != first_try_.end() && it->second) ++out.right;
  }
  return out;
}

bool LessonRunner::all_right_first_try() const {
  const Score s = score();
  return s.right == s.total;
}

}  // namespace tradelab::desktop
