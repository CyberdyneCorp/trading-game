// Port of apps/web/src/lib/game/shortcuts.ts: the remappable terminal shortcuts, their web defaults,
// key names ("Space", "Shift+Space", "b", "?"), and the checks a new binding must pass (Enter, Esc,
// Tab, arrows, Home/End and Page keys are reserved; one key per action).
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace tradelab::desktop::game {

enum class Action : unsigned char { Advance, Advance5, Buy, Sell, Close, Patterns, Auto, Mute, Options, Journal, Help };

inline constexpr std::array<Action, 11> kActions = {Action::Advance, Action::Advance5, Action::Buy,    Action::Sell,
                                                    Action::Close,   Action::Patterns, Action::Auto,  Action::Mute,
                                                    Action::Options, Action::Journal,  Action::Help};

// Stable id of an action ("advance", "advance5", ...), as the web stores it.
std::string_view action_id(Action a);
// Catalog key of the action's label (help.advance...).
std::string_view action_label_key(Action a);

class Keymap {
 public:
  Keymap();  // web defaults

  [[nodiscard]] const std::string& key(Action a) const { return keys_[static_cast<std::size_t>(a)]; }
  [[nodiscard]] std::optional<Action> action_for(std::string_view key) const;

  enum class BindError : unsigned char { None, Reserved, Conflict };
  struct BindResult {
    BindError error = BindError::None;
    Action other = Action::Advance;  // the action already using the key (Conflict)
  };
  BindResult bind(Action a, const std::string& key);
  void reset();

  [[nodiscard]] std::map<std::string, std::string> to_map() const;
  // Stored map read back: unknown actions dropped, missing ones defaulted, an invalid result -> defaults.
  static Keymap sanitized(const std::map<std::string, std::string>& stored);

  bool operator==(const Keymap&) const = default;

 private:
  std::array<std::string, 11> keys_;
};

bool is_reserved_key(std::string_view key);
// "Space" -> "Space", "Shift+Space" -> "Shift + Space", "b" -> "B".
std::string key_label(std::string_view key);

}  // namespace tradelab::desktop::game
