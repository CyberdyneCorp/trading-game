// AppViewModel: the Academy's options lab (web LabScreen.svelte, options-whatif spec "Lab in the
// Academy"): an illustrative market at 100 with legs the player edits, valued by the engine's run-less
// what-if (tl_option_whatif) on the simulation worker under price, time and IV shifts. Also a lesson
// step's what-if lab and strategy builder (web LessonWhatIf.svelte, LessonBuilder.svelte).
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

void AppViewModel::open_lab() {
  if (screen_ != Screen::Menu) return;
  academy_lab_.open_academy();
  screen_ = Screen::Lab;
  academy_lab_price_at_model();  // the call's premium is its model value, then the valuation
  touch();
}

void AppViewModel::academy_lab_changed() { lab_changed(academy_lab_); }

void AppViewModel::lesson_lab_changed() { lab_changed(lesson_lab_); }

// The lab's request after a change; an answer to an older request is dropped.
void AppViewModel::lab_changed(game::LabViewModel& lab) {
  const std::string request = lab.request();
  const auto ticket = lab.ticket.next(request);
  if (!ticket) return touch();
  const int t = *ticket;
  engine_.analyze(AnalysisKind::WhatIf, request, [alive = std::weak_ptr<bool>(alive_), lab = &lab, self = this, t](const std::string& text) {
    if (alive.expired() || !lab->ticket.current(t)) return;
    lab->answer(text);
    self->touch();
  });
  touch();
}

// A step with a what-if lab or a builder starts from the step's own legs or template.
void AppViewModel::reset_lesson_lab() {
  const LessonStep* step = shell_.lesson.step();
  if (step == nullptr) return;
  if (const auto* w = std::get_if<WhatIfWidget>(&step->widget)) {
    academy::open_whatif(lesson_lab_, *w);
    lesson_lab_changed();
  }
  if (const auto* b = std::get_if<BuilderWidget>(&step->widget)) {
    lesson_builder_.open(*b);
    lesson_builder_changed();
  }
}

void AppViewModel::lesson_builder_pick(game::Template t) {
  lesson_builder_.pick(t);
  lesson_builder_changed();
}

void AppViewModel::lesson_builder_short(bool on) {
  if (on == lesson_builder_.short_side()) return;
  lesson_builder_.set_short(on);
  lesson_builder_changed();
}

// The legs priced at the model first (entries unset), then analysed with those prices as the entries.
void AppViewModel::lesson_builder_changed() {
  const int t = lesson_builder_.next_ticket();
  const auto pricing = lesson_builder_.pricing_request();
  touch();
  if (!pricing) return;
  engine_.analyze(AnalysisKind::WhatIf, *pricing, [alive = std::weak_ptr<bool>(alive_), self = this, t](const std::string& text) {
    if (alive.expired() || !self->lesson_builder_.current(t)) return;
    const auto analysis = self->lesson_builder_.priced(text);
    self->touch();
    if (!analysis) return;
    self->engine_.analyze(AnalysisKind::WhatIf, *analysis, [alive, self, t](const std::string& reply) {
      if (alive.expired() || !self->lesson_builder_.current(t)) return;
      self->lesson_builder_.answer(reply);
      self->touch();
    });
  });
}

void AppViewModel::academy_lab_price_at_model() {
  engine_.analyze(AnalysisKind::WhatIf, academy_lab_.pricing_request(), [alive = std::weak_ptr<bool>(alive_), self = this](const std::string& text) {
    if (alive.expired()) return;
    self->academy_lab_.priced(text);
    self->academy_lab_changed();
  });
}

}  // namespace tradelab::desktop
