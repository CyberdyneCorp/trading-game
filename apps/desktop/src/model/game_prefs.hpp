// Per-device terminal progress and preferences (the web keeps them in localStorage and its progress
// record): the shortcut map, tutorials seen, advisor point balance with the runs already settled,
// finished missions (intel perks unlock by them), perks already introduced, sound (mute, volumes) and
// the terminal's panel layout. Stored as one small JSON
// file in the user-data folder until the SQLite store (task 1.9) takes them over; a damaged or missing
// file reads as defaults, and a failed write keeps the values for the session.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tradelab::desktop {

// Music and effects (the web's tradelab.audio.v1: mute and volumes 0..1).
struct AudioPrefs {
  bool muted = false;
  double music = 0.35;
  double sfx = 0.7;
  bool operator==(const AudioPrefs&) const = default;
};

struct GamePrefs {
  std::map<std::string, std::string> keymap;  // action -> key name (web shortcuts.ts names)
  std::vector<std::string> tutorials_seen;
  int advisor_points = 3;
  std::vector<std::string> settled_runs;
  std::vector<std::string> finished;
  std::vector<std::string> perks_introduced;
  AudioPrefs audio;
  std::string layout;  // the terminal's docked panels (Raven layout text); "" = the default arrangement
};

std::string prefs_to_json(const GamePrefs& prefs);
// Unknown fields are ignored; anything unreadable falls back to the default.
GamePrefs prefs_from_json(std::string_view text);

// Where the file lives: $TRADELAB_DATA_DIR when set (tests), else macOS
// ~/Library/Application Support/TradeLab, Linux $XDG_DATA_HOME/tradelab or ~/.local/share/tradelab.
std::string default_prefs_path();

class PrefsFile {
 public:
  explicit PrefsFile(std::string path) : path_(std::move(path)) {}
  [[nodiscard]] GamePrefs load() const;
  // Writes atomically (temporary file + rename); false when the folder cannot be written.
  bool save(const GamePrefs& prefs) const;
  [[nodiscard]] const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace tradelab::desktop
