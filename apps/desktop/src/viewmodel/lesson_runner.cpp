#include "viewmodel/lesson_runner.hpp"

namespace tradelab::desktop {

const LessonStep* LessonRunner::step() const {
  if (lesson_ == nullptr || index_ < 0 || index_ >= static_cast<int>(lesson_->steps.size())) return nullptr;
  return &lesson_->steps[static_cast<std::size_t>(index_)];
}

bool LessonRunner::solved() const {
  const LessonStep* s = step();
  return s == nullptr || !s->question() || picked_ == s->answer;
}

bool LessonRunner::last() const { return lesson_ != nullptr && index_ >= static_cast<int>(lesson_->steps.size()) - 1; }

bool LessonRunner::pick(int k) {
  const LessonStep* s = step();
  if (s == nullptr || !s->question() || solved() || k < 0 || k >= static_cast<int>(s->choices.size())) return false;
  picked_ = k;
  first_try_.try_emplace(s->id, k == s->answer);
  return true;
}

bool LessonRunner::next() {
  if (!solved() || finished_ || lesson_ == nullptr) return false;
  if (last()) {
    finished_ = true;
  } else {
    ++index_;
    picked_.reset();
  }
  return true;
}

bool LessonRunner::prev() {
  if (index_ == 0 || finished_) return false;
  --index_;
  picked_.reset();
  return true;
}

LessonRunner::Score LessonRunner::score() const {
  Score out;
  if (lesson_ == nullptr) return out;
  for (const auto& s : lesson_->steps) {
    if (!s.question()) continue;
    ++out.total;
    const auto it = first_try_.find(s.id);
    if (it != first_try_.end() && it->second) ++out.right;
  }
  return out;
}

}  // namespace tradelab::desktop
