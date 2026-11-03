#include "tradelab/indicators.hpp"

#include <algorithm>

namespace tradelab {
namespace {

constexpr std::int64_t kHundred = 100 * Price::kScale;

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

// 100 * num / den in Price units (0 when den is 0).
Price percent(std::int64_t num, std::int64_t den) {
  if (den == 0) return Price{};
  return Price::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(num) * kHundred, den, Rounding::HalfEven)));
}

// EMA of an already-computed series: seeded with the SMA of its first `period` values.
Series ema_of(const Series& in, std::uint32_t period) {
  Series out(in.size(), std::nullopt);
  std::size_t start = 0;
  while (start < in.size() && !in[start]) ++start;
  const std::size_t seed_at = start + period - 1;
  if (period == 0 || seed_at >= in.size()) return out;
  i128 sum = 0;
  for (std::size_t i = start; i <= seed_at; ++i) {
    const std::optional<Price>& v = in[i];
    if (!v) return out;  // inputs are contiguous once warm; a gap means nothing to seed
    sum += v->raw;
  }
  std::int64_t e = avg(sum, period).raw;
  out[seed_at] = Price::from_raw(e);
  for (std::size_t i = seed_at + 1; i < in.size(); ++i) {
    const std::optional<Price>& v = in[i];
    if (!v) break;
    e += static_cast<std::int64_t>(div_round(static_cast<i128>(v->raw - e) * 2, period + 1, Rounding::HalfEven));
    out[i] = Price::from_raw(e);
  }
  return out;
}

std::optional<Price> minus(const std::optional<Price>& a, const std::optional<Price>& b) {
  if (!a.has_value() || !b.has_value()) return std::nullopt;
  return Price::from_raw(a->raw - b->raw);
}

// floor(sqrt(v)) for v >= 0, exact integer arithmetic.
std::int64_t isqrt(i128 v) {
  if (v <= 0) return 0;
  using u128 = unsigned __int128;
  const u128 n = static_cast<u128>(v);
  u128 x = n;
  u128 y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return static_cast<std::int64_t>(x);
}

struct DirectionalMove {
  std::int64_t plus = 0;
  std::int64_t minus = 0;
};

DirectionalMove directional_move(const Scenario& sc, std::uint32_t i) {
  const std::int64_t up = sc.bars[i].high.raw - sc.bars[i - 1].high.raw;
  const std::int64_t down = sc.bars[i - 1].low.raw - sc.bars[i].low.raw;
  return {up > down && up > 0 ? up : 0, down > up && down > 0 ? down : 0};
}

// DX from the smoothed directional moves (true range cancels out of +DI / -DI).
std::int64_t dx(const DirectionalMove& s) {
  const std::int64_t diff = s.plus > s.minus ? s.plus - s.minus : s.minus - s.plus;
  return percent(diff, s.plus + s.minus).raw;
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

Series vwap(const Scenario& sc, std::uint32_t last, std::uint32_t session) {
  Series out = empty(last);
  if (session == 0) return out;
  i128 num = 0;  // sum((H + L + C) * volume); the /3 is applied once at the end
  i128 den = 0;
  for (std::uint32_t i = 0; i <= last; ++i) {
    if (i % session == 0) num = den = 0;
    const Bar& b = sc.bars[i];
    const i128 typical3 = static_cast<i128>(b.high.raw) + b.low.raw + b.close.raw;
    num += typical3 * b.volume.raw;
    den += b.volume.raw;
    out[i] = den == 0 ? Price::from_raw(static_cast<std::int64_t>(div_round(typical3, 3, Rounding::HalfEven)))
                      : Price::from_raw(static_cast<std::int64_t>(div_round(num, den * 3, Rounding::HalfEven)));
  }
  return out;
}

Macd macd(const Scenario& sc, std::uint32_t last, std::uint32_t fast, std::uint32_t slow, std::uint32_t signal) {
  Macd m{empty(last), empty(last), empty(last)};
  const Series f = ema(sc, last, fast);
  const Series s = ema(sc, last, slow);
  for (std::uint32_t i = 0; i <= last; ++i) m.line[i] = minus(f[i], s[i]);
  m.signal = ema_of(m.line, signal);
  for (std::uint32_t i = 0; i <= last; ++i) m.histogram[i] = minus(m.line[i], m.signal[i]);
  return m;
}

Adx adx(const Scenario& sc, std::uint32_t last, std::uint32_t period) {
  Adx a{empty(last), empty(last), empty(last)};
  if (period == 0 || last < period) return a;
  DirectionalMove dm;
  std::int64_t tr = 0;
  i128 plus = 0;
  i128 minus = 0;
  i128 trs = 0;
  for (std::uint32_t i = 1; i <= period; ++i) {
    const DirectionalMove d = directional_move(sc, i);
    plus += d.plus;
    minus += d.minus;
    trs += true_range(sc, i);
  }
  dm = {avg(plus, period).raw, avg(minus, period).raw};
  tr = avg(trs, period).raw;
  i128 dx_sum = 0;
  std::int64_t smoothed = 0;
  for (std::uint32_t i = period; i <= last; ++i) {
    if (i > period) {
      const DirectionalMove d = directional_move(sc, i);
      dm = {wilder(dm.plus, d.plus, period), wilder(dm.minus, d.minus, period)};
      tr = wilder(tr, true_range(sc, i), period);
    }
    a.plus_di[i] = percent(dm.plus, tr);
    a.minus_di[i] = percent(dm.minus, tr);
    const std::uint32_t k = i - period;  // DX index
    if (k + 1 < period) {
      dx_sum += dx(dm);
      continue;
    }
    smoothed = k + 1 == period ? avg(dx_sum + dx(dm), period).raw : wilder(smoothed, dx(dm), period);
    a.adx[i] = Price::from_raw(smoothed);
  }
  return a;
}

Bollinger bollinger(const Scenario& sc, std::uint32_t last, std::uint32_t period, std::uint32_t k) {
  Bollinger b{sma(sc, last, period), empty(last), empty(last)};
  if (period == 0) return b;
  i128 sum = 0;
  i128 sum_sq = 0;
  for (std::uint32_t i = 0; i <= last; ++i) {
    const i128 c = sc.bars[i].close.raw;
    sum += c;
    sum_sq += c * c;
    if (i >= period) {
      const i128 old = sc.bars[i - period].close.raw;
      sum -= old;
      sum_sq -= old * old;
    }
    const std::optional<Price>& mid = b.middle[i];
    if (!mid) continue;
    // population variance = (n * sum(x^2) - sum(x)^2) / n^2
    const i128 n = period;
    const std::int64_t width = isqrt((n * sum_sq - sum * sum) / (n * n)) * static_cast<std::int64_t>(k);
    b.upper[i] = Price::from_raw(mid->raw + width);
    b.lower[i] = Price::from_raw(mid->raw - width);
  }
  return b;
}

}  // namespace tradelab
