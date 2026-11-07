#include "viewmodel/academy/coach.hpp"

#include <algorithm>

namespace tradelab::desktop::academy {

Box callout_box(const std::optional<Box>& target, float width, float height, const Box& area) {
  constexpr float kMargin = 8;
  constexpr float kGap = 10;
  const auto clamp = [](float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); };
  const float bottom_limit = area.y + area.h - height - kMargin;
  const float right_limit = area.x + area.w - width - kMargin;
  if (!target) {
    return {std::max(area.x + kMargin, area.x + (area.w - width) / 2), std::max(area.y + kMargin, area.y + (area.h - height) / 2), width, height};
  }
  const float below = target->y + target->h + kGap;
  const float above = target->y - height - kGap;
  float top = target->y + target->h - height - kMargin;
  if (below + height < area.y + area.h) {
    top = below;
  } else if (above > area.y + kMargin) {
    top = above;
  }
  return {clamp(target->x, area.x + kMargin, right_limit), clamp(top, area.y + kMargin, bottom_limit), width, height};
}

void CoachModel::set_steps(std::vector<CoachStepDef> steps) {
  steps_ = std::move(steps);
  index_.reset();
}

const CoachStepDef* CoachModel::step() const {
  if (!index_ || *index_ < 0 || *index_ >= count()) return nullptr;
  return &steps_[static_cast<std::size_t>(*index_)];
}

int CoachModel::terminal_steps() const {
  return static_cast<int>(std::ranges::count_if(steps_, [](const CoachStepDef& s) { return s.target != "debrief"; }));
}

void CoachModel::done() {
  if (on_done) on_done();
}

void CoachModel::start(const std::string& run_id) {
  run_id_ = run_id;
  requested = false;
  index_ = steps_.empty() ? std::nullopt : std::optional<int>(0);
}

void CoachModel::maybe_start(const std::string& run_id, bool first_campaign_terminal) {
  if (active() && run_id_ == run_id) return;
  if (requested || first_campaign_terminal) {
    start(run_id);
  } else {
    index_.reset();
  }
}

void CoachModel::next() {
  if (!index_) return;
  const int n = *index_ + 1;
  if (n == terminal_steps()) done();
  index_ = n < count() ? std::optional<int>(n) : std::nullopt;
}

void CoachModel::skip() {
  if (!index_) return;
  index_.reset();
  done();
}

void CoachModel::left_terminal(bool to_debrief) {
  if (!index_ || *index_ >= terminal_steps()) return;
  if (to_debrief) {
    index_ = terminal_steps();
    done();
  } else {
    index_.reset();
  }
}

void CoachModel::debrief_opened(const std::string& run_id) {
  if (waiting_for_debrief() && run_id != run_id_) index_.reset();
}

}  // namespace tradelab::desktop::academy
