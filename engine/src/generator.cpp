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
  double event_drift = 0.0;  // transient news reaction for the current bar (not fundamental)
};

constexpr double kTrendEmaBars = 20.0;
constexpr double kJumpInformation = 0.75;  // share of an information jump absorbed by the fair value

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
  const double ret = drift + g.event_drift + structure(seg, spec, g) + sigma * z + jump_size + squeeze(spec, g, rng);
  g.variance = 0.85 * g.variance + 0.15 * z * z;
  g.drift_ema = 0.8 * g.drift_ema + 0.2 * ret;
  g.prev_ret = ret;
  // Segment drift is fundamental flow and most of an information jump is news: both move the fair
  // value. Squeeze jumps are positioning, not information, so they tend to retrace.
  g.fair += drift + kJumpInformation * jump_size;
  g.vol_mult *= seg.vol_decay;

  const double open = g.close * det::exp(0.1 * sigma * rng.next_normal() + (jump ? 0.5 * ret : 0.0));
  const double close = open * det::exp(jump ? 0.5 * ret : ret);
  const double wick_up = std::fabs(rng.next_normal()) * sigma * 0.6;
  const double wick_dn = std::fabs(rng.next_normal()) * sigma * 0.6;
  const double activity = 1.0 + 4.0 * std::fabs(ret) / std::max(seg.vol, 1e-6);
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

// Segment start: fix the range level, reset compression, draw the direction if it is random.
void start_segment(const Segment& seg, const GeneratorSpec& spec, GenState& g, Pcg32& rng) {
  const double x = det::log(g.close);
  g.level = x;
  g.vol_mult = 1.0;
  g.direction = 1.0;
  if (!seg.random_direction) return;
  const double p_up = 1.0 / (1.0 + det::exp(-spec.tilt * (g.fair - x)));
  g.direction = rng.next_unit() < p_up ? 1.0 : -1.0;
}

int surprise_of(const NewsItem& item) { return item.actual - item.expectation; }

// News moves the hidden fair value from the bar after publication: surprise x credibility.
void apply_news(const GeneratorSpec& spec, std::uint32_t index, GenState& g) {
  for (const ScriptedEvent& ev : spec.events) {
    if (ev.item.release_step + 1 != index) continue;
    g.fair += ev.fair_impact * surprise_of(ev.item) * (ev.item.credibility / 100.0);
  }
}

double funding_raw(const GeneratorSpec& spec, const Segment& seg, const GenState& g) {
  const double raw = spec.funding_base + spec.funding_sensitivity * g.drift_ema * 0.01 + seg.funding_bias;
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

}  // namespace

int sample_reaction(const ScriptedEvent& ev, std::uint64_t seed, std::uint32_t index) {
  Pcg32 rng(seed * 1000003ULL + index, Stream::Events);
  if (rng.next_unit() < ev.p_none) return 0;
  const int dir = direction_of(ev.item);
  return rng.next_unit() < ev.p_align ? dir : -dir;
}

void generate_market(const GeneratorSpec& spec, std::uint64_t seed, Scenario& out) {
  Pcg32 rng(seed, Stream::Market);
  GenState g{spec.start_price};
  g.fair = g.ema = det::log(spec.start_price);
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
      if (i == 0) start_segment(base, spec, g, rng);
      const Segment seg = with_events(base, spec, index);
      g.event_drift = reaction_drift(spec, reactions, index);
      // The rate for bar i is published at the close of bar i-1, so it may only use state up to i-1.
      g.funding = funding_raw(spec, seg, g);
      out.funding_rate.push_back(to_rate(g.funding));
      out.bars.push_back(make_bar(seg, spec, g, rng, out.contract.tick_size));
      // Implied vol known at the close of this bar: generator vol state, annualized, plus premium.
      const double iv = seg.vol * g.vol_mult * det::sqrt(g.variance) * det::sqrt(spec.bars_per_year) * (1.0 + spec.iv_premium) *
                        seg.iv_mult;
      out.options.iv_base.push_back(Vol::from_raw(static_cast<std::int64_t>(std::floor(iv * Vol::kScale + 0.5))));
    }
  }
}

}  // namespace tradelab
