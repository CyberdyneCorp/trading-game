// Port of the web's AudioDirector (apps/web/src/lib/game/audio.svelte.ts): one music track at a time
// from the playlist of the current mood (tracks alternate when one ends), a crossfade when the mood
// changes, stings as one-shots on top, mute and volumes. The web drives fades with a timer and hears
// "ended" from the element; here the frame calls tick(dt), which ramps volumes and polls the tracks.
// Plain C++: the player is an interface (miniaudio in the app, a fake in the tests).
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "viewmodel/audio/audio_rules.hpp"

namespace tradelab::desktop::audio {

// One playing sound. Volume 0..1.
class Track {
 public:
  virtual ~Track() = default;
  virtual void set_volume(float v) = 0;
  [[nodiscard]] virtual float volume() const = 0;
  virtual void play() = 0;
  virtual void pause() = 0;
  [[nodiscard]] virtual bool finished() const = 0;
};

// Opens the web's audio files by name ("menu-1.mp3"); nullptr when the file or the device is missing.
class AudioOut {
 public:
  virtual ~AudioOut() = default;
  virtual std::unique_ptr<Track> open(std::string_view file, bool stream) = 0;
};

// The web's playlists and stings (audio.svelte.ts PLAYLISTS, STINGS) and its fade length.
std::array<std::string_view, 2> playlist(Mood mood);
std::string_view sting_file(Sting s);
inline constexpr double kFadeSeconds = 1.2;

struct AudioSettings {
  bool muted = false;
  float music = 0.35F;
  float sfx = 0.7F;
  bool operator==(const AudioSettings&) const = default;
};

class AudioDirector {
 public:
  explicit AudioDirector(AudioOut* out = nullptr) : out_(out) {}

  void set_mood(Mood mood);
  void sting(Sting s);
  void set_muted(bool muted);
  void set_music_volume(float v);
  void set_sfx_volume(float v);
  void apply(const AudioSettings& s);  // only what changed
  void tick(double dt);

  [[nodiscard]] std::optional<Mood> mood() const { return mood_; }
  [[nodiscard]] const AudioSettings& settings() const { return settings_; }
  // The file of the music track playing now ("" when none), for tests and the smoke report.
  [[nodiscard]] std::string current_file() const { return current_ ? current_->file : std::string(); }

 private:
  struct Fade {
    Track* track = nullptr;
    float from = 0, to = 0;
    double elapsed = 0;
    bool pause_at_end = false;
  };
  struct Playing {
    std::string file;
    std::unique_ptr<Track> track;
  };

  std::unique_ptr<Playing> start(Mood mood);
  void fade_out(std::unique_ptr<Playing> p);
  void ramp(Track* t, float from, float to, bool pause_at_end);
  void stop_fades(const Track* t);
  void advance_fades(double dt);
  void release_done();
  [[nodiscard]] float target() const { return settings_.muted ? 0.0F : settings_.music; }

  AudioOut* out_;
  AudioSettings settings_;
  std::optional<Mood> mood_;
  std::unique_ptr<Playing> current_;
  std::vector<std::unique_ptr<Playing>> leaving_;  // fading out, then released
  std::vector<std::unique_ptr<Track>> stings_;     // released once finished
  std::vector<Fade> fades_;
  std::array<int, 3> next_{};
};

// The web's +page.svelte effects: the music follows the screen and the account's danger, stings follow
// trades that close (each run's history as first seen is a baseline), settings follow the prefs.
class Soundtrack {
 public:
  explicit Soundtrack(AudioOut* out = nullptr) : director_(out) {}
  // Once per frame. `g`: the run on screen (null outside a run).
  void update(bool terminal, const GameState* g, const std::string& run_id, const AudioSettings& s, double dt);
  [[nodiscard]] const AudioDirector& director() const { return director_; }

 private:
  AudioDirector director_;
  StingTracker stings_;
};

}  // namespace tradelab::desktop::audio
