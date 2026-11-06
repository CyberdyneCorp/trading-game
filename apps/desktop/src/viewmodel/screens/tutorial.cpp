// Tutorial before the briefing (web Tutorial.svelte): one card at a time, skippable, keyboard friendly
// (Enter / → next, ← previous, Esc skips: the view's key hook).
#include <algorithm>

#include "viewmodel/game/dialogs_vm.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

ScreenModel tutorial(const AppViewModel& vm) {
  ScreenModel m;
  const std::string& mission = vm.shell().run_mission;
  const auto cards = game::tutorial_cards(vm.game().texts(), mission);
  const int count = static_cast<int>(cards.size());
  const int i = count == 0 ? 0 : std::min(vm.tutorial_index(), count - 1);
  const bool last = i >= count - 1;
  m.text(names::kTutHead, vm.tr("tut.title") + std::string(kSep) + mission_title(vm, mission));
  m.text(names::kTutStep, vm.tr("tut.step", {{"n", std::to_string(i + 1)}, {"total", std::to_string(count)}}));
  if (count > 0) {
    m.text(names::kTutTitle, cards[static_cast<std::size_t>(i)].title);
    m.text(names::kTutBody, cards[static_cast<std::size_t>(i)].body);
  }
  m.text(names::kTutSkip, vm.tr("tut.skip"));
  m.text(names::kTutPrev, vm.tr("tut.prev"));
  m.text(names::kTutNext, vm.tr(last ? "tut.done" : "tut.next"));
  m.enable(names::kTutPrev, i > 0);
  return m;
}

}  // namespace tradelab::desktop::screens
