// Historical masking (spec scenario-packs "Historical masking", design D4). Integer-only: each
// masked price is round_half_even(p * 100 / reference_open / tick) * tick, computed in 128 bits
// with a single rounding, so bar-to-bar returns and candle proportions are kept within half a tick.

#include "tradelab/pack.hpp"

namespace tradelab {
namespace {

constexpr i128 kHundred = 100;

bool scale_price(Price p, Price reference, Price tick, Price* out) {
  // p * (100 / ref) in Price units: p.raw * 100 * kScale / ref.raw, then to the tick grid.
  const i128 num = static_cast<i128>(p.raw) * kHundred * Price::kScale;
  const i128 den = static_cast<i128>(reference.raw) * tick.raw;
  const i128 ticks = div_round(num, den, Rounding::HalfEven);
  const i128 raw = ticks * tick.raw;
  if (raw <= 0 || !fits_i64(raw)) return false;
  *out = Price::from_raw(static_cast<std::int64_t>(raw));
  return true;
}

bool scale_volume(Qty v, Price reference, Qty* out) {
  // Inverse factor: v * ref / 100, so price x volume (notional) is unchanged.
  const i128 raw = div_round(static_cast<i128>(v.raw) * reference.raw, kHundred * Price::kScale, Rounding::HalfEven);
  if (!fits_i64(raw)) return false;
  *out = Qty::from_raw(static_cast<std::int64_t>(raw));
  return true;
}

}  // namespace

bool mask_bars(std::span<const Bar> in, std::uint32_t reference, Price tick, std::vector<Bar>* out) {
  if (reference >= in.size() || tick.raw <= 0 || in[reference].open.raw <= 0) return false;
  const Price ref = in[reference].open;
  out->clear();
  out->reserve(in.size());
  for (const Bar& b : in) {
    Bar m;
    m.synthetic = b.synthetic;
    m.continuity = b.continuity;
    const bool ok = scale_price(b.open, ref, tick, &m.open) && scale_price(b.high, ref, tick, &m.high) &&
                    scale_price(b.low, ref, tick, &m.low) && scale_price(b.close, ref, tick, &m.close) &&
                    scale_volume(b.volume, ref, &m.volume);
    if (!ok) return false;
    out->push_back(m);
  }
  return true;
}

}  // namespace tradelab
