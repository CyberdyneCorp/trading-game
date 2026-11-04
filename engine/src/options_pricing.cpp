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

Price settlement_price(const Scenario& sc, std::uint32_t step, bool* approximated) {
  const std::uint32_t n = std::max<std::uint32_t>(1, std::min(sc.options.settlement_bars, step + 1));
  *approximated = n == 1;  // a single close stands in for the venue's 30-minute TWAP
  i128 sum = 0;
  for (std::uint32_t i = step + 1 - n; i <= step; ++i) sum += sc.bars[i].close.raw;
  const auto avg = static_cast<std::int64_t>(div_round(sum, n, Rounding::HalfEven));
  return round_to_step(Price::from_raw(avg), sc.contract.tick_size, Rounding::HalfEven).value();
}

}  // namespace tradelab
