#include "viewmodel/game/hotkeys.hpp"

#include <array>
#include <string_view>

namespace tradelab::desktop::game {

namespace {

constexpr std::array<std::string_view, 8> kNavKeys = {"ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
                                                      "Home",    "End",       "PageUp",    "PageDown"};

// The web compares KeyboardEvent.key, which Shift does not change for these keys.
std::string_view base_key(std::string_view key) {
  constexpr std::string_view kShift = "Shift+";
  if (key.starts_with(kShift) && key.size() > kShift.size()) key.remove_prefix(kShift.size());
  return key;
}

bool nav_key(std::string_view key) {
  for (const auto k : kNavKeys) {
    if (k == key) return true;
  }
  return false;
}

}  // namespace

bool keeps_key(Focus focus, std::string_view key) {
  const std::string_view k = base_key(key);
  switch (focus) {
    case Focus::Text: return true;
    case Focus::Select: return k != "Space";
    case Focus::Slider:
    case Focus::Radio: return nav_key(k);
    case Focus::None:
    case Focus::Checkbox:
    case Focus::Button:
    case Focus::Other: return false;
  }
  return false;
}

}  // namespace tradelab::desktop::game
