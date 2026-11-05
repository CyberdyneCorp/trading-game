#include "viewmodel/game/shortcuts.hpp"

#include <cctype>
#include <set>

namespace tradelab::desktop::game {

namespace {

constexpr std::array<std::string_view, 11> kIds = {"advance", "advance5", "buy",     "sell",    "close", "patterns",
                                                   "auto",    "mute",     "options", "journal", "help"};
constexpr std::array<std::string_view, 11> kDefaults = {"Space", "Shift+Space", "b", "s", "x", "p",
                                                        "a",     "m",           "o", "j", "?"};
constexpr std::array<std::string_view, 11> kLabels = {"help.advance", "help.advance5", "help.buy",    "help.sell",
                                                      "help.close",   "help.patterns", "help.auto",   "help.sound",
                                                      "help.options", "help.journal",  "help.help"};

}  // namespace

std::string_view action_id(Action a) { return kIds[static_cast<std::size_t>(a)]; }

std::string_view action_label_key(Action a) { return kLabels[static_cast<std::size_t>(a)]; }

bool is_reserved_key(std::string_view key) {
  static const std::set<std::string_view> reserved = {"Enter",     "Escape",  "Tab",  "ArrowLeft", "ArrowRight",
                                                      "ArrowUp",   "ArrowDown", "Home", "End",     "PageUp",
                                                      "PageDown"};
  constexpr std::string_view shift = "Shift+";
  if (key.substr(0, shift.size()) == shift) key.remove_prefix(shift.size());
  return reserved.contains(key);
}

// key.split('+').map(p => p.length === 1 ? p.toUpperCase() : p).join(' + ')
std::string key_label(std::string_view key) {
  std::string out;
  std::size_t start = 0;
  for (;;) {
    const std::size_t plus = key.find('+', start);
    const std::string_view part = key.substr(start, plus == std::string_view::npos ? key.size() - start : plus - start);
    if (part.size() == 1) out += static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
    else out += part;
    if (plus == std::string_view::npos) return out;
    out += " + ";
    start = plus + 1;
  }
}

Keymap::Keymap() { reset(); }

void Keymap::reset() {
  for (std::size_t i = 0; i < keys_.size(); ++i) keys_[i] = std::string(kDefaults[i]);
}

std::optional<Action> Keymap::action_for(std::string_view key) const {
  if (key.empty()) return std::nullopt;
  for (const Action a : kActions) {
    if (keys_[static_cast<std::size_t>(a)] == key) return a;
  }
  return std::nullopt;
}

Keymap::BindResult Keymap::bind(Action a, const std::string& key) {
  if (is_reserved_key(key)) return {BindError::Reserved, a};
  const auto other = action_for(key);
  if (other && *other != a) return {BindError::Conflict, *other};
  keys_[static_cast<std::size_t>(a)] = key;
  return {};
}

std::map<std::string, std::string> Keymap::to_map() const {
  std::map<std::string, std::string> out;
  for (const Action a : kActions) out[std::string(action_id(a))] = key(a);
  return out;
}

Keymap Keymap::sanitized(const std::map<std::string, std::string>& stored) {
  Keymap map;
  for (const Action a : kActions) {
    const auto it = stored.find(std::string(action_id(a)));
    if (it != stored.end() && !it->second.empty()) map.keys_[static_cast<std::size_t>(a)] = it->second;
  }
  std::set<std::string> seen;
  for (const auto& k : map.keys_) {
    if (is_reserved_key(k) || !seen.insert(k).second) return Keymap{};
  }
  return map;
}

}  // namespace tradelab::desktop::game
