#include <algorithm>
#include <cmath>  // exact operations only: floor

#include "tradelab/det_math.hpp"
#include "tradelab/options.hpp"
#include "options_kernel.hpp"

namespace tradelab {
namespace {

using detail::KernelInputs;
using detail::UnitValuation;

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

// Black-76 output in doubles (never stored: quantized by `price_unit`).
struct Model {
  double price;
  double delta;
  double gamma;
  double theta_year;
  double vega;  // per 1.00 of volatility
};

Model black76(const KernelInputs& in, OptionKind kind) {
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

// At (or past) expiry: intrinsic value; delta is 1 (call) or -1 (put) in the money, else 0.
UnitValuation expired(const KernelInputs& in, OptionKind kind) {
  const bool call = kind == OptionKind::Call;
  const double v = std::max(0.0, call ? in.f - in.k : in.k - in.f);
  UnitValuation u;
  u.model = v;
  u.value = quantize<Price>(v);
  u.iv = quantize<Vol>(in.sigma);
  const bool itm = call ? in.f > in.k : in.f < in.k;
  u.delta = itm ? Rate::from_raw(call ? Rate::kScale : -Rate::kScale) : Rate{};
  return u;
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

namespace detail {

double raw_model_vol(const Scenario& sc, const OptionSeries& s, std::uint32_t base_step, Price forward, double t_years) {
  const OptionSpec& o = sc.options;
  const double step_years = o.minutes_per_step / kMinutesPerYear;
  const double base = o.iv_base.empty() ? realized_vol(sc, base_step, step_years) : to_d(o.iv_base[base_step]);
  const double lm = det::log(to_d(s.strike) / to_d(forward));
  const double term = 1.0 + to_d(o.term_slope) * (det::sqrt(kRefYears / std::max(t_years, kTermFloorYears)) - 1.0);
  return base * term + to_d(o.skew) * lm + to_d(o.smile) * lm * lm;
}

double step_years(const Scenario& sc) { return sc.options.minutes_per_step / kMinutesPerYear; }

double minutes_to_years(double minutes) { return minutes / kMinutesPerYear; }

double floor_vol(double sigma) { return std::max(kMinVol, sigma); }

double to_double(Price p) { return to_d(p); }

UnitValuation price_unit(const KernelInputs& in, OptionKind kind) {
  if (in.t <= 0.0) return expired(in, kind);
  const Model m = black76(in, kind);
  UnitValuation u;
  u.model = std::max(0.0, m.price);
  u.value = quantize<Price>(u.model);
  u.iv = quantize<Vol>(in.sigma);
  u.delta = quantize<Rate>(m.delta);
  u.gamma = quantize<Rate>(m.gamma);
  u.theta = quantize<Price>(m.theta_year * in.step_years);
  u.vega = quantize<Price>(m.vega / 100.0);
  return u;
}

KernelInputs series_inputs(const Scenario& sc, const OptionSeries& s, std::uint32_t step, Price forward) {
  const double sy = step_years(sc);
  const double t = (s.expiry_step - step) * sy;
  return {to_d(forward), to_d(s.strike), t, floor_vol(raw_model_vol(sc, s, step, forward, t)), sy};
}

}  // namespace detail

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
  const detail::UnitValuation u = detail::price_unit(detail::series_inputs(sc, series, step, forward), series.kind);
  fill_book(sc, u.model, q);
  q.iv = u.iv;
  q.delta = u.delta;
  q.gamma = u.gamma;
  q.theta = u.theta;
  q.vega = u.vega;
  q.iv_from_realized = sc.options.iv_base.empty();
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
