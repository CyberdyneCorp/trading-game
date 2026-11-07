// Assisted practice (web AssistPanel.svelte) in the terminal and the debrief, and the terminal's shell
// parts (the ASSISTED tag of the top bar, TopBar.svelte).
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

void assist_panel(const AppViewModel& vm, const names::AssistNames& n, ScreenModel& m) {
  const Snapshot* s = vm.snapshot();
  const bool shown = s != nullptr && s->game != nullptr && !vm.shell().run_id.empty();
  m.show(n.panel, shown);
  if (!shown) return;
  const bool allowed = vm.assist_allowed();
  const auto range = vm.rewind_range();
  const bool pack = !vm.run_pack().empty();
  m.text(n.title, vm.tr("assist.title"));
  m.text(n.note, vm.tr("assist.note"));
  m.text(n.blocked, vm.tr("assist.campaignBlocked"));
  m.show(n.note, allowed);
  m.show(n.blocked, !allowed);
  m.show(n.turn_label, allowed && range.has_value());
  m.show(n.turn, allowed && range.has_value());
  m.show(n.rewind, allowed && range.has_value());
  m.show(n.new_seed, allowed && !pack);
  if (range) {
    const int last = range->second - range->first + 1;
    m.text(n.turn_label, vm.tr("assist.rewindTo") + " (1\xE2\x80\x93" + std::to_string(last) + ")");
    m.text(n.turn, vm.assist_turn());
    m.name(n.turn, vm.tr("assist.rewindTo"));
    m.name(n.rewind, vm.tr("assist.rewind") + ": " + std::to_string(vm.assist_target_turn()));
  }
  m.text(n.rewind, vm.tr("assist.rewind"));
  m.text(n.new_seed, vm.tr("assist.newSeed"));
  m.enable(n.rewind, !vm.waiting());
  m.enable(n.new_seed, !vm.waiting());
}

ScreenModel game(const AppViewModel& vm) {
  ScreenModel m;
  for (auto& b : vm.bindings()) m.text(b.widget, std::move(b.text));
  m.text(names::kGameAssistedTag, vm.tr("assist.tag"));
  m.name(names::kGameAssistedTag, vm.tr("assist.banner"));
  m.show(names::kGameAssistedTag, vm.run_assisted().has_value());
  // Practice packs and assisted attempts start without the advisor (Terminal.svelte): the panel is
  // shown for every run this session knows.
  assist_panel(vm, names::kGameAssist, m);
  game_replay(vm, m);
  academy_game(vm, m);
  return m;
}

}  // namespace tradelab::desktop::screens
