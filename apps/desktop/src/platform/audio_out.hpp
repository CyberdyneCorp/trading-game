// The soundtrack's player (viewmodel/audio AudioOut) on miniaudio (third_party/miniaudio): the web's
// MP3 files decoded and mixed by one ma_engine on the system's output (Core Audio; PulseAudio, ALSA or
// JACK opened at run time on Linux). No device, no files or TRADELAB_AUDIO=off: no player, the game is
// silent and plays on. TRADELAB_AUDIO=null uses miniaudio's null device: everything decodes and mixes,
// nothing is heard (tests).
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "viewmodel/audio/audio_director.hpp"

namespace tradelab::desktop::platform {

// Where the web's audio files are: $TRADELAB_AUDIO_DIR, else next to the executable (TradeLab.app:
// Contents/Resources/audio; the AppImage: usr/share/tradelab/audio), else `source_dir` (the source
// tree's apps/web/static/audio, for a build-folder binary). "" when none has menu-1.mp3.
std::string audio_dir(const std::string& source_dir);

class MiniaudioOut final : public audio::AudioOut {
 public:
  ~MiniaudioOut() override;
  MiniaudioOut(const MiniaudioOut&) = delete;
  MiniaudioOut& operator=(const MiniaudioOut&) = delete;

  // nullptr when the device cannot be opened.
  static std::unique_ptr<MiniaudioOut> create(std::string dir, bool null_device);
  // As the environment asks (TRADELAB_AUDIO=off|null), from audio_dir(source_dir).
  static std::unique_ptr<MiniaudioOut> from_environment(const std::string& source_dir);

  std::unique_ptr<audio::Track> open(std::string_view file, bool stream) override;
  [[nodiscard]] const std::string& dir() const { return dir_; }

 private:
  struct Impl;
  MiniaudioOut(std::string dir, std::unique_ptr<Impl> impl);

  std::string dir_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tradelab::desktop::platform
