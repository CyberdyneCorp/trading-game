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
};

Bar make_bar(const Segment& seg, const GeneratorSpec& spec, GenState& g, Pcg32& rng, Price tick) {
  const double z = rng.next_normal();
  const double sigma = seg.vol * det::sqrt(g.variance);
  const bool jump = rng.next_unit() < seg.jump_prob;
  const double ret = seg.drift + sigma * z + (jump ? seg.jump_mean * (0.5 + rng.next_unit()) : 0.0);
  g.variance = 0.85 * g.variance + 0.15 * z * z;
  g.drift_ema = 0.8 * g.drift_ema + 0.2 * ret;

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
  return b;
}

Rate funding_for(const GeneratorSpec& spec, const Segment& seg, const GenState& g) {
  const double raw = spec.funding_base + spec.funding_sensitivity * g.drift_ema * 0.01 + seg.funding_bias;
  return to_rate(std::clamp(raw, -0.003, 0.003));
}

int direction_of(const NewsItem& item) {
  const int surprise = item.actual - item.expectation;
  if (surprise != 0) return surprise > 0 ? 1 : -1;
  return item.tone >= 0 ? 1 : -1;
}

// Segment parameters for bar `index` with every active event's reaction applied on top.
Segment with_events(const Segment& seg, const GeneratorSpec& spec, const std::vector<int>& reactions, std::uint32_t index) {
  Segment eff = seg;
  for (std::size_t e = 0; e < spec.events.size(); ++e) {
    const ScriptedEvent& ev = spec.events[e];
    const std::uint32_t r = ev.item.release_step;
    if (index <= r || index > r + ev.duration) continue;  // reacts from the bar after publication
    eff.drift += reactions[e] * ev.drift;
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
      const Segment seg = with_events(base, spec, reactions, index);
      // The rate for bar i is published at the close of bar i-1, so it may only use state up to i-1.
      out.funding_rate.push_back(funding_for(spec, seg, g));
      out.bars.push_back(make_bar(seg, spec, g, rng, out.contract.tick_size));
      // Implied vol known at the close of this bar: generator vol state, annualized, plus premium.
      const double iv = seg.vol * det::sqrt(g.variance) * det::sqrt(spec.bars_per_year) * (1.0 + spec.iv_premium) * seg.iv_mult;
      out.options.iv_base.push_back(Vol::from_raw(static_cast<std::int64_t>(std::floor(iv * Vol::kScale + 0.5))));
    }
  }
}

}  // namespace tradelab
