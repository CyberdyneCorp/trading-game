// Port of apps/web/src/lib/game/hotkeys.ts (parity fixture tests/fixtures/web/hotkeys.json): which
// focused control keeps a key for itself instead of the game's shortcuts. Text entry keeps every key;
// a select (Raven ComboBox) keeps its keys but not Space; a slider or radio keeps only its navigation
// keys; a checkbox or button keeps none, so after clicking "BB20" Space still advances.
#pragma once

#include <cstdint>
#include <string_view>

namespace tradelab::desktop::game {

// The kind of the focused control (the web reads the DOM element; the view maps Raven widget types).
enum class Focus : std::uint8_t { None, Text, Select, Slider, Radio, Checkbox, Button, Other };

// `key`: a key as the shortcuts name it ("Space", "Shift+Space", "b", "ArrowUp", "Enter").
bool keeps_key(Focus focus, std::string_view key);

}  // namespace tradelab::desktop::game
