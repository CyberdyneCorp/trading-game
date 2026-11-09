// Port of apps/web/src/lib/game/guessRound.ts (names kept; web parity fixture guess.json): "guess the
// next move" rounds (add-practice-and-advanced-strategies D19). A round draws a round market, a seed and
// a decision step, shows the chart up to that bar, takes the player's commitment (long, short or no
// trade; the probability that the close 12 bars later is higher; for a trade an invalidation and a
// target), then reveals the next 12 bars. The score judges the decision, never the P&L: calibration 50,
// plan 40, consistency 10, in hundredths with integer arithmetic. Prices and the ATR are the engine's
// decimal strings, compared in fixed point at 1e-8 (int64), never as doubles.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/snapshot.hpp"

namespace tradelab::desktop::practice {

inline constexpr int kGuessHorizon = 12;   // bars revealed after the decision
inline constexpr int kGuessLead = 20;      // the decision is at least this many bars after the warm-up
inline constexpr int kGuessPMin = 5;       // probabilities offered: 5% to 95% in steps of 5
inline constexpr int kGuessPMax = 95;
inline constexpr int kGuessPStep = 5;
inline constexpr int kGuessTableAfter = 20;  // the calibration table appears after this many rounds

struct RoundPick {
  std::string market;
  std::uint32_t seed = 0;
  int step = 0;
};

// mulberry32 seeded by FNV-1a 32 of "guess-v1:" + date + ":" + round index; the step in
// [warm-up + 20, total - 13]. `markets` must not be empty.
RoundPick pick_round(const std::vector<MissionInfo>& markets, const std::string& date, int index);

// A decimal string as an integer of 1e-8 units (extra digits truncated, either decimal mark); none when
// malformed.
std::optional<std::int64_t> fixed(std::string_view text);

struct Commitment {
  std::string decision = "long";  // long | short | none
  int p = 50;                     // percent, 5..95 in steps of 5
  std::string invalidation;       // trades only
  std::string target;
};

struct RoundFacts {
  std::string close;  // the close at the decision bar
  std::string atr;    // ATR(14) at the decision bar
  bool higher = false;  // was the close 12 bars later above the decision close
};

struct GuessScore {
  int calibration = 0;  // hundredths of a point (9600 = 96)
  int plan = 0;
  int consistency = 0;
  int total = 0;
  std::optional<bool> side_ok;  // plan parts for trades (none for no trade)
  std::optional<bool> distance_ok;
  std::optional<bool> reward_ok;
};

// 5000 - (p - o)^2, floored at 0 (50 - (p - o)^2 / 100 points).
int calibration_part(int p, bool higher);
// A long with p >= 55, a short with p <= 45, no trade with 40 <= p <= 60.
bool consistent(std::string_view decision, int p);
GuessScore score_round(const Commitment& c, const RoundFacts& f);

// Which level the next bars reached first (information only): invalidation, target, both (one bar
// reached both) or neither.
struct HighLow {
  std::string high;
  std::string low;
};
std::string first_hit(const Commitment& c, const std::vector<HighLow>& bars);

struct CalibrationRow {
  int from = 0;  // bucket start in percent: 0, 10, ... 90 (95 falls in 90)
  int rounds = 0;
  double stated = 0;    // mean stated probability (percent)
  double observed = 0;  // share of rounds whose close was higher (percent)
};
struct StatedOutcome {
  int p = 0;
  bool higher = false;
};
// Stated probability against observed frequency per 10% bucket (only buckets with rounds).
std::vector<CalibrationRow> calibration_table(const std::vector<StatedOutcome>& rounds);

// The probabilities offered, ascending.
std::vector<int> probabilities();

}  // namespace tradelab::desktop::practice
