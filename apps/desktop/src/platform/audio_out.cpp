#include "platform/audio_out.hpp"

#include <miniaudio.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

#include "platform/platform.hpp"

namespace tradelab::desktop::platform {

namespace {

namespace fs = std::filesystem;

bool has_tracks(const fs::path& dir) {
  std::error_code ec;
  return !dir.empty() && fs::is_regular_file(dir / "menu-1.mp3", ec);
}

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr ? v : "";
}

class SoundTrack final : public audio::Track {
 public:
  explicit SoundTrack(std::unique_ptr<ma_sound> sound) : sound_(std::move(sound)) {}
  ~SoundTrack() override { ma_sound_uninit(sound_.get()); }
  SoundTrack(const SoundTrack&) = delete;
  SoundTrack& operator=(const SoundTrack&) = delete;

  void set_volume(float v) override { ma_sound_set_volume(sound_.get(), v); }
  [[nodiscard]] float volume() const override { return ma_sound_get_volume(sound_.get()); }
  void play() override { ma_sound_start(sound_.get()); }
  void pause() override { ma_sound_stop(sound_.get()); }
  [[nodiscard]] bool finished() const override { return ma_sound_at_end(sound_.get()) != MA_FALSE; }

 private:
  std::unique_ptr<ma_sound> sound_;
};

}  // namespace

struct MiniaudioOut::Impl {
  ma_context context{};
  bool has_context = false;
  ma_engine engine{};
  bool has_engine = false;

  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  ~Impl() {
    if (has_engine) ma_engine_uninit(&engine);
    if (has_context) ma_context_uninit(&context);
  }
};

std::string audio_dir(const std::string& source_dir) {
  if (const std::string dir = env("TRADELAB_AUDIO_DIR"); !dir.empty()) return has_tracks(dir) ? dir : std::string();
  const fs::path exe = executable_path();
  if (!exe.empty()) {
    const fs::path bin = exe.parent_path();
    for (const fs::path& dir : {bin / "../Resources/audio", bin / "../share/tradelab/audio"}) {
      if (has_tracks(dir)) return fs::weakly_canonical(dir).string();
    }
  }
  return has_tracks(source_dir) ? source_dir : std::string();
}

MiniaudioOut::MiniaudioOut(std::string dir, std::unique_ptr<Impl> impl) : dir_(std::move(dir)), impl_(std::move(impl)) {}

MiniaudioOut::~MiniaudioOut() = default;

std::unique_ptr<MiniaudioOut> MiniaudioOut::create(std::string dir, bool null_device) {
  auto impl = std::make_unique<Impl>();
  ma_engine_config config = ma_engine_config_init();
  if (null_device) {
    const std::array<ma_backend, 1> backends = {ma_backend_null};
    if (ma_context_init(backends.data(), 1, nullptr, &impl->context) != MA_SUCCESS) return nullptr;
    impl->has_context = true;
    config.pContext = &impl->context;
  }
  if (ma_engine_init(&config, &impl->engine) != MA_SUCCESS) return nullptr;
  impl->has_engine = true;
  return std::unique_ptr<MiniaudioOut>(new MiniaudioOut(std::move(dir), std::move(impl)));
}

std::unique_ptr<MiniaudioOut> MiniaudioOut::from_environment(const std::string& source_dir) {
  const std::string mode = env("TRADELAB_AUDIO");
  if (mode == "off") return nullptr;
  std::string dir = audio_dir(source_dir);
  if (dir.empty()) return nullptr;
  return create(std::move(dir), mode == "null");
}

std::unique_ptr<audio::Track> MiniaudioOut::open(std::string_view file, bool stream) {
  const std::string path = (fs::path(dir_) / std::string(file)).string();
  auto sound = std::make_unique<ma_sound>();
  // Music streams from the file; a sting is decoded up front so it starts on time.
  const ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
  if (ma_sound_init_from_file(&impl_->engine, path.c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
    return nullptr;
  }
  return std::make_unique<SoundTrack>(std::move(sound));
}

}  // namespace tradelab::desktop::platform
