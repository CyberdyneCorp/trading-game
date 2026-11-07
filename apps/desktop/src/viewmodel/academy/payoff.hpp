// Port of apps/web/src/lib/content/payoff.ts (names kept; web parity fixture payoff.json): the expiry
// payoff of a lesson position (one to four option legs and an optional perpetual leg) and its exact
// analytics on the piecewise-linear payoff: breakevens, maximum profit and maximum loss (none =
// unbounded). Prices cannot go below 0. Arithmetic at expiry (intrinsic value minus premium), not
// model prices; fees are left out, as the lessons say.
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "model/content.hpp"

namespace tradelab::desktop::academy {

// P&L of one leg at expiry for settlement price s, premium or entry included.
double leg_pnl_at(const PayoffLeg& leg, double s);
double pnl_at(const std::vector<PayoffLeg>& legs, double s);
// Prices where the expiry P&L crosses zero, ascending.
std::vector<double> breakevens(const std::vector<PayoffLeg>& legs);
// Largest expiry profit; none when it grows without bound as the price rises.
std::optional<double> max_profit(const std::vector<PayoffLeg>& legs);
// Largest expiry loss as a positive amount (0 when none); none when unbounded as the price rises.
std::optional<double> max_loss(const std::vector<PayoffLeg>& legs);

struct PayoffPoint {
  double s = 0;
  double pnl = 0;
};
// Points of the expiry curve between lo and hi (strikes included, so the corners are exact).
std::vector<PayoffPoint> payoff_curve(const std::vector<PayoffLeg>& legs, double lo, double hi, int points = 60);

// What the lesson's payoff diagram draws in a box of width x height (LessonPayoff.svelte's SVG): the
// curve, the zero line, the strikes (dashed, labeled) and the breakevens (dots, unless hidden).
struct PayoffGeometry {
  struct Point {
    float x = 0;
    float y = 0;
  };
  std::vector<Point> curve;
  float zero_y = 0;
  float left = 0;
  float right = 0;
  float top = 0;
  float bottom = 0;
  std::vector<std::pair<float, double>> strikes;  // x, strike
  std::vector<Point> breakevens;
};
PayoffGeometry payoff_geometry(const PayoffWidget& w, float width, float height);

}  // namespace tradelab::desktop::academy
