// Port of apps/web/src/lib/game/audioRules.ts (names kept; parity fixture tests/fixtures/web/audioRules.json):
// what the soundtrack should do. Music has a mood per screen: the menu theme outside a mission, the
// in-game theme while trading and the tense theme while the account is in danger. A trade that closes
// plays a short win or loss sting; a liquidation always plays the loss sting.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"

namespace tradelab::desktop::audio {

enum class Mood : std::uint8_t { Menu, Game, Tense };
enum class Sting : std::uint8_t { Win, Loss };

inline constexpr double kLiqDanger = 0.05;   // within 5% of the liquidation price
inline constexpr double kMarginDanger = 2;   // option equity under 2x the maintenance margin
inline constexpr double kRelief = 1.6;       // hysteresis: 8% (5% x 1.6) to calm down again
inline constexpr int kAlertBars = 3;         // a margin call or liquidation keeps the tension for 3 bars

bool in_danger(const GameState& g, bool was_tense = false);
// `terminal`: the trading terminal is on screen (the web's screen 'terminal'); `g` null: no run.
Mood mood_for(bool terminal, const GameState* g, std::optional<Mood> previous = std::nullopt);
// The sting for events newer than `after_seq`, or none when nothing closed.
std::optional<Sting> sting_for(const std::vector<EngineEvent>& events, int after_seq);

// Remembers which events were already heard per run. The first state seen for a run (a new mission,
// or one resumed from storage) is a baseline: its history never plays.
class StingTracker {
 public:
  std::optional<Sting> observe(const std::string& run_id, const std::vector<EngineEvent>& events);

 private:
  std::string run_;
  int heard_ = 0;
};

}  // namespace tradelab::desktop::audio
