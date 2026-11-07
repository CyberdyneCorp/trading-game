// The guided first trade's coach marks on Raven (web CoachMarks.svelte): each frame the terminal's
// callout is placed next to its target (a docked panel can move or resize) with an outline over the
// target; a new step takes the focus to the callout's Next button and is announced; when the tour
// leaves the screen the focus returns to the last target. Keys: Esc skips; Space and Enter on the
// callout's buttons act on the buttons and never reach the terminal's shortcuts. The texts and which
// step shows are the viewmodel's (screens::coach_callout).
#pragma once

#include <raven/raven_core.h>
#include <raven/raven_window.h>

#include <optional>
#include <string_view>

#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop::view {

class CoachView {
 public:
  CoachView(AppViewModel& vm, Rid scope, Rid window) : vm_(vm), scope_(scope), window_(window) {}

  // Once per frame, after the screen's model was applied.
  void refresh();
  // The window's key hook: a value when the coach decides (true = used), none when it is not its key.
  std::optional<bool> on_key(const REvent* e);

 private:
  [[nodiscard]] Rid find(std::string_view name) const;
  [[nodiscard]] bool shown(std::string_view callout) const;
  [[nodiscard]] bool inside(Rid widget, Rid box) const;
  void place();
  void return_focus();

  AppViewModel& vm_;
  Rid scope_;
  Rid window_;
  std::optional<int> seen_;  // the step whose callout was focused and announced
  Rid last_target_ = R_INVALID_ID;
};

}  // namespace tradelab::desktop::view
