#include "viewmodel/keymap.hpp"

#include <array>
#include <set>

namespace tradelab::desktop {

namespace {

// Enter submits, Escape closes, Tab moves the focus, arrows and Home/End/Page move the chart's focus:
// never a shortcut, with or without Shift.
constexpr std::array<std::string_view, 11> kReserved = {"Enter",     "Escape",  "Tab",  "ArrowLeft", "ArrowRight", "ArrowUp",
                                                        "ArrowDown", "Home",    "End",  "PageUp",    "PageDown"};
constexpr std::array<std::string_view, 8> kModifiers = {"Shift", "Control",      "Alt",    "Meta",
                                                        "CapsLock", "Dead", "Unidentified", "Process"};

template <std::size_t N>
bool in(const std::array<std::string_view, N>& set, std::string_view key) {
  for (std::string_view k : set) {
    if (k == key) return true;
  }
  return false;
}

// Lower-cases one ASCII letter; other characters (punctuation, UTF-8 bytes) stay.
std::string lower_ascii(std::string_view s) {
  std::string out(s);
  for (char& ch : out) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return out;
}

bool printable(std::string_view key) { return key.size() == 1; }

}  // namespace

Keymap default_keymap(const WebContent& c) {
  Keymap map;
  for (const auto& [action, key] : c.default_keymap) map[action] = key;
  return map;
}

std::string key_name(std::string_view key, bool shift) {
  if (in(kModifiers, key)) return {};
  const std::string name = key == " " ? std::string("Space") : printable(key) ? lower_ascii(key) : std::string(key);
  return shift && name.size() > 1 ? "Shift+" + name : name;
}

// key.split('+').map(upper-case single characters).join(' + '), as the web.
std::string key_label(std::string_view key) {
  std::string out;
  std::size_t start = 0;
  for (bool first = true;; first = false) {
    const std::size_t plus = key.find('+', start);
    std::string part(key.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start));
    if (part.size() == 1 && part[0] >= 'a' && part[0] <= 'z') part[0] = static_cast<char>(part[0] - 'a' + 'A');
    out += (first ? "" : " + ") + part;
    if (plus == std::string_view::npos) return out;
    start = plus + 1;
  }
}

bool key_reserved(std::string_view key) {
  constexpr std::string_view kShift = "Shift+";
  if (key.starts_with(kShift) && key.size() > kShift.size()) key.remove_prefix(kShift.size());
  return in(kReserved, key);
}

std::string action_for(const WebContent& c, const Keymap& map, std::string_view key) {
  if (key.empty()) return {};
  for (const auto& action : c.shortcut_actions) {
    const auto it = map.find(action);
    if (it != map.end() && it->second == key) return action;
  }
  return {};
}

BindResult bind_key(const WebContent& c, const Keymap& map, std::string_view action, std::string_view key) {
  BindResult r;
  if (key_reserved(key)) {
    r.error = BindResult::Error::Reserved;
    return r;
  }
  const std::string other = action_for(c, map, key);
  if (!other.empty() && other != action) {
    r.error = BindResult::Error::Conflict;
    r.other = other;
    return r;
  }
  r.map = map;
  r.map[std::string(action)] = std::string(key);
  return r;
}

Keymap sanitize_keymap(const WebContent& c, const std::map<std::string, std::string>& raw) {
  Keymap map = default_keymap(c);
  for (const auto& action : c.shortcut_actions) {
    const auto it = raw.find(action);
    if (it != raw.end() && !it->second.empty()) map[action] = it->second;
  }
  std::set<std::string> keys;
  for (const auto& action : c.shortcut_actions) {
    const std::string& key = map[action];
    if (key_reserved(key) || !keys.insert(key).second) return default_keymap(c);
  }
  return map;
}

}  // namespace tradelab::desktop
