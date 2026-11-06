#pragma once

// Test helper: the builder's seven templates (design D3) as combo legs on a run's chain, strikes
// from the first expiry that is listed and not settling, at fixed steps around the at-the-money strike
// (the web builder picks its own strikes, apps/web/src/lib/game/strategies.ts).

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "tradelab/options.hpp"
#include "tradelab/whatif.hpp"

namespace tradelab::test {

enum class Template { BullCall, BearPut, CoveredCall, ProtectivePut, Straddle, Strangle, IronCondor };

inline constexpr Template kTemplates[] = {Template::BullCall, Template::BearPut,  Template::CoveredCall, Template::ProtectivePut,
                                          Template::Straddle, Template::Strangle, Template::IronCondor};

inline const OptionSeries* series_at(const Scenario& sc, std::uint32_t expiry, OptionKind k, Price strike) {
  for (const OptionSeries& s : sc.options.series) {
    if (s.expiry_step == expiry && s.kind == k && s.strike == strike) return &s;
  }
  return nullptr;
}

// Legs in the builder's submission order: long options, the perpetual, short options.
inline std::vector<ComboLeg> template_legs(const Scenario& sc, const RunState& st, Template t, const char* qty_text) {
  std::uint32_t expiry = 0;
  for (const OptionSeries& s : sc.options.series) {
    const bool ok = st.step >= s.list_step && st.step < s.expiry_step && !in_settlement_window(sc, s, st.step + 1);
    if (ok && (expiry == 0 || s.expiry_step < expiry)) expiry = s.expiry_step;
  }
  std::vector<Price> strikes;
  for (const OptionSeries& s : sc.options.series) {
    if (s.expiry_step == expiry) strikes.push_back(s.strike);
  }
  std::sort(strikes.begin(), strikes.end());
  strikes.erase(std::unique(strikes.begin(), strikes.end()), strikes.end());
  if (strikes.size() < 5) return {};
  const Price f = sc.bars[st.step].close;
  std::size_t atm = 0;
  for (std::size_t i = 1; i < strikes.size(); ++i) {
    if (std::abs(strikes[i].raw - f.raw) < std::abs(strikes[atm].raw - f.raw)) atm = i;
  }
  atm = std::clamp<std::size_t>(atm, 2, strikes.size() - 3);
  const Qty qty = parse_decimal<Qty>(qty_text).value();
  const auto leg = [&](OptionKind k, std::size_t i, OrderSide side) {
    return ComboLeg{false, series_at(sc, expiry, k, strikes[i])->id, side, qty};
  };
  const ComboLeg perp{true, 0, OrderSide::Buy, qty};
  constexpr OptionKind C = OptionKind::Call;
  constexpr OptionKind P = OptionKind::Put;
  constexpr OrderSide B = OrderSide::Buy;
  constexpr OrderSide S = OrderSide::Sell;
  switch (t) {
    case Template::BullCall: return {leg(C, atm, B), leg(C, atm + 1, S)};
    case Template::BearPut: return {leg(P, atm, B), leg(P, atm - 1, S)};
    case Template::CoveredCall: return {perp, leg(C, atm + 1, S)};
    case Template::ProtectivePut: return {perp, leg(P, atm - 1, B)};
    case Template::Straddle: return {leg(C, atm, B), leg(P, atm, B)};
    case Template::Strangle: return {leg(C, atm + 1, B), leg(P, atm - 1, B)};
    case Template::IronCondor: return {leg(P, atm - 2, B), leg(C, atm + 2, B), leg(P, atm - 1, S), leg(C, atm + 1, S)};
  }
  return {};
}

}  // namespace tradelab::test
