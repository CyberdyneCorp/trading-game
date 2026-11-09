// Report-only what-if valuation (design D4 of add-academy-and-strategies): the quote's own kernel
// (options_kernel.hpp) on shifted inputs, then exact expiry analytics on the piecewise-linear payoff.

#include <algorithm>

#include "accounting.hpp"
#include "options_kernel.hpp"
#include "tradelab/options.hpp"
#include "tradelab/whatif.hpp"

namespace tradelab {
namespace {

using detail::KernelInputs;
using detail::UnitValuation;

constexpr std::uint32_t kDefaultGridPoints = 61;
constexpr std::uint32_t kMaxGridPoints = 401;
constexpr std::size_t kMaxLegs = 8;
constexpr std::int64_t kMaxPrice = 10'000'000LL * Price::kScale;  // inputs are bounded so no amount overflows
constexpr std::int64_t kMaxQty = 1'000'000LL * Qty::kScale;
constexpr std::int64_t kMaxMultiplier = 10'000LL * Qty::kScale;
constexpr std::int64_t kMaxIvPoints = 500LL * Vol::kScale;
constexpr std::int64_t kMaxVol = 10LL * Vol::kScale;

double to_d(Vol v) { return static_cast<double>(v.raw) / Vol::kScale; }

// Everything needed to value one leg at a forward: the run's IV model (series != nullptr) or a
// given volatility, the time left after the shift, and the position's signed units and entry.
struct PreparedLeg {
  WhatIfLeg leg;
  const Scenario* sc = nullptr;
  const OptionSeries* series = nullptr;
  std::uint32_t base_step = 0;
  Price strike;
  std::uint64_t expiry_key = 0;  // equal keys = one expiry
  std::uint32_t expiry_step = 0;
  double t_years = 0.0;
  double step_years = 0.0;
  double iv = 0.0;  // run-less, annualized
  double iv_shift = 0.0;
  Qty units;
  int sign = 1;
  // Time to expiry from now, in bars (run) or minutes (run-less): the horizon and the heatmap value
  // the leg at other times (design D4 of add-practice-and-advanced-strategies).
  std::uint32_t expiry_offset = 0;
  bool by_minutes = false;
};

// Years left after `elapsed` bars (run) or minutes (run-less), exactly as the preparation computes them.
double years_left(const PreparedLeg& p, std::uint32_t elapsed) {
  const std::uint32_t left = p.expiry_offset - std::min(p.expiry_offset, elapsed);
  return p.by_minutes ? detail::minutes_to_years(left) : left * p.step_years;
}

PreparedLeg at_time(const PreparedLeg& p, std::uint32_t elapsed) {
  PreparedLeg q = p;
  if (!p.leg.perp) q.t_years = years_left(p, elapsed);
  return q;
}

struct LegValue {
  UnitValuation unit;
  bool floored = false;
};

LegValue value_at(const PreparedLeg& p, Price f) {
  LegValue v;
  if (p.leg.perp) {
    v.unit.value = f;
    v.unit.delta = Rate::from_raw(Rate::kScale);
    return v;
  }
  const double raw = (p.series != nullptr ? detail::raw_model_vol(*p.sc, *p.series, p.base_step, f, p.t_years) : p.iv) + p.iv_shift;
  const double sigma = detail::floor_vol(raw);
  v.floored = sigma > raw;
  v.unit = detail::price_unit(KernelInputs{detail::to_double(f), detail::to_double(p.strike), p.t_years, sigma, p.step_years}, p.leg.kind);
  return v;
}

Money signed_amount(const PreparedLeg& p, Price per_unit) {
  const Money m = detail::unwrap(mul<Money>(p.units, per_unit, Rounding::HalfEven));
  return Money::from_raw(p.sign * m.raw);
}

Money plus(Money a, Money b) { return Money::from_raw(a.raw + b.raw); }

Price diff(Price a, Price b) { return Price::from_raw(a.raw - b.raw); }

// Value per unit at expiry for settlement price s: a perpetual is worth s, or the stop beyond it.
Price payoff(const PreparedLeg& p, Price s) {
  if (p.leg.perp && p.leg.stop.raw > 0) return p.sign > 0 ? std::max(s, p.leg.stop) : std::min(s, p.leg.stop);
  if (p.leg.perp) return s;
  const std::int64_t v = p.leg.kind == OptionKind::Call ? s.raw - p.strike.raw : p.strike.raw - s.raw;
  return Price::from_raw(std::max<std::int64_t>(0, v));
}

Money expiry_pnl(const std::vector<PreparedLeg>& legs, Price s) {
  Money total{};
  for (const PreparedLeg& p : legs) total = plus(total, signed_amount(p, diff(payoff(p, s), p.leg.entry)));
  return total;
}

Money today_pnl(const std::vector<PreparedLeg>& legs, Price f) {
  Money total{};
  for (const PreparedLeg& p : legs) total = plus(total, signed_amount(p, diff(value_at(p, f).unit.value, p.leg.entry)));
  return total;
}

// P&L slope above the highest kink, in units per unit of price (calls and the perpetual; a short
// perpetual stopped above is flat there).
std::int64_t upper_slope(const std::vector<PreparedLeg>& legs) {
  std::int64_t slope = 0;
  for (const PreparedLeg& p : legs) {
    const bool stopped_above = p.leg.perp && p.sign < 0 && p.leg.stop.raw > 0;
    if ((p.leg.perp || p.leg.kind == OptionKind::Call) && !stopped_above) slope += p.sign * p.units.raw;
  }
  return slope;
}

// Where the expiry payoff bends: 0, every strike and every perpetual stop.
std::vector<Price> kink_points(const std::vector<PreparedLeg>& legs) {
  std::vector<Price> pts{Price{}};
  for (const PreparedLeg& p : legs) {
    if (!p.leg.perp) pts.push_back(p.strike);
    else if (p.leg.stop.raw > 0) pts.push_back(p.leg.stop);
  }
  std::sort(pts.begin(), pts.end());
  pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
  return pts;
}

int sign_of(Money m) { return m.raw > 0 ? 1 : (m.raw < 0 ? -1 : 0); }

// The root of the line through (a, pa) and (b, pb), whose P&L signs differ, rounded half-even.
Price root_between(Price a, Money pa, Price b, Money pb) {
  const i128 num = static_cast<i128>(b.raw - a.raw) * -pa.raw;
  return Price::from_raw(a.raw + static_cast<std::int64_t>(div_round(num, static_cast<i128>(pb.raw) - pa.raw, Rounding::HalfEven)));
}

std::vector<Price> breakevens(const std::vector<Price>& pts, const std::vector<Money>& pnl, std::int64_t slope) {
  std::vector<Price> out;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    if (pnl[i].raw == 0) out.push_back(pts[i]);
    if (i + 1 < pts.size() && sign_of(pnl[i]) * sign_of(pnl[i + 1]) < 0) out.push_back(root_between(pts[i], pnl[i], pts[i + 1], pnl[i + 1]));
  }
  const Money last = pnl.back();
  if (slope != 0 && sign_of(last) != 0 && (last.raw > 0) != (slope > 0)) {
    const i128 d = div_round(static_cast<i128>(-last.raw) * Price::kScale, slope, Rounding::HalfEven);
    out.push_back(Price::from_raw(pts.back().raw + static_cast<std::int64_t>(d)));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

ExpiryAnalytics analyze(const std::vector<PreparedLeg>& legs, const std::vector<Price>& grid) {
  ExpiryAnalytics a;
  const std::vector<Price> pts = kink_points(legs);
  std::vector<Money> pnl;
  pnl.reserve(pts.size());
  for (const Price s : pts) pnl.push_back(expiry_pnl(legs, s));
  const std::int64_t slope = upper_slope(legs);
  a.breakevens = breakevens(pts, pnl, slope);
  const auto [lo, hi] = std::minmax_element(pnl.begin(), pnl.end());
  if (slope <= 0) a.max_profit = *hi;
  if (slope >= 0) a.max_loss = Money::from_raw(-lo->raw);
  for (const Price s : grid) a.curve.push_back({s, expiry_pnl(legs, s)});
  return a;
}

std::vector<Price> grid_prices(const WhatIfGrid& g, Price forward) {
  Price lo = g.lo;
  Price hi = g.hi;
  std::uint32_t n = g.points;
  if (n == 0) {
    lo = Price::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(forward.raw) * 7, 10, Rounding::HalfEven)));
    hi = Price::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(forward.raw) * 13, 10, Rounding::HalfEven)));
    n = kDefaultGridPoints;
  }
  std::vector<Price> out;
  for (std::uint32_t i = 0; i < n; ++i) {
    const i128 step = div_round(static_cast<i128>(hi.raw - lo.raw) * i, n - 1, Rounding::HalfEven);
    out.push_back(Price::from_raw(lo.raw + static_cast<std::int64_t>(step)));
  }
  return out;
}

bool same_expiry(const std::vector<PreparedLeg>& legs) {
  const PreparedLeg* first = nullptr;
  for (const PreparedLeg& p : legs) {
    if (p.leg.perp) continue;
    if (first != nullptr && p.expiry_key != first->expiry_key) return false;
    if (first == nullptr) first = &p;
  }
  return true;
}

bool any_option_alive(const std::vector<PreparedLeg>& legs) {
  return std::any_of(legs.begin(), legs.end(), [](const PreparedLeg& p) { return !p.leg.perp && p.t_years > 0.0; });
}

void add_net(WhatIfNet& n, const PreparedLeg& p, const WhatIfLegResult& r) {
  n.value = plus(n.value, r.position_value);
  n.pnl = plus(n.pnl, r.pnl);
  const auto greek = [&p](Rate g) { return p.sign * detail::unwrap(mul<Qty>(p.units, g, Rounding::HalfEven)).raw; };
  n.delta = Qty::from_raw(n.delta.raw + greek(r.delta));
  n.gamma = Qty::from_raw(n.gamma.raw + greek(r.gamma));
  n.theta = plus(n.theta, signed_amount(p, r.theta));
  n.vega = plus(n.vega, signed_amount(p, r.vega));
}

WhatIfLegResult leg_result(const PreparedLeg& p, Price f) {
  const LegValue v = value_at(p, f);
  WhatIfLegResult r;
  r.leg = p.leg;
  r.strike = p.strike;
  r.expiry_step = p.expiry_step;
  r.value = v.unit.value;
  r.iv = v.unit.iv;
  r.iv_floored = v.floored;
  r.delta = v.unit.delta;
  r.gamma = v.unit.gamma;
  r.theta = v.unit.theta;
  r.vega = v.unit.vega;
  r.position_value = signed_amount(p, v.unit.value);
  r.pnl = signed_amount(p, diff(v.unit.value, p.leg.entry));
  return r;
}

// ---- horizon analytics (design D4 of add-practice-and-advanced-strategies) ----------------

// Per-unit value at the horizon for price s: a perpetual as at expiry (its stop beyond it), an option at
// its model value with the time it has left then (intrinsic for the legs expiring then, and at price 0).
Price horizon_value(const PreparedLeg& p, Price s) {
  if (p.leg.perp || s.raw == 0) return payoff(p, s);
  return value_at(p, s).unit.value;
}

Money horizon_pnl(const std::vector<PreparedLeg>& legs, Price s) {
  Money total{};
  for (const PreparedLeg& p : legs) total = plus(total, signed_amount(p, diff(horizon_value(p, s), p.leg.entry)));
  return total;
}

Price snap(Price p, Price tick) { return detail::unwrap(round_to_step(p, tick, Rounding::HalfEven)); }

// The prices the horizon P&L is evaluated at before the search: 0, the grid, the strikes and the
// perpetual stops, on the tick grid.
std::vector<Price> horizon_points(const std::vector<PreparedLeg>& legs, const std::vector<Price>& grid, Price tick) {
  std::vector<Price> pts = kink_points(legs);
  pts.insert(pts.end(), grid.begin(), grid.end());
  for (Price& x : pts) x = snap(x, tick);
  std::sort(pts.begin(), pts.end());
  pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
  return pts;
}

// Bisection over integer ticks between a and b, whose P&L signs differ: the tick on the profitable side
// nearest the crossing (or the tick where the P&L is exactly 0). Exact and identical on every target.
Price bisect_ticks(const std::vector<PreparedLeg>& legs, Price a, Money pa, Price b, Price tick) {
  std::int64_t lo = a.raw / tick.raw;
  std::int64_t hi = b.raw / tick.raw;
  const int lo_sign = sign_of(pa);
  while (hi - lo > 1) {
    const std::int64_t mid = lo + (hi - lo) / 2;
    const int s = sign_of(horizon_pnl(legs, Price::from_raw(mid * tick.raw)));
    if (s == 0) return Price::from_raw(mid * tick.raw);
    if (s == lo_sign) lo = mid;
    else hi = mid;
  }
  return Price::from_raw((lo_sign > 0 ? lo : hi) * tick.raw);
}

void horizon_extremes(HorizonAnalytics& h, const std::vector<Money>& pnl, std::int64_t slope) {
  const auto [lo, hi] = std::minmax_element(pnl.begin(), pnl.end());
  if (slope <= 0) h.max_profit = *hi;
  if (slope >= 0) h.max_loss = Money::from_raw(-lo->raw);
  h.unbounded_above = slope < 0;
}

std::uint32_t first_expiry(const std::vector<PreparedLeg>& legs) {
  std::uint32_t first = 0;
  bool any = false;
  for (const PreparedLeg& p : legs) {
    if (p.leg.perp || (any && p.expiry_offset >= first)) continue;
    first = p.expiry_offset;
    any = true;
  }
  return first;
}

std::vector<Price> breakevens_at(const std::vector<PreparedLeg>& legs, const std::vector<Price>& pts,
                                 const std::vector<Money>& pnl, Price tick) {
  std::vector<Price> out;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    if (pnl[i].raw == 0) out.push_back(pts[i]);
    if (i + 1 < pts.size() && sign_of(pnl[i]) * sign_of(pnl[i + 1]) < 0) out.push_back(bisect_ticks(legs, pts[i], pnl[i], pts[i + 1], tick));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

HorizonAnalytics analyze_horizon(const std::vector<PreparedLeg>& now, const std::vector<Price>& grid, Price tick) {
  HorizonAnalytics h;
  const std::uint32_t first = first_expiry(now);
  std::vector<PreparedLeg> legs;
  legs.reserve(now.size());
  for (const PreparedLeg& p : now) legs.push_back(at_time(p, first));
  h.expiry = first;
  const std::vector<Price> pts = horizon_points(legs, grid, tick);
  std::vector<Money> pnl;
  pnl.reserve(pts.size());
  for (const Price s : pts) pnl.push_back(horizon_pnl(legs, s));
  h.breakevens = breakevens_at(legs, pts, pnl, tick);
  for (const Price b : h.breakevens) pnl.push_back(horizon_pnl(legs, b));
  horizon_extremes(h, pnl, upper_slope(legs));
  for (const Price s : grid) h.curve.push_back({s, horizon_pnl(legs, s)});
  return h;
}

// ---- heatmap ---------------------------------------------------------------------------------

WhatIfHeatmap build_heatmap(const std::vector<PreparedLeg>& now, const WhatIfHeatmapSpec& spec, Price f) {
  WhatIfHeatmap m;
  m.prices = grid_prices(WhatIfGrid{spec.lo, spec.hi, spec.points}, f);
  m.times = spec.times;
  for (const std::uint32_t t : spec.times) {
    std::vector<PreparedLeg> legs;
    legs.reserve(now.size());
    for (const PreparedLeg& p : now) legs.push_back(at_time(p, t));
    std::vector<Money> row;
    row.reserve(m.prices.size());
    for (const Price s : m.prices) row.push_back(today_pnl(legs, s));
    m.pnl.push_back(std::move(row));
  }
  return m;
}

struct EvalOptions {
  const WhatIfGrid& grid;
  Price tick;
  const std::optional<WhatIfHeatmapSpec>& heatmap;
};

WhatIfResult evaluate(const std::vector<PreparedLeg>& legs, Price f, const EvalOptions& o) {
  WhatIfResult out;
  out.forward = f;
  for (const PreparedLeg& p : legs) {
    out.legs.push_back(leg_result(p, f));
    add_net(out.net, p, out.legs.back());
  }
  const std::vector<Price> grid = grid_prices(o.grid, f);
  const bool one_expiry = same_expiry(legs);
  out.at_expiry = one_expiry && !any_option_alive(legs);
  if (one_expiry && !legs.empty()) out.expiry = analyze(legs, grid);
  if (!one_expiry) out.horizon = analyze_horizon(legs, grid, o.tick);
  for (const Price s : grid) out.today.push_back({s, today_pnl(legs, s)});
  if (o.heatmap) out.heatmap = build_heatmap(legs, *o.heatmap, f);
  return out;
}

bool in_range(std::int64_t v, std::int64_t lo, std::int64_t hi) { return v >= lo && v <= hi; }

WhatIfResult failure(WhatIfError e) {
  WhatIfResult r;
  r.error = e;
  return r;
}

bool valid_grid(const WhatIfGrid& g) {
  if (g.points == 0) return true;
  return g.points >= 2 && g.points <= kMaxGridPoints && g.lo.raw >= 0 && g.hi > g.lo && g.hi.raw <= kMaxPrice;
}

bool valid_shift(const WhatIfShift& s) {
  return in_range(s.forward.raw, 0, kMaxPrice) && in_range(s.iv_points.raw, -kMaxIvPoints, kMaxIvPoints);
}

bool valid_leg_common(const WhatIfLeg& l) {
  return in_range(l.qty.raw, 1, kMaxQty) && in_range(l.entry.raw, 0, kMaxPrice) && in_range(l.stop.raw, 0, kMaxPrice) &&
         (l.perp || l.stop.raw == 0);
}

bool valid_runless_leg(const WhatIfLeg& l) {
  if (!valid_leg_common(l)) return false;
  return l.perp || (in_range(l.strike.raw, 1, kMaxPrice) && in_range(l.iv.raw, 1, kMaxVol));
}

constexpr std::uint32_t kMaxHeatmapPoints = 61;
constexpr std::size_t kMaxHeatmapTimes = 25;

bool valid_heatmap(const std::optional<WhatIfHeatmapSpec>& h) {
  if (!h) return true;
  const bool times_ok = !h->times.empty() && h->times.size() <= kMaxHeatmapTimes;
  return times_ok && h->points >= 2 && h->points <= kMaxHeatmapPoints && h->lo.raw > 0 && h->hi > h->lo && h->hi.raw <= kMaxPrice;
}

bool valid_runless(const WhatIfRequest& r) {
  if (r.legs.empty() || r.legs.size() > kMaxLegs || !valid_grid(r.grid) || !valid_shift(r.shift)) return false;
  if (!valid_heatmap(r.heatmap) || r.holdings || !in_range(r.tick.raw, 0, kMaxPrice)) return false;
  if (!in_range(r.forward.raw, 1, kMaxPrice) || !in_range(r.multiplier.raw, 1, kMaxMultiplier) || r.minutes_per_step == 0) return false;
  return std::all_of(r.legs.begin(), r.legs.end(), valid_runless_leg);
}

PreparedLeg prepare_common(const WhatIfLeg& l, const WhatIfShift& s, Qty multiplier) {
  PreparedLeg p;
  p.leg = l;
  p.sign = l.side == OrderSide::Buy ? 1 : -1;
  p.units = detail::unwrap(mul<Qty>(l.qty, multiplier, Rounding::TowardZero));
  p.iv_shift = to_d(s.iv_points) / 100.0;
  return p;
}

}  // namespace

WhatIfResult option_whatif(const WhatIfRequest& req) {
  if (!valid_runless(req)) return failure(WhatIfError::InvalidInput);
  std::vector<PreparedLeg> legs;
  for (const WhatIfLeg& l : req.legs) {
    PreparedLeg p = prepare_common(l, req.shift, req.multiplier);
    p.strike = l.perp ? Price{} : l.strike;
    p.expiry_key = l.minutes_to_expiry;
    const std::uint32_t left = l.minutes_to_expiry - std::min(l.minutes_to_expiry, req.shift.minutes_elapsed);
    p.t_years = detail::minutes_to_years(left);
    p.step_years = detail::minutes_to_years(req.minutes_per_step);
    p.iv = to_d(l.iv);
    p.expiry_offset = l.minutes_to_expiry;
    p.by_minutes = true;
    legs.push_back(p);
  }
  const Price tick = req.tick.raw > 0 ? req.tick : Price::from_raw(Price::kScale / 100);
  return evaluate(legs, req.shift.forward.raw > 0 ? req.shift.forward : req.forward, EvalOptions{req.grid, tick, req.heatmap});
}

namespace {

// The price a leg would cross now: the ask to buy, the bid to sell (the last close for the perpetual).
Price crossing_price(const Scenario& sc, const RunState& st, const WhatIfLeg& l, const OptionSeries* s) {
  const Price close = sc.bars[st.step].close;
  if (s == nullptr) return close;
  const OptionQuote q = quote(sc, *s, st.step, close);
  return l.side == OrderSide::Buy ? q.ask : q.bid;
}

bool prepare_run_leg(const Scenario& sc, const RunState& st, const WhatIfRequest& req, const WhatIfLeg& l, PreparedLeg& p) {
  const OptionSeries* s = l.perp ? nullptr : find_series(sc, l.series);
  if (!l.perp && (s == nullptr || st.step < s->list_step || st.step >= s->expiry_step)) return false;
  p = prepare_common(l, req.shift, l.perp ? sc.contract.multiplier : sc.options.multiplier);
  if (!l.has_entry) p.leg.entry = crossing_price(sc, st, l, s);
  if (l.perp) return true;
  p.leg.kind = s->kind;
  p.sc = &sc;
  p.series = s;
  p.base_step = st.step;  // the IV base published now, never a later one
  p.strike = s->strike;
  p.expiry_key = s->expiry_step;
  p.expiry_step = s->expiry_step;
  p.step_years = detail::step_years(sc);
  const std::uint32_t left = s->expiry_step - st.step;
  p.t_years = (left - std::min(left, req.shift.bars)) * p.step_years;  // quote(): (expiry - step) * step_years
  p.expiry_offset = left;
  return true;
}

// ---- holdings and the risk block (design D4, D9 of add-practice-and-advanced-strategies) -------

// The held perpetual's working protective stop: the active reduce-only stop on the closing side nearest
// the last close and beyond it (0 = none), as the builder passes it.
Price working_stop(const Scenario& sc, const RunState& st) {
  const bool is_long = st.position.is_long();
  const Price close = sc.bars[st.step].close;
  Price best{};
  for (const Order& o : st.orders) {
    const bool stop = o.type == OrderType::StopMarket || o.type == OrderType::TrailingStop;
    if (!o.active() || o.series != 0 || !o.reduce_only || !stop || (o.side == OrderSide::Sell) != is_long) continue;
    if (is_long ? o.price >= close : o.price <= close) continue;
    if (best.raw == 0 || (is_long ? o.price > best : o.price < best)) best = o.price;
  }
  return best;
}

std::vector<WhatIfLeg> holding_legs(const Scenario& sc, const RunState& st) {
  std::vector<WhatIfLeg> out;
  if (!st.position.flat()) {
    WhatIfLeg l;
    l.perp = true;
    l.side = st.position.is_long() ? OrderSide::Buy : OrderSide::Sell;
    l.qty = detail::abs_qty(st.position.qty);
    l.entry = st.position.entry;
    l.has_entry = true;
    l.stop = working_stop(sc, st);
    out.push_back(l);
  }
  for (const OptionPosition& p : st.option_positions) {
    WhatIfLeg l;
    l.series = p.series;
    l.side = p.qty.raw > 0 ? OrderSide::Buy : OrderSide::Sell;
    l.qty = detail::abs_qty(p.qty);
    l.entry = p.avg_premium;
    l.has_entry = true;
    out.push_back(l);
  }
  return out;
}

std::int64_t basis_points_of(Money v, Money equity) {
  if (equity.raw <= 0) return 0;
  return static_cast<std::int64_t>(div_round(static_cast<i128>(v.raw) * 10'000, equity.raw, Rounding::HalfEven));
}

// 1/2 x |gamma units| x (1% of S)^2 in fixed point: S^2 at 1e-8 (exact for a price on the tick grid),
// then |G| x S^2 / 2e12 to Money, half-even.
Money gamma_one_percent(Qty gamma, Price s) {
  const i128 s2 = div_round(static_cast<i128>(s.raw) * s.raw, Price::kScale, Rounding::HalfEven);
  const i128 g = gamma.raw < 0 ? -static_cast<i128>(gamma.raw) : static_cast<i128>(gamma.raw);
  return Money::from_raw(static_cast<std::int64_t>(div_round(g * s2, 2'000'000'000'000LL, Rounding::HalfEven)));
}

// The next funding payment exactly as apply_funding would book it at the latest mark close and the
// published next rate: the payer rounds up, the receiver down.
Money next_funding(const Scenario& sc, const RunState& st, Rate rate) {
  const Position& p = st.position;
  if (p.flat() || rate.raw == 0) return Money{};
  const Money n = detail::notional(sc, p.qty, mark_bar(sc, st.step).close, Rounding::HalfEven);
  const bool pays = (rate.raw > 0) == p.is_long();
  const Rate magnitude = Rate::from_raw(rate.raw < 0 ? -rate.raw : rate.raw);
  const Money m = detail::unwrap(mul<Money>(n, magnitude, pays ? Rounding::Ceil : Rounding::Floor));
  return Money::from_raw(pays ? -m.raw : m.raw);
}

void add_limit(RiskBlock& r, RiskLimitKind kind, std::uint32_t limit, std::int64_t value) {
  if (limit == 0) return;
  r.limits.push_back({kind, limit, value, value > static_cast<std::int64_t>(limit)});
}

std::int64_t magnitude_of(std::int64_t v) { return v < 0 ? -v : v; }

RiskBlock risk_of(const Scenario& sc, const RunState& st, const WhatIfNet& net) {
  RiskBlock r;
  const Price close = sc.bars[st.step].close;
  const AccountView v = account_view(sc, st);
  r.delta_units = net.delta;
  r.delta_notional = detail::unwrap(mul<Money>(net.delta, close, Rounding::HalfEven));
  r.gamma_1pct = gamma_one_percent(net.gamma, close);
  r.vega_point = net.vega;
  r.theta_bar = net.theta;
  r.funding_next = next_funding(sc, st, v.next_funding_rate);
  r.equity = v.equity;
  r.delta_bp = basis_points_of(r.delta_notional, v.equity);
  r.gamma_bp = basis_points_of(r.gamma_1pct, v.equity);
  r.vega_bp = basis_points_of(r.vega_point, v.equity);
  r.theta_bp = basis_points_of(r.theta_bar, v.equity);
  r.funding_bp = basis_points_of(r.funding_next, v.equity);
  const RiskLimits& lim = sc.mission.risk_limits;
  add_limit(r, RiskLimitKind::Delta, lim.delta_bp, magnitude_of(r.delta_bp));
  add_limit(r, RiskLimitKind::Gamma, lim.gamma_bp, r.gamma_bp);
  add_limit(r, RiskLimitKind::Vega, lim.vega_bp, magnitude_of(r.vega_bp));
  add_limit(r, RiskLimitKind::Theta, lim.theta_bp, -r.theta_bp);
  return r;
}

constexpr std::size_t kMaxRunLegs = 32;  // holdings plus the request's legs

}  // namespace

WhatIfResult run_option_whatif(const Scenario& sc, const RunState& st, const WhatIfRequest& req) {
  std::vector<WhatIfLeg> all = req.holdings ? holding_legs(sc, st) : std::vector<WhatIfLeg>{};
  all.insert(all.end(), req.legs.begin(), req.legs.end());
  const bool sized = req.legs.size() <= kMaxLegs && all.size() <= kMaxRunLegs && (req.holdings || !req.legs.empty());
  const bool ok = sized && valid_grid(req.grid) && valid_shift(req.shift) && valid_heatmap(req.heatmap) &&
                  std::all_of(req.legs.begin(), req.legs.end(), valid_leg_common);
  if (!ok) return failure(WhatIfError::InvalidInput);
  std::vector<PreparedLeg> legs(all.size());
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (!prepare_run_leg(sc, st, req, all[i], legs[i])) return failure(WhatIfError::SeriesNotListed);
  }
  const Price f = req.shift.forward.raw > 0 ? req.shift.forward : sc.bars[st.step].close;
  WhatIfResult out = evaluate(legs, f, EvalOptions{req.grid, sc.contract.tick_size, req.heatmap});
  if (req.holdings) out.risk = risk_of(sc, st, out.net);
  return out;
}

}  // namespace tradelab
