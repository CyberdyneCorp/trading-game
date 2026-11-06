// Port of apps/web/src/lib/game/sizing.ts and stops.ts (same names, same test vectors in
// tests/viewmodel/game/test_sizing.cpp): pre-trade estimates and stop/target helpers of the ticket.
// Estimates only: the engine is authoritative for acceptance, fills, margin and liquidation.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"

namespace tradelab::desktop::game {

enum class Side : unsigned char { Buy, Sell };

inline bool is_buy(Side s) { return s == Side::Buy; }

struct Tier {
  double floor = 0;
  double mmr = 0;
  double max_leverage = 0;  // +infinity for the implicit base tier
};

struct Costs {
  double multiplier = 1;
  double taker_fee = 0;
  double maintenance_margin = 0;
  double half_spread = 0;
  double slippage = 0;
  std::vector<Tier> tiers;
};

// The costs an order placed now pays: the contract's, or those of the liquidity shock in force.
Costs costs_of(const Contract& c, const std::optional<Liquidity>& liquidity);

struct ShockFactors {
  double cost = 1;
  double capacity = 1;
};
ShockFactors shock_factors(const Contract& c, const Liquidity& liquidity);

Tier margin_tier(const Costs& k, double notional);
double estimated_market_entry(Side side, double mark, const Costs& k);
std::optional<double> liquidation_price(Side side, double qty, double entry, double margin, const Costs& k);
std::optional<double> relevered_liquidation(Side side, double qty, double entry, int leverage, const Costs& k);

struct TicketInput {
  Side side = Side::Buy;
  double qty = 0;
  double entry = 0;
  int leverage = 1;
  std::optional<double> stop;
};

struct OpenPosition {
  double qty = 0;  // signed
  double entry = 0;
  double margin = 0;
  int leverage = 1;
};

struct TicketEstimate {
  double notional = 0;
  double margin = 0;
  double fee = 0;
  std::optional<double> liquidation;
  std::optional<double> planned_risk;
  bool stop_beyond_liquidation = false;
};

std::optional<double> liquidation_after(const TicketInput& in, const OpenPosition& pos, const Costs& k);
TicketEstimate estimate(const TicketInput& in, const Costs& k, const std::optional<OpenPosition>& pos = std::nullopt);

double qty_for_risk(double equity, double risk_pct, double entry, double stop, const Costs& k, double lot);
double required_margin(double inc_qty, double entry, int leverage, const Costs& k);

struct Fit {
  double qty = 0;
  int leverage = 1;
};
Fit fit_to_margin(double qty, double entry, double available, int from, int cap, const Costs& k, double lot);

enum class SizeBy : unsigned char { Risk, Qty, Notional, Equity };

double qty_for_notional(double notional, double entry, const Costs& k, double lot);
double qty_for_equity_pct(double equity, double pct, double entry, const Costs& k, double lot);

struct SizeInputs {
  double equity = 0;
  double entry = 0;
  std::optional<double> stop;
  double qty = 0;
  double amount = 0;
};
double sized_qty(SizeBy by, const SizeInputs& x, const Costs& k, double lot);

enum class Opposing : unsigned char { Reduce, Close, Reverse };
struct OpposingEffect {
  Opposing kind = Opposing::Reduce;
  double open = 0;
  double after = 0;
};
std::optional<OpposingEffect> opposing_effect(double position, Side side, double qty, double lot);

// ---- stops.ts ----

std::optional<double> stop_from_atr(Side side, double entry, std::optional<double> atr, double mult);
std::optional<double> stop_from_pct(Side side, double entry, double pct);
std::optional<std::string> trail_rate_from_percent(double pct);
std::optional<double> trail_trigger_from_percent(Side exit, double close, double pct, double tick);
std::optional<double> swing_stop(Side side, double entry, const std::vector<GameBar>& bars, double tick,
                                 int lookback = 10);
std::optional<double> target_from_r(Side side, double entry, std::optional<double> stop, double r);

struct Distance {
  double abs = 0;
  double pct = 0;
  std::optional<double> atr;
};
std::optional<Distance> distance_of(double entry, std::optional<double> level, std::optional<double> atr);
std::optional<double> reward_risk(Side side, double entry, std::optional<double> stop, std::optional<double> tp);
bool stop_on_losing_side(Side side, double entry, double stop);
bool target_on_winning_side(Side side, double entry, double tp);

}  // namespace tradelab::desktop::game
