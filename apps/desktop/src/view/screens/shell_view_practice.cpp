// ShellView glue of the practice formats (add-practice-and-advanced-strategies D15-D19): the menu's
// practice buttons, the Drills, Daily challenge, Guess and Coach review screens, the debrief's coach
// button and "Retry from here", and the two charts (guess round, coach moment) painted from their scenes.
#include <raven/raven_controls.h>
#include <raven/raven_draw.h>
#include <raven/raven_window.h>
#include <raven/raven_widget.h>

#include "view/game/scene_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "view/screens/shell_view.hpp"
#include "viewmodel/practice_names.hpp"

namespace tradelab::desktop {

namespace {

AppViewModel& vm_of(void* user_data) { return *static_cast<AppViewModel*>(user_data); }

std::string text_or_empty(const char* text) { return text != nullptr ? text : ""; }

void paint_guess_chart(Rid /*w*/, RPainter* p, float width, float height, void* user_data) {
  view::paint_scene(p, vm_of(user_data).guess_scene(width, height));
}

void paint_review_chart(Rid /*w*/, RPainter* p, float width, float height, void* user_data) {
  view::paint_scene(p, vm_of(user_data).coach_review_scene(width, height));
}

void on_drills_timer(Rid, int checked, void* user_data) { vm_of(user_data).drills_set_timer(checked != 0); }
void on_drills_typed(Rid, const char* text, void* user_data) { vm_of(user_data).drills_set_typed(text_or_empty(text)); }
void on_drills_commit(Rid, const char*, void* user_data) { vm_of(user_data).drills_check(); }
void on_drills_lesson(Rid, int index, void* user_data) { vm_of(user_data).drills_pick_lesson(index); }

void on_drills_lesson_activated(Rid, int index, void* user_data) {
  vm_of(user_data).drills_pick_lesson(index);
  vm_of(user_data).drills_open_lesson();
}

void on_guess_decision(Rid, int index, void* user_data) { vm_of(user_data).guess_set_decision(index); }
void on_guess_p(Rid, int index, void* user_data) { vm_of(user_data).guess_set_p(index); }
void on_guess_inv(Rid, const char* text, void* user_data) { vm_of(user_data).guess_set_level(false, text_or_empty(text)); }
void on_guess_target(Rid, const char* text, void* user_data) { vm_of(user_data).guess_set_level(true, text_or_empty(text)); }
void on_guess_commit(Rid, const char*, void* user_data) { vm_of(user_data).guess_commit(); }
void on_retry_pick(Rid, int index, void* user_data) { vm_of(user_data).debrief_retry_pick(index); }

}  // namespace

// CoachReview.svelte: Esc closes the review; the arrows move between moments (no field to type in there).
bool ShellView::review_key(AppViewModel& vm, const REvent* e) {
  if (vm.screen() != Screen::CoachReview || e->type != R_EVENT_KEY_DOWN) return false;
  switch (e->key) {
    case R_KEY_ESCAPE: vm.coach_review_close(); return true;
    case R_KEY_RIGHT: vm.coach_review_step(1); return true;
    case R_KEY_LEFT: vm.coach_review_step(-1); return true;
    default: return false;
  }
}

void ShellView::bind_practice() {
  AppViewModel& vm = vm_;
  ui::bind_action(names::kActOpenDrills, [&vm] { vm.open_drills(); });
  ui::bind_action(names::kActOpenDaily, [&vm] { vm.open_daily(); });
  ui::bind_action(names::kActOpenGuess, [&vm] { vm.open_guess(); });
  ui::bind_action(names::kActDrillsStart, [&vm] { vm.drills_start(); });
  for (std::size_t k = 0; k < names::kActDrillsChoice.size(); ++k) {
    ui::bind_action(names::kActDrillsChoice[k], [&vm, k] { vm.drills_choose(static_cast<int>(k)); });
  }
  ui::bind_action(names::kActDrillsCheck, [&vm] { vm.drills_check(); });
  ui::bind_action(names::kActDrillsNext, [&vm] { vm.drills_next(); });
  ui::bind_action(names::kActDrillsLesson, [&vm] { vm.drills_open_lesson(); });
  ui::bind_action(names::kActDailyPlay, [&vm] { vm.daily_play(); });
  ui::bind_action(names::kActGuessStart, [&vm] { vm.guess_start(); });
  ui::bind_action(names::kActGuessCommit, [&vm] { vm.guess_commit(); });
  ui::bind_action(names::kActReviewOpen, [&vm] { vm.open_coach_review(); });
  ui::bind_action(names::kActReviewClose, [&vm] { vm.coach_review_close(); });
  ui::bind_action(names::kActReviewPrev, [&vm] { vm.coach_review_step(-1); });
  ui::bind_action(names::kActReviewNext, [&vm] { vm.coach_review_step(1); });
  ui::bind_action(names::kActReviewRetry, [&vm] { vm.coach_review_retry(); });
  ui::bind_action(names::kActReviewLesson, [&vm] { vm.coach_review_lesson(); });
  ui::bind_action(names::kActReviewTerm, [&vm] { vm.coach_review_term(); });
  ui::bind_action(names::kActReviewReveal, [&vm] { vm.coach_review_to_reveal(); });
  ui::bind_action(names::kActDebriefRetry, [&vm] { vm.debrief_retry(); });
  Rcheckbox_set_changed_callback(find(names::kDrillsTimer), on_drills_timer, &vm_);
  Rtext_input_set_changed_callback(find(names::kDrillsInput), on_drills_typed, &vm_);
  Rtext_input_set_committed_callback(find(names::kDrillsInput), on_drills_commit, &vm_);
  Rlist_view_set_selection_changed_callback(find(names::kDrillsLessons), on_drills_lesson, &vm_);
  Rlist_view_set_activated_callback(find(names::kDrillsLessons), on_drills_lesson_activated, &vm_);
  Rcombo_box_set_changed_callback(find(names::kGuessDecision), on_guess_decision, &vm_);
  Rcombo_box_set_changed_callback(find(names::kGuessP), on_guess_p, &vm_);
  Rtext_input_set_changed_callback(find(names::kGuessInv), on_guess_inv, &vm_);
  Rtext_input_set_changed_callback(find(names::kGuessTarget), on_guess_target, &vm_);
  Rtext_input_set_committed_callback(find(names::kGuessTarget), on_guess_commit, &vm_);
  Rcombo_box_set_changed_callback(find(names::kDebriefRetryPick), on_retry_pick, &vm_);
  Rcustom_set_paint_callback(find(names::kGuessChart), paint_guess_chart, &vm_);
  Rcustom_set_paint_callback(find(names::kReviewChart), paint_review_chart, &vm_);
}

}  // namespace tradelab::desktop
