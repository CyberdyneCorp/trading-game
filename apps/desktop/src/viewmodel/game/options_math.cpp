#include "viewmodel/game/options_math.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

OptionLeg leg_from_quote(const OptionQuote& q, bool buy, double qty, double multiplier) {
  return {q.kind == "call", q.strike, buy, qty, buy ? q.ask : q.bid, multiplier};
}

double payoff_at(const OptionLeg& leg, double s) {
  const double intrinsic = leg.call ? std::max(0.0, s - leg.strike) : std::max(0.0, leg.strike - s);
  const double per_unit = intrinsic - leg.premium;
  return (leg.buy ? per_unit : -per_unit) * leg.qty * leg.multiplier;
}

double breakeven(const OptionLeg& leg) { return leg.call ? leg.strike + leg.premium : leg.strike - leg.premium; }

std::optional<double> max_loss(const OptionLeg& leg) {
  if (!leg.buy) return std::nullopt;
  return leg.premium * leg.qty * leg.multiplier;
}

double option_fee(const OptionLeg& leg, double underlying, double fee_rate) {
  const double units = leg.qty * leg.multiplier;
  return std::min(units * underlying * fee_rate, units * leg.premium * 0.125);
}

double option_buy_cost(const OptionLeg& leg, double underlying, double fee_rate) {
  return leg.premium * leg.qty * leg.multiplier + option_fee(leg, underlying, fee_rate);
}

// Engine formula with the default parameters: max(0.15 F - OTM, 0.10 F) + ask, per unit.
double write_margin(const OptionQuote& q, double f, double qty, double multiplier) {
  const double otm = std::max(0.0, q.kind == "call" ? q.strike - f : f - q.strike);
  const double per_unit = std::max(0.15 * f - otm, 0.10 * f) + q.ask;
  return per_unit * qty * multiplier;
}

std::vector<PayoffPoint> payoff_curve(const OptionLeg& leg, double lo, double hi, int points) {
  std::vector<PayoffPoint> out;
  out.reserve(static_cast<std::size_t>(points) + 1);
  for (int i = 0; i <= points; ++i) {
    const double s = lo + (hi - lo) * i / points;
    out.push_back({s, payoff_at(leg, s)});
  }
  return out;
}

BookGreeks position_greeks(const Options& opts) {
  BookGreeks g;
  for (const auto& p : opts.positions) {
    const auto q = std::find_if(opts.chain.begin(), opts.chain.end(), [&](const OptionQuote& c) { return c.id == p.series; });
    if (q == opts.chain.end()) continue;
    const double n = p.qty * opts.multiplier;
    g.delta += n * q->delta;
    g.gamma += n * q->gamma;
    g.theta += n * q->theta;
    g.vega += n * q->vega;
  }
  return g;
}

std::string series_label(const std::string& kind, double strike, int expiry_step, Locale locale) {
  const bool whole = std::floor(strike) == strike;
  const std::string shown = whole ? fmt::fmt_num(strike, locale, 0) : fmt::fmt_num(strike, locale, 2);
  // JS String(100) has no grouping: strip the locale's thousands mark from a whole strike.
  std::string plain;
  for (const char c : shown) {
    if (!whole || (c != '.' && c != ',')) plain += c;
  }
  return std::string(kind == "call" ? "C " : "P ") + plain + " \xC2\xB7 exp " + std::to_string(expiry_step);  // ·
}

std::string series_label(const OptionQuote& q, Locale locale) {
  return series_label(q.kind, q.strike, q.expiry_step, locale);
}

}  // namespace tradelab::desktop::game
