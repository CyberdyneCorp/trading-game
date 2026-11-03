#include "tradelab/indicators.hpp"

#include <algorithm>

namespace tradelab {
namespace {

Price avg(i128 sum, std::uint32_t n) { return Price::from_raw(static_cast<std::int64_t>(div_round(sum, n, Rounding::HalfEven))); }

std::int64_t true_range(const Scenario& sc, std::uint32_t i) {
  const Bar& b = sc.bars[i];
  const std::int64_t hl = b.high.raw - b.low.raw;
  if (i == 0) return hl;
  const std::int64_t pc = sc.bars[i - 1].close.raw;
  return std::max({hl, b.high.raw > pc ? b.high.raw - pc : pc - b.high.raw, b.low.raw > pc ? b.low.raw - pc : pc - b.low.raw});
}

// Wilder smoothing: s = (s * (n - 1) + x) / n.
std::int64_t wilder(std::int64_t prev, std::int64_t x, std::uint32_t n) {
  return static_cast<std::int64_t>(div_round(static_cast<i128>(prev) * (n - 1) + x, n, Rounding::HalfEven));
}

Series empty(std::uint32_t last) { return Series(static_cast<std::size_t>(last) + 1, std::nullopt); }

std::int64_t change(const Scenario& sc, std::uint32_t i) { return sc.bars[i].close.raw - sc.bars[i - 1].close.raw; }

Price rsi_value(std::int64_t gain, std::int64_t loss) {
  if (loss == 0) return Price::from_raw(100 * Price::kScale);
  // 100 - 100 / (1 + gain/loss) = 100 * gain / (gain + loss)
  return Price::from_raw(static_cast<std::int64_t>(
      div_round(static_cast<i128>(gain) * 100 * Price::kScale, static_cast<i128>(gain) + loss, Rounding::HalfEven)));
}

}  // namespace

Series sma(const Scenario& sc, std::uint32_t last, std::uint32_t period) {
  Series out = empty(last);
  if (period == 0) return out;
  i128 sum = 0;
  for (std::uint32_t i = 0; i <= last; ++i) {
    sum += sc.bars[i].close.raw;
    if (i >= period) sum -= sc.bars[i - period].close.raw;
    if (i + 1 >= period) out[i] = avg(sum, period);
  }
  return out;
}

Series ema(const Scenario& sc, std::uint32_t last, std::uint32_t period) {
  Series out = sma(sc, last, period);
  if (period == 0 || last + 1 < period) return out;
  const std::optional<Price> seed = out[period - 1];  // SMA at the first warm bar
  if (!seed) return out;
  std::int64_t e = seed->raw;
  for (std::uint32_t i = period; i <= last; ++i) {
    // e += (close - e) * 2 / (n + 1)
    e += static_cast<std::int64_t>(div_round(static_cast<i128>(sc.bars[i].close.raw - e) * 2, period + 1, Rounding::HalfEven));
    out[i] = Price::from_raw(e);
  }
  return out;
}

Series rsi(const Scenario& sc, std::uint32_t last, std::uint32_t period) {
  Series out = empty(last);
  if (period == 0 || last < period) return out;
  std::int64_t gain = 0;
  std::int64_t loss = 0;
  for (std::uint32_t i = 1; i <= period; ++i) {
    const std::int64_t c = change(sc, i);
    gain += std::max<std::int64_t>(0, c);
    loss += std::max<std::int64_t>(0, -c);
  }
  gain = static_cast<std::int64_t>(div_round(gain, period, Rounding::HalfEven));
  loss = static_cast<std::int64_t>(div_round(loss, period, Rounding::HalfEven));
  out[period] = rsi_value(gain, loss);
  for (std::uint32_t i = period + 1; i <= last; ++i) {
    const std::int64_t c = change(sc, i);
    gain = wilder(gain, std::max<std::int64_t>(0, c), period);
    loss = wilder(loss, std::max<std::int64_t>(0, -c), period);
    out[i] = rsi_value(gain, loss);
  }
  return out;
}

Series atr(const Scenario& sc, std::uint32_t last, std::uint32_t period) {
  Series out = empty(last);
  if (period == 0 || last < period) return out;
  i128 sum = 0;
  for (std::uint32_t i = 1; i <= period; ++i) sum += true_range(sc, i);
  std::int64_t a = avg(sum, period).raw;
  out[period] = Price::from_raw(a);
  for (std::uint32_t i = period + 1; i <= last; ++i) {
    a = wilder(a, true_range(sc, i), period);
    out[i] = Price::from_raw(a);
  }
  return out;
}

}  // namespace tradelab
