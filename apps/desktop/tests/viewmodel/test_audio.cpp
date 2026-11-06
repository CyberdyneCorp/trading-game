// The soundtrack (viewmodel/audio): the web's AudioDirector behaviour (audio.test.ts) with a fake player,
// the mood and sting rules on terminal states (audioRules.test.ts; the full grids are in the web parity
// fixture), mute and volumes kept with the prefs, and the remapped mute key.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "model/game_prefs.hpp"
#include "support/fake_engine.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/audio/audio_director.hpp"
#include "viewmodel/game/panels_vm.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::audio;

namespace {

// What a test sees of a track; it outlives the track the director releases.
struct TrackState {
  std::string file;
  float vol = 1;
  bool playing = false;
  bool ended = false;
};

struct FakeTrack final : Track {
  explicit FakeTrack(std::shared_ptr<TrackState> s) : state(std::move(s)) {}
  void set_volume(float v) override { state->vol = v; }
  [[nodiscard]] float volume() const override { return state->vol; }
  void play() override { state->playing = true; }
  void pause() override { state->playing = false; }
  [[nodiscard]] bool finished() const override { return state->ended; }
  std::shared_ptr<TrackState> state;
};

struct FakeOut final : AudioOut {
  std::unique_ptr<Track> open(std::string_view file, bool stream) override {
    opened.emplace_back(file);
    streamed.push_back(stream);
    tracks.push_back(std::make_shared<TrackState>(TrackState{std::string(file)}));
    return std::make_unique<FakeTrack>(tracks.back());
  }
  // The last track opened for `file`.
  TrackState* last(std::string_view file) {
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {
      if ((*it)->file == file) return it->get();
    }
    return nullptr;
  }
  std::vector<std::string> opened;
  std::vector<bool> streamed;
  std::vector<std::shared_ptr<TrackState>> tracks;
};

GameState terminal_state(double liq) {
  GameState g = testing::base_state();
  g.status = "active";
  g.step = 50;
  g.account.mark = 100;
  g.position.qty = 1;
  g.account.liquidation = liq;
  return g;
}

EngineEvent event(int seq, std::string type, std::string pnl = "") {
  EngineEvent e;
  e.seq = seq;
  e.step = seq;
  e.type = std::move(type);
  e.pnl = std::move(pnl);
  return e;
}

}  // namespace

TEST_CASE("audio: a mood starts its playlist with a fade-in; tracks alternate when one ends") {
  FakeOut out;
  AudioDirector d(&out);
  d.set_mood(Mood::Menu);
  CHECK(d.current_file() == "menu-1.mp3");
  CHECK(out.streamed.back());  // music streams
  TrackState* first = out.last("menu-1.mp3");
  CHECK(first->playing);
  CHECK(first->vol == 0.0F);
  d.tick(kFadeSeconds / 2);
  CHECK(first->vol == doctest::Approx(0.35 / 2));
  d.tick(kFadeSeconds);
  CHECK(first->vol == doctest::Approx(0.35));
  first->ended = true;
  d.tick(0.01);
  CHECK(d.current_file() == "menu-2.mp3");
  out.last("menu-2.mp3")->ended = true;
  d.tick(0.01);
  CHECK(d.current_file() == "menu-1.mp3");
}

TEST_CASE("audio: changing mood crossfades; the same mood again changes nothing") {
  FakeOut out;
  AudioDirector d(&out);
  d.set_mood(Mood::Game);
  d.tick(2);
  TrackState* game = out.last("game-1.mp3");
  d.set_mood(Mood::Game);
  CHECK(out.opened.size() == 1);
  d.set_mood(Mood::Tense);
  CHECK(d.current_file() == "tense-1.mp3");
  d.tick(kFadeSeconds / 2);
  CHECK(game->vol == doctest::Approx(0.35 / 2));
  CHECK(out.last("tense-1.mp3")->vol == doctest::Approx(0.35 / 2));
  d.tick(kFadeSeconds);
  CHECK_FALSE(game->playing);  // paused at the end of its fade, then released
}

TEST_CASE("audio: mute stops the music and the stings; unmute restarts the mood; volumes apply") {
  FakeOut out;
  AudioDirector d(&out);
  d.set_mood(Mood::Menu);
  d.tick(2);
  d.set_muted(true);
  CHECK(d.current_file().empty());
  d.sting(Sting::Win);
  CHECK(out.last("trade-win.mp3") == nullptr);
  d.set_mood(Mood::Game);  // remembered while muted
  CHECK(d.current_file().empty());
  d.set_muted(false);
  CHECK(d.current_file() == "game-1.mp3");
  d.tick(2);
  d.set_music_volume(0.8F);
  CHECK(out.last("game-1.mp3")->vol == doctest::Approx(0.8));
  d.set_sfx_volume(0.5F);
  d.sting(Sting::Loss);
  CHECK(out.last("trade-loss.mp3")->vol == doctest::Approx(0.5));
  CHECK_FALSE(out.streamed.back());  // a sting is decoded up front
  d.set_sfx_volume(0);
  d.sting(Sting::Win);
  CHECK(out.last("trade-win.mp3") == nullptr);
}

TEST_CASE("audio: without a player the director keeps its state and plays nothing") {
  AudioDirector d(nullptr);
  d.set_mood(Mood::Game);
  d.sting(Sting::Win);
  d.tick(1);
  CHECK(d.mood() == Mood::Game);
  CHECK(d.current_file().empty());
}

TEST_CASE("audio: danger hysteresis 5% to tense, 8% to calm again") {
  CHECK_FALSE(in_danger(terminal_state(94.9)));  // 5.1% away
  CHECK(in_danger(terminal_state(95.1)));        // 4.9%
  CHECK(in_danger(terminal_state(92.5), true));  // 7.5%: still tense once tense
  CHECK_FALSE(in_danger(terminal_state(91.9), true));
  const GameState g = terminal_state(95.1);
  CHECK(mood_for(true, &g) == Mood::Tense);
  CHECK(mood_for(false, &g) == Mood::Menu);
  CHECK(mood_for(true, nullptr) == Mood::Menu);
}

TEST_CASE("audio: a resumed run's history plays no sting; later closes play once") {
  FakeOut out;
  Soundtrack s(&out);
  GameState g = terminal_state(50);
  g.events = {event(1, "fill", "40"), event(2, "liquidation")};
  s.update(true, &g, "run-1", {}, 0.01);  // first state of the run: baseline
  CHECK(out.last("trade-win.mp3") == nullptr);
  CHECK(out.last("trade-loss.mp3") == nullptr);
  g.events.push_back(event(3, "fill", "12.5"));
  s.update(true, &g, "run-1", {}, 0.01);
  CHECK(out.last("trade-win.mp3") != nullptr);
  const std::size_t opened = out.opened.size();
  s.update(true, &g, "run-1", {}, 0.01);  // the same events again: nothing
  CHECK(out.opened.size() == opened);
  CHECK(s.director().current_file() == "game-1.mp3");
}

TEST_CASE("audio: the soundtrack follows the prefs' mute and volumes") {
  FakeOut out;
  Soundtrack s(&out);
  s.update(false, nullptr, "", AudioSettings{true, 0.35F, 0.7F}, 0.01);
  CHECK(s.director().current_file().empty());
  s.update(false, nullptr, "", AudioSettings{false, 0.5F, 0.7F}, 0.01);
  CHECK(s.director().current_file() == "menu-1.mp3");
  CHECK(s.director().settings().music == doctest::Approx(0.5));
}

TEST_CASE("audio prefs: mute and volumes survive the prefs file; damaged values fall back") {
  GamePrefs p;
  p.audio = {true, 0.25, 0.9};
  p.layout = "H(700,T(0;a),T(0;b))";
  const GamePrefs back = prefs_from_json(prefs_to_json(p));
  CHECK(back.audio == p.audio);
  CHECK(back.layout == p.layout);
  const GamePrefs damaged = prefs_from_json(R"({"audio":{"muted":"yes","music":7,"sfx":"x"},"layout":3})");
  CHECK_FALSE(damaged.audio.muted);
  CHECK(damaged.audio.music == doctest::Approx(1.0));
  CHECK(damaged.audio.sfx == doctest::Approx(0.7));
  CHECK(damaged.layout.empty());
}

TEST_CASE("audio: the mute key (remapped) and Settings change the same prefs") {
  Catalog catalog = builtin_catalog();
  testing::FakeEngine engine;
  game::GameViewModel vm{catalog, engine, nullptr, {}};
  vm.on_snapshot(*testing::snapshot_of(testing::base_state(), CommandResult{CommandKind::Start, true, "none"}));
  vm.set_keymap({{"mute", "k"}});
  CHECK(vm.handle_key({"k", false, false, false}));
  CHECK(vm.muted());
  CHECK_FALSE(vm.handle_key({"m", false, false, false}));
  CHECK(vm.muted());
  const auto hints = game::key_hints(vm.texts(), vm.keymap(), vm.muted());
  CHECK(hints.sound_tip == "Ativar som (K)");
  vm.set_audio({false, 0.1, 0.2});
  CHECK_FALSE(vm.muted());
  CHECK(vm.prefs().audio.music == doctest::Approx(0.1));
}
