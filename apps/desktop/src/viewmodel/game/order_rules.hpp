// Order rules of execution policy v18 and the trailing stop as the terminal shows them (port of the
// web's orderRules.ts and stops.ts trailTrigger, add-practice-and-advanced-strategies D6, D7, D10). The
// engine decides every fill, expiry and link; these helpers only choose what the ticket sends and how
// the orders panel and the chart name it. Parity fixture: orderRules.json.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/game_state.hpp"
#include "model/i18n.hpp"
#include "model/progress.hpp"

namespace tradelab::desktop::game {

// ---- time in force (D6) ----

// What the ticket offers: "day" is bars:N with N = one day of bars.
enum class TifChoice : std::uint8_t { Gtc, Day, Bars, Ioc, Fok };
inline constexpr std::array<TifChoice, 5> kTifChoices = {TifChoice::Gtc, TifChoice::Day, TifChoice::Bars, TifChoice::Ioc,
                                                          TifChoice::Fok};
std::string_view tif_choice_code(TifChoice c);  // gtc | day | bars | ioc | fok
std::optional<TifChoice> tif_choice_from(std::string_view code);

// The bars in one day at this market's interval (86,400 / step_seconds), at least 1.
int day_bars(int step_seconds);
// Bars an order placed now can still work on: up to the run's last bar.
int bars_left(int total_bars, int step);
// Why a choice cannot go with this order type ("needs_limit", "market_immediate"), or "".
std::string tif_block(TifChoice choice, std::string_view type);

// The time in force a ticket sends ("gtc" | "bars" with bars | "ioc" | "fok"), or error "bars_range":
// N bars must be between 1 and the bars left ("day" longer than the run is cut to what is left).
struct TifOrder {
  std::string tif;
  int bars = 0;
  std::string error;
  [[nodiscard]] bool ok() const { return error.empty(); }
};
TifOrder tif_order(TifChoice choice, double bars_input, int step_seconds, int left);

// The orders panel's time-in-force cell: the kind and, for bars/ioc/fok, the last bar it can execute on.
struct TifCell {
  std::string kind;
  std::optional<int> until;
};
TifCell tif_cell(const Order& o);

// ---- player-built OCO links (D7) ----

// Why two working orders cannot be linked (the engine's invalid_oco): "same", "child", "linked"; "".
std::string link_problem(const Order& a, const Order& b);
// The other working order of a link, if it still works.
const Order* link_partner(const Order& o, const std::vector<Order>& orders);
// The link mark on an order and its chart line: "⛓#3" names the partner; "" without one.
std::string link_mark(const Order& o, const std::vector<Order>& orders);

// ---- trailing stop (D10) ----

// The close the trigger follows: the highest close (a sell stop) or the lowest (a buy stop) from the
// placement bar to the current one; only bars the player has seen (non-finite closes skipped).
struct BestClose {
  int step = 0;
  double close = 0;
};
std::optional<BestClose> best_close(const std::vector<double>& closes, int placed_step, bool exit_sell);

// The engine's trailing trigger for a best close (accounting.cpp trail_trigger, exact fixed point): the
// distance is the fixed one or ceil(close x rate) at 1e-8; the trigger is close - distance for a sell,
// close + distance for a buy, rounded outward onto the tick, at least one tick. Decimal strings in and
// out ("101.96000000").
std::string trail_trigger(bool exit_sell, std::string_view close, std::string_view distance, std::string_view rate,
                          std::string_view tick);

// The chart's trailing label: "TRAIL 2% · 101,96" or "TRAIL 1,50 · 101,96".
std::string trail_label(const Order& o, Locale locale);

// A planned trailing stop (D10): recorded with the plan, sent only when the player clicks after the fill.
using PlannedTrail = desktop::PlanTrail;
// The trailing order a planned trail becomes: a fixed distance on the tick ("trail") or the engine
// rate ("rate"); nullopt when the value is not usable.
struct PlannedTrailOrder {
  std::string trail;
  std::string rate;
};
std::optional<PlannedTrailOrder> planned_trail_order(const PlannedTrail& p, double tick, Locale locale);

}  // namespace tradelab::desktop::game
