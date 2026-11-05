#include "viewmodel/game/sizing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace tradelab::desktop::game {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double floor_lot(double units, double lot) {
  return units > 0 && std::isfinite(units) ? std::floor(units / lot + 1e-9) * lot : 0;
}

}  // namespace

Costs costs_of(const Contract& c, const std::optional<Liquidity>& liquidity) {
  Costs k;
  k.multiplier = c.multiplier;
  k.taker_fee = c.taker_fee;
  k.maintenance_margin = c.maintenance_margin;
  k.half_spread = liquidity ? liquidity->half_spread : c.half_spread;
  k.slippage = liquidity ? liquidity->slippage : c.slippage;
  for (const auto& t : c.tiers) k.tiers.push_back({t.floor, t.mmr, static_cast<double>(t.max_leverage)});
  return k;
}

ShockFactors shock_factors(const Contract& c, const Liquidity& liquidity) {
  const double base = c.half_spread + c.slippage;
  const double cap = c.capacity_fraction;
  return {base > 0 ? (liquidity.half_spread + liquidity.slippage) / base : 1,
          cap > 0 ? liquidity.capacity_fraction / cap : 1};
}

Tier margin_tier(const Costs& k, double notional) {
  Tier tier{0, k.maintenance_margin, kInf};
  for (const auto& t : k.tiers) {
    if (notional < t.floor) break;
    tier = t;
  }
  return tier;
}

double estimated_market_entry(Side side, double mark, const Costs& k) {
  const double adj = k.half_spread + k.slippage;
  return is_buy(side) ? mark * (1 + adj) : mark * (1 - adj);
}

std::optional<double> liquidation_price(Side side, double qty, double entry, double margin, const Costs& k) {
  const double u = qty * k.multiplier;
  if (u <= 0) return std::nullopt;
  const double mmr = margin_tier(k, u * entry).mmr;
  if (is_buy(side)) {
    const double num = u * entry - margin;
    if (num <= 0) return std::nullopt;
    return num / (u * (1 - mmr));
  }
  return (u * entry + margin) / (u * (1 + mmr));
}

std::optional<double> relevered_liquidation(Side side, double qty, double entry, int leverage, const Costs& k) {
  const double margin = std::fabs(qty) * entry * k.multiplier / std::max(1, leverage);
  return liquidation_price(side, std::fabs(qty), entry, margin, k);
}

std::optional<double> liquidation_after(const TicketInput& in, const OpenPosition& pos, const Costs& k) {
  const double held = std::fabs(pos.qty);
  const int lev = std::max(1, in.leverage);
  const double held_margin = lev == pos.leverage ? pos.margin : held * pos.entry * k.multiplier / lev;
  const Side pos_side = pos.qty > 0 ? Side::Buy : Side::Sell;
  if (held == 0 || pos_side == in.side) {
    const double total = held + in.qty;
    const double avg = total > 0 ? (held * pos.entry + in.qty * in.entry) / total : in.entry;
    return liquidation_price(in.side, total, avg, held_margin + in.qty * in.entry * k.multiplier / lev, k);
  }
  if (in.qty < held) {
    return liquidation_price(pos_side, held - in.qty, pos.entry, held_margin * (held - in.qty) / held, k);
  }
  const double rest = in.qty - held;
  if (rest <= 0) return std::nullopt;
  return liquidation_price(in.side, rest, in.entry, rest * in.entry * k.multiplier / lev, k);
}

TicketEstimate estimate(const TicketInput& in, const Costs& k, const std::optional<OpenPosition>& pos) {
  TicketEstimate e;
  e.notional = in.qty * k.multiplier * in.entry;
  e.margin = e.notional / std::max(1, in.leverage);
  e.fee = e.notional * k.taker_fee;
  e.liquidation = pos ? liquidation_after(in, *pos, k) : liquidation_price(in.side, in.qty, in.entry, e.margin, k);
  const bool on_order_side =
      !pos || pos->qty == 0 || (pos->qty > 0) == is_buy(in.side) || in.qty > std::fabs(pos->qty);
  if (!in.stop || !(*in.stop > 0)) return e;
  const double stop = *in.stop;
  const double cost = k.half_spread + k.slippage;
  const double exit_adj = is_buy(in.side) ? stop * (1 - cost) : stop * (1 + cost);
  const double per_unit = is_buy(in.side) ? in.entry - exit_adj : exit_adj - in.entry;
  e.planned_risk = per_unit * in.qty * k.multiplier + e.fee + in.qty * k.multiplier * exit_adj * k.taker_fee;
  if (e.liquidation && on_order_side) {
    e.stop_beyond_liquidation = is_buy(in.side) ? stop <= *e.liquidation : stop >= *e.liquidation;
  }
  return e;
}

double qty_for_risk(double equity, double risk_pct, double entry, double stop, const Costs& k, double lot) {
  const double budget = equity * (risk_pct / 100);
  const double distance = std::fabs(entry - stop);
  if (budget <= 0 || distance <= 0) return 0;
  const double cost_per_unit = entry * k.taker_fee + stop * (k.taker_fee + k.half_spread + k.slippage);
  const double units = budget / ((distance + cost_per_unit) * k.multiplier);
  return std::floor(units / lot + 1e-9) * lot;
}

double required_margin(double inc_qty, double entry, int leverage, const Costs& k) {
  const double notional = inc_qty * k.multiplier * entry;
  return notional / std::max(1, leverage) + notional * k.taker_fee;
}

Fit fit_to_margin(double qty, double entry, double available, int from, int cap, const Costs& k, double lot) {
  const double tier_cap = margin_tier(k, qty * k.multiplier * entry).max_leverage;
  const int top = tier_cap < cap ? static_cast<int>(tier_cap) : cap;
  for (int lev = std::max(1, from); lev <= top; ++lev) {
    if (required_margin(qty, entry, lev, k) <= available) return {qty, lev};
  }
  const double per_unit = required_margin(1, entry, top, k);
  const double fits = per_unit > 0 ? std::floor(available / per_unit / lot + 1e-9) * lot : 0;
  return {std::min(qty, fits), top};
}

double qty_for_notional(double notional, double entry, const Costs& k, double lot) {
  if (!(notional > 0) || !(entry > 0)) return 0;
  return floor_lot(notional / (entry * k.multiplier), lot);
}

double qty_for_equity_pct(double equity, double pct, double entry, const Costs& k, double lot) {
  return qty_for_notional(equity * (pct / 100), entry, k, lot);
}

double sized_qty(SizeBy by, const SizeInputs& x, const Costs& k, double lot) {
  if (by == SizeBy::Notional) return qty_for_notional(x.amount, x.entry, k, lot);
  if (by == SizeBy::Equity) return qty_for_equity_pct(x.equity, x.amount, x.entry, k, lot);
  if (by == SizeBy::Risk && x.stop) return x.amount > 0 ? qty_for_risk(x.equity, x.amount, x.entry, *x.stop, k, lot) : 0;
  return floor_lot(x.qty, lot);
}

std::optional<OpposingEffect> opposing_effect(double position, Side side, double qty, double lot) {
  if (position == 0 || qty <= 0 || (position > 0) == is_buy(side)) return std::nullopt;
  const double open = std::fabs(position);
  const double diff = std::round((qty - open) / lot) * lot;
  if (diff < 0) return OpposingEffect{Opposing::Reduce, open, -diff};
  return OpposingEffect{diff == 0 ? Opposing::Close : Opposing::Reverse, open, diff};
}

// ---- stops.ts ----

std::optional<double> stop_from_atr(Side side, double entry, std::optional<double> atr, double mult) {
  if (!atr || !(*atr > 0) || !(entry > 0)) return std::nullopt;
  const double stop = is_buy(side) ? entry - mult * *atr : entry + mult * *atr;
  if (stop > 0) return stop;
  return std::nullopt;
}

std::optional<double> stop_from_pct(Side side, double entry, double pct) {
  if (!(entry > 0) || !(pct > 0)) return std::nullopt;
  const double stop = is_buy(side) ? entry * (1 - pct / 100) : entry * (1 + pct / 100);
  if (stop > 0) return stop;
  return std::nullopt;
}

std::optional<std::string> trail_rate_from_percent(double pct) {
  if (!std::isfinite(pct) || pct <= 0 || pct >= 100) return std::nullopt;
  const double units = std::floor(pct * 10'000 + 0.5);  // Math.round, 1e-6 of the close
  if (!(units > 0)) return std::nullopt;
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.6f", units / 1'000'000);
  return std::string(buf);
}

std::optional<double> trail_trigger_from_percent(Side exit, double close, double pct, double tick) {
  if (!(close > 0) || !(pct > 0) || pct >= 100 || !(tick > 0)) return std::nullopt;
  const double ticks = close * (is_buy(exit) ? 1 + pct / 100 : 1 - pct / 100) / tick;
  const double rounded = is_buy(exit) ? std::ceil(ticks - 1e-9) : std::floor(ticks + 1e-9);
  return std::max(1.0, rounded) * tick;
}

std::optional<double> swing_stop(Side side, double entry, const std::vector<GameBar>& bars, double tick,
                                 int lookback) {
  if (bars.empty()) return std::nullopt;
  const std::size_t from = bars.size() > static_cast<std::size_t>(lookback) ? bars.size() - lookback : 0;
  double low = kInf;
  double high = -kInf;
  for (std::size_t i = from; i < bars.size(); ++i) {
    low = std::min(low, bars[i].low);
    high = std::max(high, bars[i].high);
  }
  if (is_buy(side)) {
    const double stop = low - tick;
    if (stop > 0 && stop < entry) return stop;
    return std::nullopt;
  }
  const double stop = high + tick;
  if (stop > entry) return stop;
  return std::nullopt;
}

std::optional<double> target_from_r(Side side, double entry, std::optional<double> stop, double r) {
  if (!stop) return std::nullopt;
  const double risk = std::fabs(entry - *stop);
  if (!(risk > 0)) return std::nullopt;
  const double tp = is_buy(side) ? entry + r * risk : entry - r * risk;
  if (tp > 0) return tp;
  return std::nullopt;
}

std::optional<Distance> distance_of(double entry, std::optional<double> level, std::optional<double> atr) {
  if (!level || !(entry > 0) || !(*level > 0)) return std::nullopt;
  const double abs = std::fabs(entry - *level);
  Distance d{abs, abs / entry * 100, std::nullopt};
  if (atr && *atr > 0) d.atr = abs / *atr;
  return d;
}

std::optional<double> reward_risk(Side side, double entry, std::optional<double> stop, std::optional<double> tp) {
  if (!stop || !tp) return std::nullopt;
  const double risk = is_buy(side) ? entry - *stop : *stop - entry;
  const double reward = is_buy(side) ? *tp - entry : entry - *tp;
  if (risk > 0 && reward > 0) return reward / risk;
  return std::nullopt;
}

bool stop_on_losing_side(Side side, double entry, double stop) { return is_buy(side) ? stop < entry : stop > entry; }

bool target_on_winning_side(Side side, double entry, double tp) { return is_buy(side) ? tp > entry : tp < entry; }

}  // namespace tradelab::desktop::game
