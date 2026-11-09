#pragma once

// Helpers shared by the scoring v14 debrief facts (src/debrief_v14.cpp) and the coach review
// (src/coach_review.cpp): they read the recorded orders, plans and events as of a step, and the bars
// up to it, never a later bar.

#include <algorithm>
#include <cstdint>
#include <optional>

#include "tradelab/indicators.hpp"
#include "tradelab/scoring.hpp"

namespace tradelab::bias {

constexpr std::int64_t kBp = 10'000;
constexpr std::uint32_t kAtrPeriod = 14;
constexpr std::uint32_t kSmaPeriod = 20;

inline std::int64_t abs64(std::int64_t v) { return v < 0 ? -v : v; }
inline std::int64_t dir_of(OrderSide s) { return s == OrderSide::Buy ? 1 : -1; }
inline bool perp_fill(const Event& e) { return e.type == EventType::Fill && e.series == 0; }

inline std::int64_t ratio_bp(i128 num, i128 den) {
  if (den <= 0) return 0;
  return static_cast<std::int64_t>(div_round(num * kBp, den, Rounding::HalfEven));
}

const Order* order_by_id(const RunState& st, std::uint32_t id);
// A perpetual entry: not reduce-only, not a bracket child.
bool entry_order(const Order* o);
// Option contracts held after the events up to `step`.
bool holds_options(const RunState& st, std::uint32_t step);
// An exposure-increasing perpetual entry the bias rules judge: no option structure held when decided.
bool speculative_entry(const RunState& st, const Order* o);
bool is_stop(const Order& o);
// Last step an order could act on: the step of its last update or fill, or UINT32_MAX while working.
std::uint32_t end_step(const RunState& st, const Order& o);
// The protective stop of an entry: its bracket stop, else the nearest reduce-only perpetual stop on the
// exit side working at decision step d (0 = none).
Price protective_stop(const RunState& st, const Order& entry, std::uint32_t d);
// Consecutive closes in direction `dir` ending at bar d.
std::uint32_t closes_in_direction(const Scenario& sc, std::uint32_t d, std::int64_t dir);

// A running perpetual position over the fills, as the engine books it.
struct Book {
  std::int64_t qty = 0;  // signed raw Qty
  Price entry;           // average entry (half-even weighted, like accounting's increase)

  // Applies a fill; returns the quantity (raw, unsigned) that grew the position.
  std::int64_t apply(OrderSide side, Qty q, Price px) {
    const std::int64_t d = dir_of(side);
    std::int64_t rest = q.raw;
    if (qty != 0 && (qty > 0) != (d > 0)) {
      const std::int64_t closing = std::min(rest, abs64(qty));
      qty += d * closing;
      rest -= closing;
      if (qty == 0) entry = Price{};
    }
    if (rest == 0) return 0;
    const std::int64_t old_abs = abs64(qty);
    const i128 w = static_cast<i128>(old_abs) * entry.raw + static_cast<i128>(rest) * px.raw;
    entry = Price::from_raw(static_cast<std::int64_t>(div_round(w, old_abs + rest, Rounding::HalfEven)));
    qty = d * (old_abs + rest);
    return rest;
  }
};

inline Money risk_to(Price avg, Price stop, std::int64_t qty_abs) {
  if (stop.raw == 0 || qty_abs == 0) return Money{};
  const std::int64_t gap = abs64(avg.raw - stop.raw);
  return Money::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(gap) * qty_abs, Qty::kScale, Rounding::HalfEven)));
}

// ATR(14) and SMA(20) over the run (each value reads bars up to its own index only).
struct Reads {
  Series atr;
  Series sma;
};

// The fomo_entry rule at an entry's decision step (bars up to that step only).
std::optional<Finding> fomo_at(const Scenario& sc, const RunState& st, const Reads& r, const Order& o);

// The account as the engine books it, applied event by event (accounting.cpp, options_exec.cpp).
struct Shadow {
  RunState st;
  void apply(const Scenario& sc, const Event& e);

 private:
  void perp_fill_event(const Event& e);
  void option_fill_event(const Scenario& sc, const Event& e);
};

}  // namespace tradelab::bias
