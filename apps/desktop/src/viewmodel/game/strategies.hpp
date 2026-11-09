// Strategy builder model (port of the web's strategies.ts, add-academy-and-strategies design D3):
// templates as legs on the listed chain, the submission order, and the requests for the engine's
// what-if valuation and combo dry run. Every number the builder shows comes from those engine calls;
// this module only chooses legs. Checked against the web by the parity fixtures (strategies.json).
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model/game_state.hpp"

namespace tradelab::desktop::game {

enum class Template : std::uint8_t {
  BullCall,
  BearPut,
  CoveredCall,
  ProtectivePut,
  Straddle,
  Strangle,
  IronCondor,
  // Two expiries (add-practice-and-advanced-strategies D3): the near expiry sold, the far one bought.
  CalendarCall,
  CalendarPut,
  DiagonalCall,
  DiagonalPut
};

inline constexpr std::array<Template, 11> kTemplates = {
    Template::BullCall, Template::BearPut,      Template::CoveredCall, Template::ProtectivePut,
    Template::Straddle, Template::Strangle,     Template::IronCondor,  Template::CalendarCall,
    Template::CalendarPut, Template::DiagonalCall, Template::DiagonalPut};
// The templates on one expiry (the Academy's run-less builder has no second expiry to offer).
inline constexpr std::array<Template, 7> kOneExpiryTemplates = {Template::BullCall,      Template::BearPut,  Template::CoveredCall,
                                                                Template::ProtectivePut, Template::Straddle, Template::Strangle,
                                                                Template::IronCondor};
inline constexpr std::size_t kMaxOptionLegs = 4;

// "bull_call", "bear_put", ... (the web's TemplateId).
std::string_view template_code(Template t);
std::optional<Template> template_from(std::string_view code);

enum class LegKind : std::uint8_t { Call, Put, Perp };
std::string_view leg_kind_code(LegKind k);  // "call" | "put" | "perp"
std::optional<LegKind> leg_kind_from(std::string_view code);

struct BuilderLeg {
  LegKind kind = LegKind::Call;
  bool buy = true;
  std::optional<double> strike;  // nullopt for the perpetual
  // Contracts (options) or underlying units (the perpetual: contracts x the option multiplier).
  double qty = 1;
  std::optional<int> expiry;    // the leg's expiry step (D3: up to two per structure); nullopt = the builder's
  std::optional<double> limit;  // D2: a limit premium (option legs); nullopt = a market leg
  bool operator==(const BuilderLeg&) const = default;
};

// Calendars and diagonals: built by two_expiry_template_legs, never by template_legs.
[[nodiscard]] bool two_expiry(Template t);
[[nodiscard]] bool template_writes(Template t, bool short_side);
[[nodiscard]] bool template_shortable(Template t);
// A combo's structure name: a template, its short version or "custom" (the web's StrategyId).
[[nodiscard]] std::string strategy_of(Template t, bool short_side);

// Expiries a combo can be built on now: listed series whose orders would not fall in the settlement window.
std::vector<int> expiries_of(const std::vector<OptionQuote>& chain);
std::vector<double> strikes_of(const std::vector<OptionQuote>& chain, int expiry);
const OptionQuote* series_for(const std::vector<OptionQuote>& chain, int expiry, LegKind kind, double strike);

// A template's legs on one expiry's strikes around the forward `f`, `n` contracts each (the perpetual
// leg in underlying units); nullopt when the chain lacks a strike the template needs.
std::optional<std::vector<BuilderLeg>> template_legs(Template t, const std::vector<double>& strikes, double f, double n,
                                                     bool short_side = false);

// A calendar or diagonal on two expiries, `n` contracts each: a calendar sells and buys the strike at
// the money; a diagonal sells the next strike out of the money on the near expiry and buys the one at
// the money on the far one. nullopt when the far expiry lacks the strike at the money or the near one
// lacks the strike the template sells.
struct ExpiryStrikes {
  int step = 0;
  std::vector<double> strikes;
};
std::optional<std::vector<BuilderLeg>> two_expiry_template_legs(Template t, const ExpiryStrikes& near, const ExpiryStrikes& far,
                                                                double f, double n, bool short_side = false);

// Submission order: long options, then the perpetual, then short options (stable within each group).
std::vector<BuilderLeg> submission_order(const std::vector<BuilderLeg>& legs);
[[nodiscard]] bool writes_options(const std::vector<BuilderLeg>& legs);

// Why the legs cannot be combined: "empty", "too_many", "two_perps", "expiries" (more than two), "qty";
// "" when they can. Legs without an expiry are on `expiry`.
std::string legs_problem(const std::vector<BuilderLeg>& legs, int expiry = 0);
// The expiries the option legs use (one or two), ascending.
std::vector<int> leg_expiries(const std::vector<BuilderLeg>& legs, int expiry);

// One builder leg as the order it sends: a series id (0 = the perpetual), side and quantity text, and an
// option limit leg's premium on the premium tick (D2; nullopt = market).
struct OrderLeg {
  int series = 0;
  bool buy = true;
  std::string qty;
  std::optional<std::string> premium;
  bool operator==(const OrderLeg&) const = default;
};

// A premium on the tick grid as text.
std::string premium_text(double v, double tick);
// The default limit of a leg (D2): the quote's mid rounded to the tick toward the passive side (down
// for a buy, up for a sell), never below one tick.
double default_limit(double mid, bool buy, double tick);

// Perpetual contracts for `units` of the underlying, on the contract's lot grid.
std::string perp_contracts(const GameState& g, double units);
// The legs' orders in submission order; nullopt when a strike is not listed on its leg's expiry.
std::optional<std::vector<OrderLeg>> order_legs(const GameState& g, int expiry, const std::vector<BuilderLeg>& legs);

// The player's long perpetual ("use my long"): its quantity and entry as the engine wrote them, and the
// trigger of the working reduce-only stop that closes all of it (the highest when several do).
struct HeldLong {
  std::string qty;
  std::string entry;
  std::optional<std::string> stop;
};
// The highest trigger of the working reduce-only sell stops (stop market or trailing) that close the
// whole long; nullopt without a long or without such a stop.
std::optional<std::string> long_stop(const GameState& g);
std::optional<HeldLong> held_long(const GameState& g);
// Option contracts covering a held long: its underlying units over the option multiplier, to the lot.
double cover_contracts(const GameState& g, const HeldLong& held);
[[nodiscard]] bool covers_long(Template t);

// A what-if or dry-run leg as the engine reads it ({"series"} or {"kind":"perp"}, "side", "qty",
// optional "entry" and, on the perpetual, optional "stop": the expiry analytics treat that leg as
// closed at its stop).
struct AnalysisLeg {
  int series = 0;  // 0 with kind "perp"
  bool buy = true;
  std::string qty;
  std::optional<std::string> entry;
  std::optional<std::string> stop;
  // tl_combo_preview only (D2): an option limit leg ("type":"limit", "premium") and the combo's time in
  // force ("" = gtc, not written).
  std::optional<std::string> premium;
  std::string tif;
  int tif_bars = 0;
  bool operator==(const AnalysisLeg&) const = default;
};

// A combo's time in force for its limit legs (D2): "gtc", "bars" with tif_bars, "ioc" or "fok".
struct ComboTif {
  std::string tif;
  int tif_bars = 0;
};

// The dry run's legs: limit legs carry their premium and the combo's time in force (market legs ignore it).
std::vector<AnalysisLeg> combo_legs(const std::vector<OrderLeg>& orders, const std::optional<ComboTif>& tif = std::nullopt);
// The combo's orders at the prices they would cross (a limit leg at its limit premium), plus the held long when "use my long" replaces
// the perpetual order (valued from its entry). The long carries its stop, so the engine's maximum loss
// (the combo's planned risk) is the loss to that stop, not the long's whole value down to a price of 0.
std::vector<AnalysisLeg> whatif_legs(const std::vector<OrderLeg>& orders, const std::optional<HeldLong>& held);
// The player's open positions as what-if legs at their entries (the terminal lab "from positions").
std::vector<AnalysisLeg> position_legs(const GameState& g);

// Contracts per leg whose maximum loss is `risk_pct` % of equity (the builder's sizing aid): the
// engine's maximum loss at `n` contracts scaled to the budget, down to the option lot. nullopt without
// a bounded, positive maximum loss (or with a non-positive input).
std::optional<double> contracts_for_risk(double equity, double risk_pct, std::optional<double> max_loss, double n, double lot);

// ---- roll and hedge (D3, D9) ----

// A held option leg of a combo: series and signed contracts (> 0 long).
struct HeldLeg {
  int series = 0;
  double qty = 0;
};

struct RollPlan {
  std::vector<OrderLeg> orders;  // closing the near expiry's held legs, then opening them on `to`; buys first
  int from = 0;
  int to = 0;
};

// The orders of a roll: close every held option leg of the near expiry and open the same structure on
// the later expiry `to`, at the same strikes when listed (else the nearest listed), or re-centred at the
// money. Ordered buys, then sells. nullopt when nothing expires first or a strike cannot be found.
std::optional<RollPlan> roll_orders(const GameState& g, const std::vector<HeldLeg>& held, int to, bool recenter, double f);
// Later listed expiries a roll from `from` can go to (the next one first).
std::vector<int> roll_targets(const std::vector<OptionQuote>& chain, int from);
// The perpetual hedge to neutral (D9): -delta_units in contracts, rounded toward zero to the lot;
// nullopt when that is less than one lot.
std::optional<OrderLeg> hedge_order(const GameState& g, double delta_units);

// {"legs":[...]} for tl_combo_preview, or the "legs" array of a what-if request.
std::string legs_json(const std::vector<AnalysisLeg>& legs);

}  // namespace tradelab::desktop::game
