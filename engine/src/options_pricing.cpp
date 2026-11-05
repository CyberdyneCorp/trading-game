#include <algorithm>
#include <cmath>  // exact operations only: floor

#include "tradelab/det_math.hpp"
#include "tradelab/options.hpp"

namespace tradelab {
namespace {

constexpr double kMinutesPerYear = 365.0 * 24.0 * 60.0;
constexpr double kRefYears = 30.0 / 365.0;  // term-structure reference maturity
// Shortest maturity the term structure distinguishes: below it the factor stays at its 7-day value
// (about +16% with slope 0.15) instead of growing without bound (4.9x in an hourly option's last bar).
constexpr double kTermFloorYears = 7.0 / 365.0;
constexpr double kMinVol = 0.05;
constexpr std::uint32_t kRealizedWindow = 48;

double to_d(Price p) { return static_cast<double>(p.raw) / Price::kScale; }
double to_d(Vol v) { return static_cast<double>(v.raw) / Vol::kScale; }
double to_d(Rate r) { return static_cast<double>(r.raw) / Rate::kScale; }

// Quantize a non-negative double to a multiple of `tick` (Price units).
Price to_tick(double x, Price tick, Rounding mode) {
  const double ticks = x * Price::kScale / static_cast<double>(tick.raw);
  double n = std::floor(ticks + 0.5);
  if (mode == Rounding::Floor) n = std::floor(ticks);
  if (mode == Rounding::Ceil) n = -std::floor(-ticks);
  return Price::from_raw(static_cast<std::int64_t>(std::max(0.0, n)) * tick.raw);
}

template <typename F>
F quantize(double x) {
  return F::from_raw(static_cast<std::int64_t>(std::floor(x * F::kScale + 0.5)));
}

// Annualized realized volatility of log returns of closes up to `step` (inclusive): past data only.
double realized_vol(const Scenario& sc, std::uint32_t step, double step_years) {
  const std::uint32_t n = std::min<std::uint32_t>(step, kRealizedWindow);
  if (n < 2) return 0.5;
  double sum = 0.0;
  double sum_sq = 0.0;
  for (std::uint32_t i = step + 1 - n; i <= step; ++i) {
    const double r = det::log(to_d(sc.bars[i].close) / to_d(sc.bars[i - 1].close));
    sum += r;
    sum_sq += r * r;
  }
  const double mean = sum / n;
  const double var = std::max(0.0, sum_sq / n - mean * mean);
  return det::sqrt(var / step_years);
}

struct Inputs {
  double f;
  double k;
  double t;      // years to expiry
  double sigma;  // annualized implied volatility
  double step_years;
  bool realized;
};

Inputs make_inputs(const Scenario& sc, const OptionSeries& s, std::uint32_t step, Price forward) {
  const OptionSpec& o = sc.options;
  const double step_years = o.minutes_per_step / kMinutesPerYear;
  Inputs in{to_d(forward), to_d(s.strike), (s.expiry_step - step) * step_years, 0.0, step_years, o.iv_base.empty()};
  const double base = in.realized ? realized_vol(sc, step, step_years) : to_d(o.iv_base[step]);
  const double lm = det::log(in.k / in.f);
  const double term = 1.0 + to_d(o.term_slope) * (det::sqrt(kRefYears / std::max(in.t, kTermFloorYears)) - 1.0);
  in.sigma = std::max(kMinVol, base * term + to_d(o.skew) * lm + to_d(o.smile) * lm * lm);
  return in;
}

struct Model {
  double price;
  double delta;
  double gamma;
  double theta_year;
  double vega;  // per 1.00 of volatility
};

Model black76(const Inputs& in, OptionKind kind) {
  const double sq = det::sqrt(in.t);
  const double d1 = (det::log(in.f / in.k) + 0.5 * in.sigma * in.sigma * in.t) / (in.sigma * sq);
  const double d2 = d1 - in.sigma * sq;
  const double pdf = det::norm_pdf(d1);
  const bool call = kind == OptionKind::Call;
  Model m{};
  m.price = call ? in.f * det::norm_cdf(d1) - in.k * det::norm_cdf(d2) : in.k * det::norm_cdf(-d2) - in.f * det::norm_cdf(-d1);
  m.delta = call ? det::norm_cdf(d1) : det::norm_cdf(d1) - 1.0;
  m.gamma = pdf / (in.f * in.sigma * sq);
  m.theta_year = -(in.f * pdf * in.sigma) / (2.0 * sq);
  m.vega = in.f * pdf * sq;
  return m;
}

// Bid/ask around the model mid: half-spread = max(spread * mid, one tick); bid may be zero.
void fill_book(const Scenario& sc, double mid, OptionQuote& q) {
  const Price tick = sc.options.premium_tick;
  const double half = std::max(mid * to_d(sc.options.spread), to_d(tick));
  q.mid = to_tick(mid, tick, Rounding::HalfEven);
  q.bid = to_tick(mid - half, tick, Rounding::Floor);
  q.ask = std::max(to_tick(mid + half, tick, Rounding::Ceil), tick);
}

}  // namespace

const OptionSeries* find_series(const Scenario& sc, std::uint32_t id) {
  for (const OptionSeries& s : sc.options.series) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

Price intrinsic(const OptionSeries& s, Price spot) {
  const std::int64_t v = s.kind == OptionKind::Call ? spot.raw - s.strike.raw : s.strike.raw - spot.raw;
  return Price::from_raw(std::max<std::int64_t>(0, v));
}

OptionQuote quote(const Scenario& sc, const OptionSeries& series, std::uint32_t step, Price forward) {
  OptionQuote q;
  q.series_id = series.id;
  q.listed = step >= series.list_step && step < series.expiry_step && forward.raw > 0;
  if (!q.listed) return q;
  const Inputs in = make_inputs(sc, series, step, forward);
  const Model m = black76(in, series.kind);
  fill_book(sc, std::max(0.0, m.price), q);
  q.iv = quantize<Vol>(in.sigma);
  q.delta = quantize<Rate>(m.delta);
  q.gamma = quantize<Rate>(m.gamma);
  q.theta = quantize<Price>(m.theta_year * in.step_years);
  q.vega = quantize<Price>(m.vega / 100.0);
  q.iv_from_realized = in.realized;
  return q;
}

namespace {

// Half-even mean of the last n prices of `closes` (n >= 1), on the tick grid.
Price twap(const Scenario& sc, const std::vector<Price>& closes, std::size_t n) {
  i128 sum = 0;
  for (std::size_t i = closes.size() - n; i < closes.size(); ++i) sum += closes[i].raw;
  const auto avg = static_cast<std::int64_t>(div_round(sum, static_cast<std::int64_t>(n), Rounding::HalfEven));
  return round_to_step(Price::from_raw(avg), sc.contract.tick_size, Rounding::HalfEven).value();
}

const SubBars* sub_bars_for(const Scenario& sc, std::uint32_t step) {
  for (const SubBars& x : sc.sub_bars) {
    if (x.bar == step) return &x;
  }
  return nullptr;
}

}  // namespace

Price settlement_price(const Scenario& sc, std::uint32_t step, bool* approximated) {
  const std::uint32_t window = sc.options.settlement_minutes;
  const SubBars* sub = sub_bars_for(sc, step);
  if (sub != nullptr && window % sub->minutes == 0 && window / sub->minutes <= sub->closes.size()) {
    *approximated = false;
    return twap(sc, sub->closes, window / sub->minutes);
  }
  const std::uint32_t per_bar = sc.options.minutes_per_step;
  const std::uint32_t covering = std::min((window + per_bar - 1) / per_bar, step + 1);
  *approximated = window % per_bar != 0 || covering * per_bar < window;
  std::vector<Price> closes(covering);
  for (std::uint32_t i = 0; i < covering; ++i) closes[i] = sc.bars[step + 1 - covering + i].close;
  return twap(sc, closes, covering);
}

bool in_settlement_window(const Scenario& sc, const OptionSeries& series, std::uint32_t bar) {
  // Minutes from the open of bar 0: bar b opens at b * per_bar; the expiry closes at (expiry + 1) * per_bar.
  const std::uint64_t per_bar = sc.options.minutes_per_step;
  const std::uint64_t expiry_close = (series.expiry_step + 1ULL) * per_bar;
  const std::uint64_t window = sc.options.settlement_minutes;
  return static_cast<std::uint64_t>(bar) * per_bar + window >= expiry_close;
}

}  // namespace tradelab
