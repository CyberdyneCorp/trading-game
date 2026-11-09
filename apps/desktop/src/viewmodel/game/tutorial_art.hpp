// Tutorial card art (web TutorialArt.svelte) as a display list: the schematic drawings of the strategy
// missions' cards (spread, covered, insurance, condor, roll) and of the wave 2 missions m18-m25 (scalp,
// carry, parity, crush, calendarSpread, scale, cooldown), drawn on the web's 160x100 view box scaled
// into the widget, in the terminal palette. Other art keys draw nothing on the desktop yet.
#pragma once

#include <string_view>

#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::game {

// True when the desktop draws this art key.
[[nodiscard]] bool has_tutorial_art(std::string_view art);
// The art in a width x height box (centred, aspect kept); empty for an art key it does not draw.
ChartScene tutorial_art_scene(const Texts& t, std::string_view art, float width, float height);

}  // namespace tradelab::desktop::game
