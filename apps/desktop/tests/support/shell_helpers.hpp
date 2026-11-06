// Helpers for shell viewmodel tests.
#pragma once

#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop::testing {

// Marks every mission's tutorial as seen, so a start goes straight to the briefing.
inline void tutorials_seen(AppViewModel& vm) {
  for (const auto& m : vm.missions()) vm.game().mark_tutorial_seen(m.id);
}

}  // namespace tradelab::desktop::testing
