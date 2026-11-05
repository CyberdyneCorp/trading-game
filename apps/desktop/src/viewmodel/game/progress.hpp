// Advisor points and intel perks (web points.ts, perks.ts) and auto-advance (autoplay.svelte.ts):
// pure rules over the device's progress, unit-tested without a window.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/game_command.hpp"
#include "model/game_prefs.hpp"
#include "model/game_state.hpp"

namespace tradelab::desktop::game {

// ---- points.ts ----
inline constexpr int kStartPoints = 3;
inline constexpr int kMaxPoints = 10;
int allowance_for(const GamePrefs& p);
// balance - used + earned, capped, applied once per run id (the last 100 ids are kept).
bool settle_run(GamePrefs& p, const std::string& run_id, int used, int earned);

// ---- perks.ts ----
enum class Perk : unsigned char { Calendar, Wire1, Wire2 };
struct PerkInfo {
  Perk id;
  std::string_view name;  // "calendar", "wire1", "wire2"
  int missions;           // distinct finished missions needed
  int lead;               // fast wire lead in bars (0 = not a wire)
};
inline constexpr std::array<PerkInfo, 3> kPerks = {{{Perk::Calendar, "calendar", 1, 0},
                                                    {Perk::Wire1, "wire1", 2, 1},
                                                    {Perk::Wire2, "wire2", 4, 2}}};
int distinct_finished(const std::vector<std::string>& finished, const std::vector<std::string>& built_in);
std::vector<Perk> unlocked_perks(int finished_count);
// Start options for the enabled perks (the longest enabled wire wins).
RunOptions perk_options(const std::vector<Perk>& enabled, int advice_allowance);
std::vector<Perk> perks_used(const RunOptions& o);
std::vector<Perk> new_perks(const std::vector<Perk>& unlocked, const std::vector<std::string>& introduced);

// ---- autoplay.svelte.ts ----
enum class PauseReason : unsigned char { Fill, Liquidation, MarginCall, OptionSettlement, News, Finished };
std::string_view pause_reason_id(PauseReason r);

struct AutoSpeed {
  std::string_view label;
  double seconds;
};
inline constexpr std::array<AutoSpeed, 4> kAutoSpeeds = {{{"1\xC3\x97", 1.5}, {"2\xC3\x97", 0.8}, {"4\xC3\x97", 0.4}, {"10\xC3\x97", 0.15}}};

// Something on the bar that just closed the player should not miss (a fill, a liquidation, a margin
// call, an option settlement, a headline arriving), if any.
std::optional<PauseReason> notable(const GameState& g);

// A headline reaches the player at this step: its public release, or its early delivery (news.ts).
bool news_arrived(const GameState& g);

// Largest advance (<= n) that stops on the next calibration question step (calibration.ts).
int steps_until_next_question(const GameState& g, int n);

class AutoPlay {
 public:
  bool running = false;
  int speed = 1;  // index into kAutoSpeeds
  bool pause_on_events = true;
  std::optional<PauseReason> paused;

  void start(bool finished);
  void stop(std::optional<PauseReason> reason = std::nullopt);
  void toggle(bool finished);
  void set_speed(int index);
  // Time passes; returns true when a bar is due now (the caller advances when nothing blocks).
  bool tick(double dt);
  // After an advance completed: stops on the end of the run or on something notable.
  void after_advance(const GameState& g);

 private:
  double elapsed_ = 0;
};

}  // namespace tradelab::desktop::game
