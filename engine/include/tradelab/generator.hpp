#pragma once

// Deterministic synthetic market generator (spec: market-generator; design D3, D3a).
// Same spec + seed => identical bars on every target.
//
// v2 ("market structure"): on top of regime segments with volatility clustering and jumps, each
// bar's log-return gets structural terms that give technical and fundamental reads a small,
// probabilistic edge without making them certain:
//   - a hidden fair value that news moves (surprise x credibility) and price is slowly pulled to;
//   - per-segment anchors: ranges revert to a fixed level with soft walls (support/resistance),
//     trends revert to their own moving average (pullbacks);
//   - momentum (trend persistence), volatility compression (squeeze) and segments whose direction
//     is drawn per seed, tilted toward the fair value (breakouts);
//   - crowding: when funding is extreme, a squeeze against the crowded side becomes possible;
//   - news priced on arrival (an opening gap) and a per-seed schedule for scripted segments.
// Volume is tied to |return|; funding follows recent drift.

#include <cstdint>
#include <string>
#include <vector>

#include "tradelab/scenario.hpp"

namespace tradelab {

constexpr std::uint32_t kGeneratorVersion = 2;

enum class Anchor : std::uint8_t {
  None,   // no structural pull besides the fair value
  Level,  // the log price at segment start: range with support/resistance
  Trend,  // an EMA of log price: trend with pullbacks to its average
};

// Fields are declared in the order missions use designated initializers.
struct Segment {
  std::uint32_t bars = 0;
  double drift = 0.0;        // mean log-return per bar
  double vol = 0.01;         // base log-return stdev per bar
  double jump_prob = 0.0;    // probability of a jump per bar
  double jump_mean = 0.0;    // signed mean jump size (log)
  double volume_mult = 1.0;
  double funding_bias = 0.0;  // added to the funding rate in this segment
  double iv_mult = 1.0;       // implied-vol multiplier in this segment (event premium / crush)
  Anchor anchor = Anchor::None;
  double reversion = 0.0;      // per-bar pull toward the anchor (fraction of the log gap)
  double band = 0.0;           // Level anchor: half-width (log) of the range; beyond it walls push back
  double momentum = 0.0;       // AR(1) weight on the previous bar's return
  double vol_decay = 1.0;      // per-bar factor on this segment's volatility (< 1 compresses)
  bool random_direction = false;  // drift/jump sign drawn per seed, tilted by the fair-value gap
};

// A scripted event: the headline (NewsItem) plus its seeded reaction model. The reaction direction
// is sampled: none with p_none, along the surprise with p_align, otherwise reversed (e.g. "buy the
// rumor, sell the news"). Headline tone never decides the direction by itself.
struct ScriptedEvent {
  NewsItem item;
  double p_none = 0.2;
  double p_align = 0.6;
  double drift = 0.004;        // per-bar log drift of the reaction (magnitude)
  std::uint32_t duration = 6;  // bars affected after release
  double vol_mult = 1.5;       // volatility multiplier while active
  double iv_mult = 1.0;        // implied-vol multiplier while active
  double fair_impact = 0.03;   // log fair-value shift per unit of surprise at full credibility
};

struct GeneratorSpec {
  double start_price = 100.0;
  double base_volume = 5000.0;
  double funding_base = 0.0001;   // per funding boundary
  double funding_sensitivity = 0.5;
  double iv_premium = 0.10;      // implied over realized
  double bars_per_year = 8760.0;  // hourly bars
  double fair_pull = 0.006;      // per-bar pull toward the hidden fair value (all segments)
  double tilt = 25.0;            // random-direction segments: P(up) = 1 / (1 + exp(-tilt * gap))
  double wall = 0.15;            // Level anchor: pull per unit of log distance beyond the band
  double squeeze_funding = 0.0006;  // |funding| above which the crowded side can be squeezed
  double squeeze_prob = 0.06;
  double squeeze_size = 0.02;    // log size of a squeeze jump against the crowd
  // Credible news is priced on arrival: this share of a release's fair-value shift opens the next
  // bar as a gap; the rest reaches price through fair_pull and the event's transient reaction.
  double news_gap = 0.8;
  // The first segment lasts [lead_jitter_min, lead_jitter_max] extra bars, drawn per seed; later
  // segments and every event after it shift with it and the last segment absorbs the difference,
  // so the mission length is fixed but a replay cannot learn on which bar the story turns.
  std::int32_t lead_jitter_min = 0;
  std::int32_t lead_jitter_max = 0;
  std::vector<ScriptedEvent> events;  // release steps are absolute bar indices
  std::vector<Segment> segments;
};

// Fills scenario.bars and scenario.funding_rate (contract.tick_size must be set).
void generate_market(const GeneratorSpec& spec, std::uint64_t seed, Scenario& out);

// The spec with this seed's schedule applied: first segment lengthened by the drawn lead, events
// after it shifted, last segment shortened by the same amount. Exposed for tests.
GeneratorSpec scheduled(const GeneratorSpec& spec, std::uint64_t seed);

// Sampled reaction sign for an event (-1, 0, +1); exposed for distribution tests.
int sample_reaction(const ScriptedEvent& ev, std::uint64_t seed, std::uint32_t index);

struct MissionInfo {
  std::string id;
  std::uint32_t total_bars = 0;
  std::uint32_t warmup_bars = 0;
  std::int32_t leverage_cap = 1;
  bool allow_short = true;
};

std::vector<MissionInfo> mission_catalog();
// Masked market name shown to the player: random per run (from the seed), stable within it.
std::string market_alias(std::uint64_t seed);
// Builds a validated built-in synthetic mission; returns false for an unknown id.
bool build_mission(const std::string& id, std::uint64_t seed, Scenario& out);

}  // namespace tradelab
