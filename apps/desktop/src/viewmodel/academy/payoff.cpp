#include "viewmodel/academy/payoff.hpp"

#include <algorithm>
#include <cmath>

namespace tradelab::desktop::academy {

namespace {

double sign(bool buy) { return buy ? 1.0 : -1.0; }

// Slope of the payoff above every strike (per unit of price).
double slope_above(const std::vector<PayoffLeg>& legs) {
  double sum = 0;
  for (const auto& l : legs) sum += l.kind == "put" ? 0.0 : sign(l.buy) * l.qty;
  return sum;
}

// The price 0 and every strike, ascending: the payoff is linear between them.
std::vector<double> kinks(const std::vector<PayoffLeg>& legs) {
  std::vector<double> xs{0.0};
  for (const auto& l : legs) {
    if (l.kind != "perp") xs.push_back(l.strike);
  }
  std::ranges::sort(xs);
  const auto dup = std::ranges::unique(xs);
  xs.erase(dup.begin(), dup.end());
  return xs;
}

// JavaScript's Math.round (halves toward +infinity) at 1e-8.
double round8(double v) { return std::floor(v * 1e8 + 0.5) / 1e8; }

// The root of the payoff inside the segment from kink a (to b, or beyond the last strike when none).
std::optional<double> root_in(const std::vector<PayoffLeg>& legs, double a, std::optional<double> b) {
  const double fa = pnl_at(legs, a);
  const double slope = b ? (pnl_at(legs, *b) - fa) / (*b - a) : slope_above(legs);
  if (slope == 0) return std::nullopt;
  const double root = a - fa / slope;
  if (root > a && (!b || root < *b)) return root;
  return std::nullopt;
}

void add_unique(std::vector<double>& out, double v) {
  if (std::ranges::find(out, v) == out.end()) out.push_back(v);
}

}  // namespace

double leg_pnl_at(const PayoffLeg& leg, double s) {
  if (leg.kind == "perp") return sign(leg.buy) * (s - leg.price) * leg.qty;
  const double intrinsic = leg.kind == "call" ? std::max(0.0, s - leg.strike) : std::max(0.0, leg.strike - s);
  return sign(leg.buy) * (intrinsic - leg.price) * leg.qty;
}

double pnl_at(const std::vector<PayoffLeg>& legs, double s) {
  double sum = 0;
  for (const auto& l : legs) sum += leg_pnl_at(l, s);
  return sum;
}

std::vector<double> breakevens(const std::vector<PayoffLeg>& legs) {
  const std::vector<double> xs = kinks(legs);
  std::vector<double> out;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    const double a = xs[i];
    if (a > 0 && pnl_at(legs, a) == 0) add_unique(out, round8(a));
    const auto b = i + 1 < xs.size() ? std::optional<double>(xs[i + 1]) : std::nullopt;
    if (const auto root = root_in(legs, a, b)) add_unique(out, round8(*root));
  }
  return out;
}

std::optional<double> max_profit(const std::vector<PayoffLeg>& legs) {
  if (slope_above(legs) > 0) return std::nullopt;
  double best = -INFINITY;
  for (double x : kinks(legs)) best = std::max(best, pnl_at(legs, x));
  return round8(best);
}

std::optional<double> max_loss(const std::vector<PayoffLeg>& legs) {
  if (slope_above(legs) < 0) return std::nullopt;
  double worst = INFINITY;
  for (double x : kinks(legs)) worst = std::min(worst, pnl_at(legs, x));
  return round8(std::max(0.0, -worst));
}

std::vector<PayoffPoint> payoff_curve(const std::vector<PayoffLeg>& legs, double lo, double hi, int points) {
  std::vector<double> xs;
  for (int i = 0; i <= points; ++i) add_unique(xs, lo + ((hi - lo) * i) / points);
  for (double k : kinks(legs)) {
    if (k > lo && k < hi) add_unique(xs, k);
  }
  std::ranges::sort(xs);
  std::vector<PayoffPoint> out;
  out.reserve(xs.size());
  for (double s : xs) out.push_back({s, pnl_at(legs, s)});
  return out;
}

PayoffGeometry payoff_geometry(const PayoffWidget& w, float width, float height) {
  constexpr float kPad = 24;
  PayoffGeometry g;
  const auto curve = payoff_curve(w.legs, w.lo, w.hi, 80);
  if (curve.empty() || w.hi <= w.lo) return g;
  double top = 0;
  double bottom = 0;
  for (const auto& p : curve) {
    top = std::max(top, p.pnl);
    bottom = std::min(bottom, p.pnl);
  }
  const double span = top - bottom > 0 ? top - bottom : 1;
  g.left = kPad;
  g.right = width - kPad;
  g.top = kPad / 2;
  g.bottom = height - kPad;
  const auto x = [&](double s) { return static_cast<float>(g.left + (s - w.lo) / (w.hi - w.lo) * (g.right - g.left)); };
  const auto y = [&](double v) { return static_cast<float>(g.top + (top - v) / span * (g.bottom - g.top)); };
  g.zero_y = y(0);
  for (const auto& p : curve) g.curve.push_back({x(p.s), y(p.pnl)});
  for (const auto& l : w.legs) {
    const bool seen = std::ranges::any_of(g.strikes, [&l](const auto& s) { return s.second == l.strike; });
    if (l.kind != "perp" && !seen) g.strikes.emplace_back(x(l.strike), l.strike);
  }
  if (!w.hide_analytics) {
    for (double b : breakevens(w.legs)) g.breakevens.push_back({x(b), g.zero_y});
  }
  return g;
}

}  // namespace tradelab::desktop::academy
