#include "viewmodel/audio/audio_director.hpp"

#include <algorithm>

namespace tradelab::desktop::audio {

std::array<std::string_view, 2> playlist(Mood mood) {
  switch (mood) {
    case Mood::Menu: return {"menu-1.mp3", "menu-2.mp3"};
    case Mood::Game: return {"game-1.mp3", "game-2.mp3"};
    case Mood::Tense: return {"tense-1.mp3", "tense-2.mp3"};
  }
  return {"menu-1.mp3", "menu-2.mp3"};
}

std::string_view sting_file(Sting s) { return s == Sting::Win ? "trade-win.mp3" : "trade-loss.mp3"; }

void AudioDirector::set_mood(Mood mood) {
  if (mood_ == mood) return;
  mood_ = mood;
  if (settings_.muted) return;
  fade_out(std::move(current_));
  current_ = start(mood);
}

void AudioDirector::sting(Sting s) {
  if (settings_.muted || settings_.sfx <= 0 || out_ == nullptr) return;
  auto track = out_->open(sting_file(s), false);
  if (!track) return;
  track->set_volume(settings_.sfx);
  track->play();
  stings_.push_back(std::move(track));
}

void AudioDirector::set_muted(bool muted) {
  settings_.muted = muted;
  if (muted) {
    fade_out(std::move(current_));
  } else if (mood_ && !current_) {
    current_ = start(*mood_);
  }
}

void AudioDirector::set_music_volume(float v) {
  settings_.music = std::clamp(v, 0.0F, 1.0F);
  if (!current_) return;
  stop_fades(current_->track.get());
  current_->track->set_volume(target());
}

void AudioDirector::set_sfx_volume(float v) { settings_.sfx = std::clamp(v, 0.0F, 1.0F); }

void AudioDirector::apply(const AudioSettings& s) {
  if (s.music != settings_.music) set_music_volume(s.music);
  if (s.sfx != settings_.sfx) set_sfx_volume(s.sfx);
  if (s.muted != settings_.muted) set_muted(s.muted);
}

std::unique_ptr<AudioDirector::Playing> AudioDirector::start(Mood mood) {
  if (out_ == nullptr) return nullptr;
  const auto list = playlist(mood);
  int& next = next_.at(static_cast<std::size_t>(mood));
  const std::string file(list.at(static_cast<std::size_t>(next) % list.size()));
  next += 1;
  auto track = out_->open(file, true);
  if (!track) return nullptr;
  track->set_volume(0);
  track->play();
  auto playing = std::make_unique<Playing>(Playing{file, std::move(track)});
  ramp(playing->track.get(), 0, target(), false);
  return playing;
}

void AudioDirector::fade_out(std::unique_ptr<Playing> p) {
  if (!p) return;
  stop_fades(p->track.get());
  ramp(p->track.get(), p->track->volume(), 0, true);
  leaving_.push_back(std::move(p));
}

void AudioDirector::ramp(Track* t, float from, float to, bool pause_at_end) {
  fades_.push_back(Fade{t, from, to, 0, pause_at_end});
}

void AudioDirector::stop_fades(const Track* t) {
  std::erase_if(fades_, [t](const Fade& f) { return f.track == t; });
}

void AudioDirector::advance_fades(double dt) {
  for (Fade& f : fades_) {
    f.elapsed += dt;
    const double share = std::min(1.0, f.elapsed / kFadeSeconds);
    f.track->set_volume(std::clamp(f.from + static_cast<float>((f.to - f.from) * share), 0.0F, 1.0F));
    if (share >= 1.0 && f.pause_at_end) f.track->pause();
  }
  std::erase_if(fades_, [](const Fade& f) { return f.elapsed >= kFadeSeconds; });
}

// Faded-out tracks and finished stings are released; a finished music track gives way to the next one
// of its mood (no fade-out: it has ended).
void AudioDirector::release_done() {
  std::erase_if(leaving_, [this](const std::unique_ptr<Playing>& p) {
    const bool fading = std::any_of(fades_.begin(), fades_.end(), [&](const Fade& f) { return f.track == p->track.get(); });
    return !fading;
  });
  std::erase_if(stings_, [](const std::unique_ptr<Track>& t) { return t->finished(); });
  if (current_ && current_->track->finished() && mood_) {
    stop_fades(current_->track.get());
    current_ = start(*mood_);
  }
}

void AudioDirector::tick(double dt) {
  advance_fades(dt);
  release_done();
}

void Soundtrack::update(bool terminal, const GameState* g, const std::string& run_id, const AudioSettings& s,
                        double dt) {
  director_.apply(s);
  director_.set_mood(mood_for(terminal, g, director_.mood()));
  // Observing the same events again plays nothing: the tracker remembers the last one heard.
  if (g != nullptr) {
    if (const auto sting = stings_.observe(run_id, g->events)) director_.sting(*sting);
  }
  director_.tick(dt);
}

}  // namespace tradelab::desktop::audio
