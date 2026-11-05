#include "tradelab/generator.hpp"

#include <algorithm>
#include <cmath>  // exact operations only: floor, fabs

#include "tradelab/det_math.hpp"
#include "tradelab/prng.hpp"

namespace tradelab {
namespace {

Price to_price(double x, Price tick) {
  const double ticks = std::floor(x / (static_cast<double>(tick.raw) / Price::kScale) + 0.5);
  return Price::from_raw(std::max<std::int64_t>(1, static_cast<std::int64_t>(ticks)) * tick.raw);
}

Qty to_qty(double x) { return Qty::from_raw(static_cast<std::int64_t>(std::floor(x * Qty::kScale))); }

Rate to_rate(double x) {
  // Quantize to 1e-6 (0.0001%) like venue funding prints.
  const double micro = std::floor(x * 1e6 + 0.5);
  return Rate::from_raw(static_cast<std::int64_t>(micro) * (Rate::kScale / 1'000'000));
}

double price_of(Price p) { return static_cast<double>(p.raw) / Price::kScale; }

struct GenState {
  double close;
  double variance = 1.0;   // GARCH-like multiplier on segment vol
  double drift_ema = 0.0;
  double prev_ret = 0.0;   // last bar's log-return (momentum)
  double fair = 0.0;       // hidden log fair value
  double level = 0.0;      // Level anchor: log price at segment start
  double ema = 0.0;        // EMA of log close (Trend anchor)
  double vol_mult = 1.0;   // compression within the current segment
  double direction = 1.0;  // sign for random-direction segments
  double funding = 0.0;    // last published funding rate
  double funding_bias = 0.0;  // funding bias of the segment the last closed bar belonged to
  double event_drift = 0.0;  // transient news reaction for the current bar (not fundamental)
  double iv_var = 0.0;       // smoothed per-bar variance that implied vol prices (0 = not seeded)
  double news_gap = 0.0;     // log gap at this bar's open: the share of fresh news priced on arrival
  double event_jump = 0.0;   // event jump channel at this bar (log; liquidity, not information)
  double funding_shift = 0.0;  // event funding channel for the rate published at the last close
};

constexpr double kTrendEmaBars = 20.0;
constexpr double kJumpInformation = 0.75;  // share of an information jump absorbed by the fair value
// Implied vol prices the variance expected over an option's life, not the last bar's: the per-bar
// variance is smoothed (about a 12-bar half-life), so a shock lifts IV gradually instead of
// multiplying it twentyfold in one bar. The cap keeps a crash from pricing above 250%.
constexpr double kIvSmoothing = 0.055;
constexpr double kMaxIv = 2.5;

// Structural log-return terms, from state known at the previous close (spec D3a).
double structure(const Segment& seg, const GeneratorSpec& spec, const GenState& g) {
  const double x = det::log(g.close);
  double r = spec.fair_pull * (g.fair - x) + seg.momentum * g.prev_ret;
  if (seg.anchor == Anchor::Trend) r -= seg.reversion * (x - g.ema);
  if (seg.anchor != Anchor::Level) return r;
  const double gap = x - g.level;
  r -= seg.reversion * gap;
  const double beyond = std::fabs(gap) - seg.band;
  if (seg.band > 0.0 && beyond > 0.0) r -= spec.wall * (gap > 0.0 ? beyond : -beyond);
  return r;
}

// Crowded positioning: extreme funding makes a squeeze against the paying side possible.
double squeeze(const GeneratorSpec& spec, const GenState& g, Pcg32& rng) {
  const double u = rng.next_unit();
  const double size = rng.next_unit();
  if (std::fabs(g.funding) <= spec.squeeze_funding || u >= spec.squeeze_prob) return 0.0;
  return (g.funding > 0.0 ? -1.0 : 1.0) * spec.squeeze_size * (0.5 + size);
}

Bar make_bar(const Segment& seg, const GeneratorSpec& spec, GenState& g, Pcg32& rng, Price tick) {
  const double z = rng.next_normal();
  const double sigma = seg.vol * g.vol_mult * det::sqrt(g.variance);
  const bool jump = rng.next_unit() < seg.jump_prob;
  const double jump_size = jump ? g.direction * seg.jump_mean * (0.5 + rng.next_unit()) : 0.0;
  const double drift = g.direction * seg.drift;
  const double ret = drift + g.event_drift + structure(seg, spec, g) + sigma * z + jump_size + squeeze(spec, g, rng) + g.event_jump;
  g.variance = 0.85 * g.variance + 0.15 * z * z;
  g.drift_ema = 0.8 * g.drift_ema + 0.2 * ret;
  g.prev_ret = ret;
  // Segment drift is fundamental flow and most of an information jump is news: both move the fair
  // value. Squeeze jumps are positioning, not information, so they tend to retrace.
  g.fair += drift + kJumpInformation * jump_size;
  g.vol_mult *= seg.vol_decay;

  const double open = g.close * det::exp(0.1 * sigma * rng.next_normal() + (jump ? 0.5 * ret : 0.0) + g.news_gap);
  const double close = open * det::exp(jump ? 0.5 * ret : ret);
  const double wick_up = std::fabs(rng.next_normal()) * sigma * 0.6;
  const double wick_dn = std::fabs(rng.next_normal()) * sigma * 0.6;
  const double activity = 1.0 + 4.0 * (std::fabs(ret) + std::fabs(g.news_gap)) / std::max(seg.vol, 1e-6);
  const double volume = spec.base_volume * seg.volume_mult * activity * det::exp(0.3 * rng.next_normal());

  Bar b;
  b.open = to_price(open, tick);
  b.close = to_price(close, tick);
  b.high = std::max({to_price(std::max(open, close) * det::exp(wick_up), tick), b.open, b.close});
  b.low = std::min({to_price(std::min(open, close) * det::exp(-wick_dn), tick), b.open, b.close});
  b.volume = to_qty(volume);
  b.synthetic = true;
  g.close = price_of(b.close);
  g.ema += (det::log(g.close) - g.ema) * 2.0 / (kTrendEmaBars + 1.0);
  return b;
}

// Segment start: fix the range level, reset compression, draw the direction if it is random, and
// record the segment as played when the caller asked for the reveal.
void start_segment(const Segment& seg, const GeneratorSpec& spec, GenState& g, Pcg32& rng, std::uint32_t index,
                   std::vector<RegimeSpan>* spans) {
  const double x = det::log(g.close);
  g.level = x;
  g.vol_mult = 1.0;
  g.direction = 1.0;
  if (seg.random_direction) {
    const double p_up = 1.0 / (1.0 + det::exp(-spec.tilt * (g.fair - x)));
    g.direction = rng.next_unit() < p_up ? 1.0 : -1.0;
  }
  if (spans == nullptr) return;
  const std::int8_t drawn = seg.random_direction ? static_cast<std::int8_t>(g.direction) : std::int8_t{0};
  spans->push_back({index, seg.bars, seg.label, drawn});
}

int surprise_of(const NewsItem& item) { return item.actual - item.expectation; }

// News moves the hidden fair value from the bar after publication (surprise x credibility), and
// most of that shift is priced at once: the bar opens with a gap.
void apply_news(const GeneratorSpec& spec, std::uint32_t index, GenState& g) {
  double shift = 0.0;
  for (const ScriptedEvent& ev : spec.events) {
    if (ev.item.release_step + 1 == index) shift += ev.fair_impact * surprise_of(ev.item) * (ev.item.credibility / 100.0);
  }
  g.fair += shift;
  g.news_gap = spec.news_gap * shift;
}

// Crowding as known at the last close: the bias of the segment that bar belonged to, never the
// next bar's (that would announce a regime change one bar early).
double funding_raw(const GeneratorSpec& spec, const GenState& g) {
  const double raw = spec.funding_base + spec.funding_sensitivity * g.drift_ema * 0.01 + g.funding_bias + g.funding_shift;
  return std::clamp(raw, -0.003, 0.003);
}

int direction_of(const NewsItem& item) {
  const int surprise = surprise_of(item);
  if (surprise != 0) return surprise > 0 ? 1 : -1;
  return item.tone >= 0 ? 1 : -1;
}

bool reacting(const ScriptedEvent& ev, std::uint32_t index) {
  const std::uint32_t r = ev.item.release_step;
  return index > r && index <= r + ev.duration;  // reacts from the bar after publication
}

// Transient drift of every active news reaction at bar `index` (sampled direction).
double reaction_drift(const GeneratorSpec& spec, const std::vector<int>& reactions, std::uint32_t index) {
  double d = 0.0;
  for (std::size_t e = 0; e < spec.events.size(); ++e) {
    if (reacting(spec.events[e], index)) d += reactions[e] * spec.events[e].drift;
  }
  return d;
}

// Event jump channel at bar `index`: each active event with a jump probability draws on its own
// stream (seed, event, bar), so events without the channel draw nothing and change nothing.
double event_jumps(const GeneratorSpec& spec, const std::vector<int>& reactions, std::uint64_t seed, std::uint32_t index) {
  double j = 0.0;
  for (std::size_t e = 0; e < spec.events.size(); ++e) {
    const ScriptedEvent& ev = spec.events[e];
    if (ev.jump_prob <= 0.0 || !reacting(ev, index)) continue;
    Pcg32 rng((seed * 1000003ULL + e) * 1000003ULL + index, Stream::EventJump);
    const double u = rng.next_unit();
    const double size = rng.next_unit();
    if (u >= ev.jump_prob) continue;
    const int dir = reactions[e] != 0 ? reactions[e] : direction_of(ev.item);
    j += dir * ev.jump_size * (0.5 + size);
  }
  return j;
}

// Event funding channel for the rate of bar `index` (published at the close of index - 1).
double funding_shift(const GeneratorSpec& spec, std::uint32_t index) {
  double f = 0.0;
  for (const ScriptedEvent& ev : spec.events) {
    if (reacting(ev, index)) f += ev.funding_shift;
  }
  return f;
}

std::uint32_t pct_of(double mult) { return static_cast<std::uint32_t>(std::floor(std::max(0.0, mult) * 100.0 + 0.5)); }

// Event spread and capacity channels: one liquidity shock per declaring event over its active bars.
void add_liquidity(const GeneratorSpec& spec, Scenario& out) {
  out.liquidity.clear();
  const auto total = static_cast<std::uint32_t>(out.bars.size());
  for (const ScriptedEvent& ev : spec.events) {
    const std::uint32_t start = ev.item.release_step + 1;
    if ((ev.spread_mult == 1.0 && ev.capacity_mult == 1.0) || start >= total || ev.duration == 0) continue;
    out.liquidity.push_back({start, std::min(ev.duration, total - start), pct_of(ev.spread_mult), pct_of(ev.capacity_mult)});
  }
}

// Segment parameters for bar `index` with every active event's volatility effects applied.
Segment with_events(const Segment& seg, const GeneratorSpec& spec, std::uint32_t index) {
  Segment eff = seg;
  for (const ScriptedEvent& ev : spec.events) {
    if (!reacting(ev, index)) continue;
    eff.vol *= ev.vol_mult;
    eff.iv_mult *= ev.iv_mult;
    eff.volume_mult *= 1.0 + 0.5 * (ev.vol_mult - 1.0);
  }
  return eff;
}

// Implied vol known at the close of this bar: smoothed generator variance, annualized, plus the
// volatility premium and the segment/event IV multipliers, capped at kMaxIv.
Vol implied_vol(const Segment& seg, const GeneratorSpec& spec, GenState& g) {
  const double sigma = seg.vol * g.vol_mult * det::sqrt(g.variance);
  const double var = sigma * sigma;
  g.iv_var = g.iv_var == 0.0 ? var : g.iv_var + kIvSmoothing * (var - g.iv_var);
  const double iv = std::min(kMaxIv, det::sqrt(g.iv_var * spec.bars_per_year) * (1.0 + spec.iv_premium) * seg.iv_mult);
  return Vol::from_raw(static_cast<std::int64_t>(std::floor(iv * Vol::kScale + 0.5)));
}

// Index drawn with probability weight / sum(weights), integer draws only.
std::uint32_t draw_weighted(Pcg32& rng, const std::vector<std::uint32_t>& weights) {
  std::uint32_t total = 0;
  for (const std::uint32_t w : weights) total += w;
  if (total == 0) return 0;
  std::uint32_t r = rng.next_bounded(total);
  for (std::uint32_t i = 0; i < weights.size(); ++i) {
    if (r < weights[i]) return i;
    r -= weights[i];
  }
  return static_cast<std::uint32_t>(weights.size() - 1);
}

std::int32_t draw_lead(const GeneratorSpec& spec, std::uint64_t seed) {
  if (spec.lead_jitter_max <= spec.lead_jitter_min) return spec.lead_jitter_min;
  Pcg32 rng(seed, Stream::Schedule);
  const auto span = static_cast<std::uint32_t>(spec.lead_jitter_max - spec.lead_jitter_min + 1);
  return spec.lead_jitter_min + static_cast<std::int32_t>(rng.next_bounded(span));
}

}  // namespace

const char* regime_label_name(RegimeLabel label) {
  return json_name(label);
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> chain_path(const RegimeChain& chain, std::uint64_t seed) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> path;
  if (chain.states.empty() || chain.bars == 0) return path;
  Pcg32 rng(seed, Stream::Regime);
  const std::uint32_t min_dwell = std::max<std::uint32_t>(1, chain.min_dwell);
  const std::uint32_t span = std::max(chain.max_dwell, min_dwell) - min_dwell + 1;
  std::uint32_t state = draw_weighted(rng, chain.initial);
  std::uint32_t left = chain.bars;
  while (left > 0) {
    const std::uint32_t dwell = std::min(left, min_dwell + rng.next_bounded(span));
    path.emplace_back(state, dwell);
    left -= dwell;
    if (left > 0) state = draw_weighted(rng, chain.transitions[state]);
  }
  return path;
}

GeneratorSpec expanded(const GeneratorSpec& spec, std::uint64_t seed) {
  if (!spec.chain) return spec;
  GeneratorSpec out = spec;
  out.chain.reset();
  std::vector<Segment> visits;
  for (const auto& [state, bars] : chain_path(*spec.chain, seed)) {
    Segment s = spec.chain->states[state];
    s.bars = bars;
    visits.push_back(s);
  }
  const auto at = static_cast<std::ptrdiff_t>(std::min<std::size_t>(spec.chain->at, out.segments.size()));
  out.segments.insert(out.segments.begin() + at, visits.begin(), visits.end());
  return out;
}

const RegimeSpan* span_at(const std::vector<RegimeSpan>& spans, std::uint32_t step) {
  for (const RegimeSpan& s : spans) {
    if (step >= s.start && step < s.start + s.bars) return &s;
  }
  return nullptr;
}

GeneratorSpec scheduled(const GeneratorSpec& spec, std::uint64_t seed) {
  GeneratorSpec out = spec;
  if (out.segments.size() < 2) return out;
  const std::int32_t lead = draw_lead(spec, seed);
  const std::uint32_t boundary = out.segments.front().bars;
  out.segments.front().bars = static_cast<std::uint32_t>(static_cast<std::int32_t>(boundary) + lead);
  out.segments.back().bars = static_cast<std::uint32_t>(static_cast<std::int32_t>(out.segments.back().bars) - lead);
  for (ScriptedEvent& ev : out.events) {
    // An event moves with the schedule when its reaction (from the bar after release) is past the boundary.
    if (ev.item.release_step + 1 >= boundary) ev.item.release_step = static_cast<std::uint32_t>(static_cast<std::int32_t>(ev.item.release_step) + lead);
  }
  return out;
}

namespace {

bool declares_outcome(const ScriptedEvent& ev) {
  std::uint32_t total = 0;
  for (const std::uint32_t w : ev.actual_weights) total += w;
  return total > 0 || ev.intensity_min != 1.0 || ev.intensity_max != 1.0;
}

std::uint32_t percent(double x) { return static_cast<std::uint32_t>(std::floor(x * 100.0 + 0.5)); }

}  // namespace

EventOutcome draw_outcome(const ScriptedEvent& ev, std::uint64_t seed, std::uint32_t index) {
  EventOutcome out{ev.item.actual, 100};
  if (!declares_outcome(ev)) return out;
  Pcg32 rng(seed * 1000003ULL + index, Stream::Outcome);
  const std::vector<std::uint32_t> weights(ev.actual_weights.begin(), ev.actual_weights.end());
  const bool drawn = std::any_of(weights.begin(), weights.end(), [](std::uint32_t w) { return w > 0; });
  if (drawn) out.actual = static_cast<std::int8_t>(static_cast<int>(draw_weighted(rng, weights)) - 2);
  const std::uint32_t lo = percent(ev.intensity_min);
  const std::uint32_t hi = std::max(lo, percent(ev.intensity_max));
  out.intensity_pct = lo + rng.next_bounded(hi - lo + 1);
  return out;
}

namespace {

// The headline shows what was drawn: its actual, and a tone that reads it against the consensus
// (a print in line with expectations, or an incident that turned out harmless, reads neutral).
void apply_outcome(ScriptedEvent& ev, const EventOutcome& o) {
  ev.item.actual = o.actual;
  const int surprise = o.actual - ev.item.expectation;
  ev.item.tone = static_cast<std::int8_t>(surprise > 0 ? 1 : (surprise < 0 ? -1 : 0));
  const double k = o.intensity_pct / 100.0;
  ev.fair_impact *= k;
  ev.drift *= k;
}

}  // namespace

GeneratorSpec played(const GeneratorSpec& spec, std::uint64_t seed) {
  GeneratorSpec out = scheduled(expanded(spec, seed), seed);
  for (std::uint32_t e = 0; e < out.events.size(); ++e) {
    if (declares_outcome(out.events[e])) apply_outcome(out.events[e], draw_outcome(out.events[e], seed, e));
  }
  return out;
}

int sample_reaction(const ScriptedEvent& ev, std::uint64_t seed, std::uint32_t index) {
  Pcg32 rng(seed * 1000003ULL + index, Stream::Events);
  if (rng.next_unit() < ev.p_none) return 0;
  const int dir = direction_of(ev.item);
  return rng.next_unit() < ev.p_align ? dir : -dir;
}

void generate_market(const GeneratorSpec& declared, std::uint64_t seed, Scenario& out, std::vector<RegimeSpan>* spans) {
  // The chain is expanded first, so the schedule and everything after it see ordinary segments; the
  // drawn outcomes (G2) come last and only touch events that declare them.
  const GeneratorSpec spec = played(declared, seed);
  Pcg32 rng(seed, Stream::Market);
  GenState g{spec.start_price};
  g.fair = g.ema = det::log(spec.start_price);
  if (!spec.segments.empty()) g.funding_bias = spec.segments.front().funding_bias;
  out.bars.clear();
  out.funding_rate.clear();
  out.options.iv_base.clear();
  out.news.clear();
  std::vector<int> reactions;
  for (std::uint32_t e = 0; e < spec.events.size(); ++e) {
    reactions.push_back(sample_reaction(spec.events[e], seed, e));
    out.news.push_back(spec.events[e].item);
  }
  std::uint32_t index = 0;
  for (const Segment& base : spec.segments) {
    for (std::uint32_t i = 0; i < base.bars; ++i, ++index) {
      apply_news(spec, index, g);  // before the segment start: a breakout sees the news it follows
      if (i == 0) start_segment(base, spec, g, rng, index, spans);
      const Segment seg = with_events(base, spec, index);
      g.event_drift = reaction_drift(spec, reactions, index);
      g.event_jump = event_jumps(spec, reactions, seed, index);
      g.funding_shift = funding_shift(spec, index);
      // The rate for bar i is published at the close of bar i-1, so it may only use state up to i-1.
      g.funding = funding_raw(spec, g);
      out.funding_rate.push_back(to_rate(g.funding));
      out.bars.push_back(make_bar(seg, spec, g, rng, out.contract.tick_size));
      g.funding_bias = seg.funding_bias;
      out.options.iv_base.push_back(implied_vol(seg, spec, g));
    }
  }
  add_liquidity(spec, out);
}

}  // namespace tradelab
