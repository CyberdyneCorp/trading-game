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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tradelab/scenario.hpp"

namespace tradelab {

// v3: optional Markov regime chain (G1) and segment labels; not a digest input. Missions without a
// chain generate exactly as in v2.
constexpr std::uint32_t kGeneratorVersion = 3;

enum class Anchor : std::uint8_t {
  None,   // no structural pull besides the fair value
  Level,  // the log price at segment start: range with support/resistance
  Trend,  // an EMA of log price: trend with pullbacks to its average
};

// What a segment is, for the debrief reveal and for tests. It never feeds generation.
enum class RegimeLabel : std::uint8_t { Unlabeled = 0, Range, Trend, HighVol, LowLiquidity, Crowded, Calm, Shock };

const char* regime_label_name(RegimeLabel label);

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
  RegimeLabel label = RegimeLabel::Unlabeled;
};

// G1 (design D8.1): a Markov chain of regime states, expanded per seed into ordinary segments.
// The path is drawn on Stream::Regime with integer draws: the first state from `initial`, each
// visit lasts [min_dwell, max_dwell] bars, the next state comes from the visited state's row. The
// last visit is cut so the chain fills exactly `bars`. The expanded segments are inserted at index
// `at` of GeneratorSpec::segments before the per-seed schedule applies.
struct RegimeChain {
  std::vector<Segment> states;                          // templates; their `bars` field is ignored
  std::vector<std::vector<std::uint32_t>> transitions;  // per-mille rows summing to 1000, zero diagonal
  std::vector<std::uint32_t> initial;                   // per-mille weights over the states
  std::uint32_t min_dwell = 1;
  std::uint32_t max_dwell = 1;
  std::uint32_t bars = 0;
  std::uint32_t at = 0;
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
  std::optional<RegimeChain> chain;  // G1; absent = the segments as declared
};

// One generated segment as it played for a seed: where it starts, how long it lasts, its label and,
// for a random-direction segment, the direction drawn (+1 up, -1 down; 0 = not random).
struct RegimeSpan {
  std::uint32_t start = 0;
  std::uint32_t bars = 0;
  RegimeLabel label = RegimeLabel::Unlabeled;
  std::int8_t direction = 0;
};

// Fills scenario.bars and scenario.funding_rate (contract.tick_size must be set). When `spans` is
// given, it receives every segment as played (the reveal; never part of the scenario).
void generate_market(const GeneratorSpec& spec, std::uint64_t seed, Scenario& out, std::vector<RegimeSpan>* spans = nullptr);

// The spec with its regime chain (if any) expanded into segments for this seed; identity otherwise.
GeneratorSpec expanded(const GeneratorSpec& spec, std::uint64_t seed);
// The chain's path for a seed as (state index, bars) visits; exposed for distribution tests.
std::vector<std::pair<std::uint32_t, std::uint32_t>> chain_path(const RegimeChain& chain, std::uint64_t seed);

// The spec with this seed's schedule applied: first segment lengthened by the drawn lead, events
// after it shifted, last segment shortened by the same amount. Exposed for tests.
GeneratorSpec scheduled(const GeneratorSpec& spec, std::uint64_t seed);

// Sampled reaction sign for an event (-1, 0, +1); exposed for distribution tests.
int sample_reaction(const ScriptedEvent& ev, std::uint64_t seed, std::uint32_t index);

// G4 (design D8.1): what a mission's market was, for the debrief only. Clients ask for it after the
// run has ended (leak prevention, design D6).
struct EventReveal {
  std::uint32_t release_step = 0;
  NewsKind kind = NewsKind::RateDecision;
  std::int8_t actual = 0;
  std::int8_t surprise = 0;
  std::uint32_t intensity_pct = 100;  // factor on fair impact and reaction drift (100 = as declared)
  std::int8_t reaction = 0;           // sampled transient reaction: 0 none, +1/-1 up/down
};

struct MissionReveal {
  std::vector<RegimeSpan> spans;
  std::vector<EventReveal> events;
};

// The regime span containing `step` (nullptr past the end).
const RegimeSpan* span_at(const std::vector<RegimeSpan>& spans, std::uint32_t step);

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
// Regenerates the mission for this seed and returns its reveal; false for an unknown id.
bool mission_reveal(const std::string& id, std::uint64_t seed, MissionReveal& out);

}  // namespace tradelab
