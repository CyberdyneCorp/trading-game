// Port of the web's sizing.test.ts and stops.test.ts vectors (same numbers).
#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "viewmodel/game/sizing.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;

namespace {

Costs zero_costs() { return {1, 0, 0.01, 0, 0, {}}; }
Costs k5() { return {1, 0.0005, 0.005, 0, 0, {}}; }
Costs tiered() {
  return {1, 0, 0.005, 0, 0, {{0, 0.005, 50}, {50'000, 0.01, 20}, {250'000, 0.025, 10}, {1'000'000, 0.05, 5}}};
}

}  // namespace

TEST_CASE("sizing: the spec example 10,000 x 1% / (5 + 1) = 16.6 and lot rounding") {
  CHECK(qty_for_risk(10'000, 1, 100, 94, zero_costs(), 0.1) == doctest::Approx(16.6));
  CHECK(qty_for_risk(10'000, 1, 100, 97, zero_costs(), 1) == 33);
}

TEST_CASE("sizing: liquidation matches the engine formula (FUT02) and flags a stop beyond it (FUT04)") {
  CHECK(*liquidation_price(Side::Buy, 10, 100, 100, zero_costs()) == doctest::Approx(90.909).epsilon(1e-4));
  CHECK(*liquidation_price(Side::Sell, 10, 100, 100, zero_costs()) == doctest::Approx(108.911).epsilon(1e-4));
  CHECK_FALSE(liquidation_price(Side::Buy, 1, 100, 100, zero_costs()).has_value());
  CHECK(estimate({Side::Buy, 10, 100, 10, 88.0}, zero_costs()).stop_beyond_liquidation);
  const TicketEstimate ok = estimate({Side::Buy, 10, 100, 10, 95.0}, zero_costs());
  CHECK_FALSE(ok.stop_beyond_liquidation);
  CHECK(*ok.planned_risk == doctest::Approx(50));
}

TEST_CASE("sizing: re-margining an open position and the shocked costs") {
  CHECK(*relevered_liquidation(Side::Buy, 51, 100.34, 20, k5()) == doctest::Approx(95.8).epsilon(1e-3));
  CHECK(*relevered_liquidation(Side::Buy, 51, 100.34, 10, k5()) == doctest::Approx(90.75).epsilon(1e-3));
  CHECK(*relevered_liquidation(Side::Sell, -51, 100.34, 10, k5()) > 100.34);
  Contract c;
  c.half_spread = 0.0001;
  c.slippage = 0.0002;
  c.capacity_fraction = 0.1;
  const Liquidity shocked{0.0003, 0.0006, 0.05};
  CHECK(estimated_market_entry(Side::Buy, 100, costs_of(c, std::nullopt)) == doctest::Approx(100.03));
  CHECK(estimated_market_entry(Side::Buy, 100, costs_of(c, shocked)) == doctest::Approx(100.09));
  const ShockFactors f = shock_factors(c, shocked);
  CHECK(f.cost == doctest::Approx(3));
  CHECK(f.capacity == doctest::Approx(0.5));
}

TEST_CASE("sizing: fitting to the available balance raises leverage, never lowers it, and respects tiers") {
  CHECK(required_margin(100, 100, 1, k5()) == doctest::Approx(10'005));
  CHECK(required_margin(100, 100, 2, k5()) == doctest::Approx(5'005));
  Fit f = fit_to_margin(120.4, 102, 10'000, 1, 20, k5(), 0.1);
  CHECK(f.qty == doctest::Approx(120.4));
  CHECK(f.leverage == 2);
  CHECK(fit_to_margin(41.8, 100, 10'000, 3, 5, k5(), 0.1).leverage == 3);
  f = fit_to_margin(500, 100, 10'000, 1, 5, k5(), 0.1);
  CHECK(f.leverage == 5);
  CHECK(f.qty < 500);
  CHECK(required_margin(f.qty, 100, 5, k5()) <= 10'000);
  CHECK(required_margin(f.qty + 0.1, 100, 5, k5()) > 10'000);
  f = fit_to_margin(3'000, 100, 40'000, 15, 20, tiered(), 0.1);
  CHECK(f.qty == doctest::Approx(3'000));
  CHECK(f.leverage == 10);
}

TEST_CASE("sizing: notional, % of equity and the four modes") {
  CHECK(qty_for_notional(1'000, 103, k5(), 0.1) == doctest::Approx(9.7));
  Costs k10 = k5();
  k10.multiplier = 10;
  CHECK(qty_for_notional(1'000, 103, k10, 0.1) == doctest::Approx(0.9));
  CHECK(qty_for_notional(std::numeric_limits<double>::quiet_NaN(), 103, k5(), 0.1) == 0);
  CHECK(qty_for_equity_pct(10'000, 10, 100, k5(), 0.1) == doctest::Approx(10));
  CHECK(qty_for_equity_pct(10'000, 250, 100, k5(), 1) == 250);
  const SizeInputs x{10'000, 103, std::nullopt, 7, 0};
  CHECK(sized_qty(SizeBy::Qty, {10'000, 103, std::nullopt, 7.25, 0}, k5(), 0.1) == doctest::Approx(7.2));
  CHECK(sized_qty(SizeBy::Notional, {10'000, 103, std::nullopt, 7, 1'030}, k5(), 0.1) == doctest::Approx(10));
  CHECK(sized_qty(SizeBy::Equity, {10'000, 103, std::nullopt, 7, 20.6}, k5(), 1) == 20);
  CHECK(sized_qty(SizeBy::Risk, x, k5(), 0.1) == doctest::Approx(7));
  CHECK(sized_qty(SizeBy::Risk, {10'000, 103, 100.0, 7, 1}, k5(), 0.1) == qty_for_risk(10'000, 1, 103, 100, k5(), 0.1));
}

TEST_CASE("sizing: an opposite order reduces, closes or reverses the open position") {
  auto e = opposing_effect(60, Side::Sell, 52.3, 0.1);
  REQUIRE(e);
  CHECK(e->kind == Opposing::Reduce);
  CHECK(e->after == doctest::Approx(7.7));
  e = opposing_effect(60, Side::Sell, 60, 0.1);
  CHECK(e->kind == Opposing::Close);
  e = opposing_effect(-10, Side::Buy, 25.5, 0.1);
  CHECK(e->kind == Opposing::Reverse);
  CHECK(e->after == doctest::Approx(15.5));
  CHECK_FALSE(opposing_effect(0, Side::Sell, 10, 0.1));
  CHECK_FALSE(opposing_effect(5, Side::Buy, 10, 0.1));
}

TEST_CASE("sizing: margin tiers and the liquidation of the position an order leads to (m03 seed 77)") {
  CHECK(margin_tier(tiered(), 49'999).mmr == doctest::Approx(0.005));
  CHECK(margin_tier(tiered(), 50'000).mmr == doctest::Approx(0.01));
  CHECK(margin_tier(tiered(), 300'000).max_leverage == 10);
  CHECK(std::isinf(margin_tier(zero_costs(), 1e9).max_leverage));
  CHECK(*liquidation_price(Side::Buy, 400, 100, 4'000, tiered()) == doctest::Approx(90.4523).epsilon(1e-5));
  CHECK(*liquidation_price(Side::Buy, 600, 100, 6'000, tiered()) == doctest::Approx(90.9091).epsilon(1e-5));
  const OpenPosition pos{400, 101.9, 2'038, 20};
  const TicketInput add{Side::Buy, 200, 101.79, 20, std::nullopt};
  CHECK(*estimate(add, tiered()).liquidation == doctest::Approx(97.19).epsilon(1e-4));
  CHECK(*estimate(add, tiered(), pos).liquidation == doctest::Approx(97.75).epsilon(1e-4));
  const TicketInput cut{Side::Sell, 100, 101.79, 20, std::nullopt};
  CHECK(*liquidation_after(cut, pos, tiered()) ==
        doctest::Approx(*liquidation_price(Side::Buy, 300, 101.9, 2'038 * 0.75, tiered())));
  CHECK_FALSE(liquidation_after({Side::Sell, 400, 101.79, 20, std::nullopt}, pos, tiered()));
  CHECK_FALSE(estimate({Side::Sell, 100, 101.79, 20, 103.0}, tiered(), pos).stop_beyond_liquidation);
}

TEST_CASE("stops: ATR, percent, swing, R targets, distances and sides") {
  CHECK(*stop_from_atr(Side::Buy, 100, 2.0, 1.5) == doctest::Approx(97));
  CHECK(*stop_from_atr(Side::Sell, 100, 2.0, 1) == doctest::Approx(102));
  CHECK_FALSE(stop_from_atr(Side::Buy, 100, std::nullopt, 1));
  CHECK(*stop_from_pct(Side::Buy, 100, 2) == doctest::Approx(98));
  CHECK(*stop_from_pct(Side::Sell, 100, 1) == doctest::Approx(101));
  std::vector<GameBar> bars(12, GameBar{100, 102, 98, 101, 10, true, false});
  bars[5].low = 95;
  bars[7].high = 106;
  CHECK(*swing_stop(Side::Buy, 100, bars, 0.01) == doctest::Approx(94.99));
  CHECK(*swing_stop(Side::Sell, 100, bars, 0.01) == doctest::Approx(106.01));
  CHECK_FALSE(swing_stop(Side::Buy, 90, bars, 0.01));
  CHECK(*target_from_r(Side::Buy, 100, 95.0, 2) == doctest::Approx(110));
  CHECK(*target_from_r(Side::Sell, 100, 105.0, 3) == doctest::Approx(85));
  const auto d = distance_of(100, 97.0, 2.0);
  CHECK(d->abs == doctest::Approx(3));
  CHECK(d->pct == doctest::Approx(3));
  CHECK(*d->atr == doctest::Approx(1.5));
  CHECK(*reward_risk(Side::Buy, 100, 95.0, 110.0) == doctest::Approx(2));
  CHECK_FALSE(reward_risk(Side::Buy, 100, 105.0, 110.0));
  CHECK(stop_on_losing_side(Side::Buy, 100, 99));
  CHECK_FALSE(stop_on_losing_side(Side::Sell, 100, 99));
  CHECK(target_on_winning_side(Side::Sell, 100, 90));
}

TEST_CASE("stops: trailing percent as the engine's rate and the first trigger on the tick") {
  CHECK(*trail_rate_from_percent(2.5) == "0.025000");
  CHECK(*trail_rate_from_percent(0.12345) == "0.001235");
  CHECK_FALSE(trail_rate_from_percent(0));
  CHECK_FALSE(trail_rate_from_percent(100));
  CHECK(*trail_trigger_from_percent(Side::Sell, 100, 2, 0.01) == doctest::Approx(98));
  CHECK(*trail_trigger_from_percent(Side::Buy, 100.005, 1, 0.01) == doctest::Approx(101.01));
}
