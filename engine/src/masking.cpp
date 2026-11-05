// Historical masking (spec scenario-packs "Historical masking", design D4). Integer-only: each
// masked price is round_half_even(p * 100 / reference_open / tick) * tick, computed in 128 bits
// with a single rounding, so bar-to-bar returns and candle proportions are kept within half a tick.

#include <algorithm>

#include "tradelab/pack.hpp"

namespace tradelab {
namespace {

constexpr i128 kHundred = 100;

// A positive price factor num / den, applied in 128 bits with one rounding.
struct Factor {
  i128 num;
  i128 den;
};

bool scale_price(Price p, Factor f, Price tick, Price* out) {
  // p * num / den in Price units, then to the tick grid (half-even).
  const i128 ticks = div_round(static_cast<i128>(p.raw) * f.num, f.den * tick.raw, Rounding::HalfEven);
  const i128 raw = ticks * tick.raw;
  if (raw <= 0 || !fits_i64(raw)) return false;
  *out = Price::from_raw(static_cast<std::int64_t>(raw));
  return true;
}

bool scale_volume(Qty v, Factor f, Qty* out) {
  // Inverse factor, so price x volume (notional) is unchanged.
  const i128 raw = div_round(static_cast<i128>(v.raw) * f.den, f.num, Rounding::HalfEven);
  if (!fits_i64(raw)) return false;
  *out = Qty::from_raw(static_cast<std::int64_t>(raw));
  return true;
}

bool scale_bar(const Bar& b, Factor f, Price tick, Bar* m) {
  m->synthetic = b.synthetic;
  return scale_price(b.open, f, tick, &m->open) && scale_price(b.high, f, tick, &m->high) && scale_price(b.low, f, tick, &m->low) &&
         scale_price(b.close, f, tick, &m->close) && scale_volume(b.volume, f, &m->volume);
}

// News of the branch market released after the branch point, announced no earlier than the bar
// after it (the recorded history never announced them).
void branch_news(const Scenario& alt, std::uint32_t step, Scenario& out) {
  for (NewsItem n : alt.news) {
    if (n.release_step <= step) continue;
    n.announce_lead = std::min(n.announce_lead, n.release_step - step - 1);
    out.news.push_back(n);
  }
}

}  // namespace

bool mask_bars(std::span<const Bar> in, std::uint32_t reference, Price tick, std::vector<Bar>* out) {
  if (reference >= in.size() || tick.raw <= 0 || in[reference].open.raw <= 0) return false;
  // p * (100 / ref): num = 100 in Price units, den = the reference open.
  const Factor f{kHundred * Price::kScale, in[reference].open.raw};
  out->clear();
  out->reserve(in.size());
  for (const Bar& b : in) {
    Bar m;
    if (!scale_bar(b, f, tick, &m)) return false;
    out->push_back(m);
  }
  return true;
}

bool splice_branch(const Scenario& recorded, std::uint32_t branch_step, const Scenario& alt, Scenario& out) {
  const std::size_t n = recorded.bars.size();
  if (branch_step + 1 >= n || alt.bars.size() < n || alt.funding_rate.size() < n) return false;
  const Factor f{recorded.bars[branch_step].close.raw, alt.bars[branch_step].close.raw};
  out = recorded;
  out.news.clear();
  for (const NewsItem& item : recorded.news) {
    if (item.release_step <= branch_step) out.news.push_back(item);
  }
  branch_news(alt, branch_step, out);
  out.liquidity.clear();
  for (const LiquidityShock& l : recorded.liquidity) {
    if (l.start <= branch_step) out.liquidity.push_back({l.start, std::min<std::uint32_t>(l.bars, branch_step + 1 - l.start), l.spread_pct, l.capacity_pct});
  }
  for (const LiquidityShock& l : alt.liquidity) {
    if (l.start > branch_step && l.start < n) out.liquidity.push_back({l.start, std::min<std::uint32_t>(l.bars, static_cast<std::uint32_t>(n) - l.start), l.spread_pct, l.capacity_pct});
  }
  const bool iv = !recorded.options.series.empty() && recorded.options.iv_base.size() == n && alt.options.iv_base.size() >= n;
  for (std::size_t i = branch_step + 1; i < n; ++i) {
    if (!scale_bar(alt.bars[i], f, recorded.contract.tick_size, &out.bars[i])) return false;
    out.bars[i].synthetic = true;
    out.funding_rate[i] = alt.funding_rate[i];
    if (iv) out.options.iv_base[i] = alt.options.iv_base[i];
  }
  return validate(out) == ScenarioError::Ok;
}

}  // namespace tradelab
