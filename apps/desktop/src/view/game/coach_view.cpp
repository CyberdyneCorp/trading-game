#include "view/game/coach_view.hpp"

#include <raven/raven_accessibility.h>
#include <raven/raven_widget.h>

#include <string>

#include "view/raven/raven_ui.hpp"
#include "viewmodel/academy_names.hpp"

namespace tradelab::desktop::view {

namespace {

academy::Box rect_of(Rid w) {
  academy::Box b;
  Rwidget_get_rect(w, &b.x, &b.y, &b.w, &b.h);
  return b;
}

}  // namespace

Rid CoachView::find(std::string_view name) const { return ui::find(scope_, name); }

bool CoachView::shown(std::string_view callout) const {
  const Rid w = find(callout);
  return w != R_INVALID_ID && Rwidget_get_visibility(w) == R_VISIBLE;
}

bool CoachView::inside(Rid widget, Rid box) const {
  for (Rid w = widget; w != R_INVALID_ID; w = Rwidget_get_parent(w)) {
    if (w == box) return true;
  }
  return false;
}

void CoachView::place() {
  const CoachStepDef* step = vm_.coach().step();
  const Rid layer = find(names::kGameCoachLayer);
  const Rid callout = find(names::kGameCoach);
  if (step == nullptr || layer == R_INVALID_ID || callout == R_INVALID_ID) return;
  const Rid target = find(names::coach_anchor(step->target));
  const academy::Box area = rect_of(layer);
  float cw = 0;
  float ch = 0;
  Rwidget_get_desired_size(callout, &cw, &ch);
  const std::optional<academy::Box> t = target != R_INVALID_ID ? std::optional(rect_of(target)) : std::nullopt;
  const academy::Box at = academy::callout_box(t, cw, ch, area);
  Rslot_set_position(callout, at.x - area.x, at.y - area.y);
  const Rid outline = find(names::kGameCoachOutline);
  if (t) {
    Rslot_set_position(outline, t->x - area.x - 3, t->y - area.y - 3);
    Rslot_set_size(outline, t->w + 6, t->h + 6);
  }
  last_target_ = target;
}

// The tour left this screen (ended, or waits for the debrief): focus returns to the last target.
void CoachView::return_focus() {
  if (last_target_ != R_INVALID_ID && Rwidget_get_visibility(last_target_) == R_VISIBLE) Rwidget_focus(last_target_);
  last_target_ = R_INVALID_ID;
}

void CoachView::refresh() {
  const bool game = vm_.screen() == Screen::Game && shown(names::kGameCoach);
  const bool debrief = vm_.screen() == Screen::Debrief && shown(names::kDebriefCoach);
  if (game) place();
  const std::optional<int> now = game || debrief ? vm_.coach().index() : std::nullopt;
  if (now == seen_) return;
  if (!now && seen_ && vm_.screen() == Screen::Game) return_focus();
  seen_ = now;
  if (!now) return;
  // Opening a step moves the focus to the callout and says which step it is (a polite live region).
  Rwidget_focus(find(game ? names::kGameCoachNext : names::kDebriefCoachNext));
  const CoachStepDef* step = vm_.coach().step();
  const std::string said = vm_.tr("coach.stepOf", {{"n", std::to_string(*now + 1)}, {"total", std::to_string(vm_.coach().count())}}) +
                           ": " + vm_.tr("coach." + step->id + ".title");
  Raccessibility_announce(said.c_str(), R_FALSE);
}

std::optional<bool> CoachView::on_key(const REvent* e) {
  if (e->type != R_EVENT_KEY_DOWN) return std::nullopt;
  const bool game = vm_.screen() == Screen::Game && shown(names::kGameCoach);
  const bool debrief = vm_.screen() == Screen::Debrief && shown(names::kDebriefCoach);
  if (!game && !debrief) return std::nullopt;
  if (e->key == R_KEY_ESCAPE) {
    vm_.coach_skip();
    Raccessibility_announce(vm_.tr("coach.skipped").c_str(), R_FALSE);
    return true;
  }
  // Keys on the callout's own buttons are the buttons' (Raven activates them): never a shortcut.
  if (game && inside(Rwindow_get_focused_widget(window_), find(names::kGameCoach))) return false;
  return std::nullopt;
}

}  // namespace tradelab::desktop::view
