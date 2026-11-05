// Port of apps/web/src/lib/game/shortcuts.ts (names kept): the remappable shortcuts, their defaults
// (the web's DEFAULT_KEYMAP, from model/content) and the rules for changing one: a reserved key or
// one another action already uses is refused, naming it.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/content.hpp"

namespace tradelab::desktop {

using Keymap = std::map<std::string, std::string, std::less<>>;  // action -> key name

Keymap default_keymap(const WebContent& c);

// Stored name of a key press: a printable character is its lower-case self ("B" = "b", "?" = "?");
// a named key ("Space", "Enter", "ArrowLeft") keeps a "Shift+" prefix when Shift is held. "" for a
// modifier alone.
std::string key_name(std::string_view key, bool shift);
// How a stored key is shown: "Space", "Shift + Space", "B", "?".
std::string key_label(std::string_view key);
bool key_reserved(std::string_view key);
// The action bound to a key, or "".
std::string action_for(const WebContent& c, const Keymap& map, std::string_view key);

struct BindResult {
  enum class Error : std::uint8_t { None, Reserved, Conflict };
  Error error = Error::None;
  std::string other;  // the action that already uses the key (Conflict)
  Keymap map;         // the new map (None)
  [[nodiscard]] bool ok() const { return error == Error::None; }
};
BindResult bind_key(const WebContent& c, const Keymap& map, std::string_view action, std::string_view key);
// A stored map read back: unknown actions dropped, missing ones defaulted; the defaults when a key is
// bound twice or reserved.
Keymap sanitize_keymap(const WebContent& c, const std::map<std::string, std::string>& raw);

}  // namespace tradelab::desktop
