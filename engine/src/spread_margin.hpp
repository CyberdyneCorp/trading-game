#pragma once

// Short-option margin requirements per position: per leg, or with defined-risk pairing when the
// scenario declares OptionSpec::spread_margin (design D5 of add-academy-and-strategies).

#include <vector>

#include "options_exec.hpp"

namespace tradelab::detail {

// A signed option holding in contracts: a position, or a position plus working buys.
struct Holding {
  std::uint32_t series = 0;
  Qty qty;
};

struct ShortRequirement {
  std::uint32_t series = 0;
  Money initial;
  Money maintenance;
};

// The run's option positions; with `working_buys`, plus the active option buy orders placed at the
// current step (they count as longs for pairing at acceptance, never at execution).
std::vector<Holding> holdings_of(const RunState& st, bool working_buys);
void add_holding(std::vector<Holding>& h, std::uint32_t series, Qty delta);

// One entry per short holding (in holding order without the flag, else in series-id order), with quotes at `step` for forward `f`.
// Pairing (flag on): shorts in series-id order each take the free longs of the same kind and
// expiry with the lowest worst-loss-at-expiry first (series id on ties); a paired quantity is
// margined at min(per-leg margin, that requirement x units), the rest per leg; within one expiry,
// when the losing put and call pairs cannot lose together, only the larger side is margined.
std::vector<ShortRequirement> short_requirements(const Scenario& sc, std::vector<Holding> h, std::uint32_t step, Price f);

Money total_initial(const std::vector<ShortRequirement>& r);

}  // namespace tradelab::detail
