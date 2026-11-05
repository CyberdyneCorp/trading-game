// Port of apps/web/src/lib/game/optionsMath.ts: payoff, breakeven, fee, write margin and book Greeks
// for the option ticket and the rail. Estimates only: the engine is authoritative.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "model/i18n.hpp"

namespace tradelab::desktop::game {

struct OptionLeg {
  bool call = true;
  double strike = 0;
  bool buy = true;
  double qty = 0;
  double premium = 0;
  double multiplier = 1;
};

OptionLeg leg_from_quote(const OptionQuote& q, bool buy, double qty, double multiplier);
double payoff_at(const OptionLeg& leg, double s);
double breakeven(const OptionLeg& leg);
std::optional<double> max_loss(const OptionLeg& leg);
double option_fee(const OptionLeg& leg, double underlying, double fee_rate);
double option_buy_cost(const OptionLeg& leg, double underlying, double fee_rate);
double write_margin(const OptionQuote& q, double underlying, double qty, double multiplier);

struct PayoffPoint {
  double s = 0;
  double pnl = 0;
};
std::vector<PayoffPoint> payoff_curve(const OptionLeg& leg, double lo, double hi, int points = 60);

struct BookGreeks {
  double delta = 0, gamma = 0, theta = 0, vega = 0;
};
BookGreeks position_greeks(const Options& opts);

// "C 100 · exp 96"; a strike off the whole numbers keeps its decimals in the locale ("P 97,50 · exp 85").
std::string series_label(const std::string& kind, double strike, int expiry_step, Locale locale);
std::string series_label(const OptionQuote& q, Locale locale);

}  // namespace tradelab::desktop::game
