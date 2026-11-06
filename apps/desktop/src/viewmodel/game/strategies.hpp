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

enum class Template : std::uint8_t { BullCall, BearPut, CoveredCall, ProtectivePut, Straddle, Strangle, IronCondor };

inline constexpr std::array<Template, 7> kTemplates = {Template::BullCall,      Template::BearPut,  Template::CoveredCall,
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
  bool operator==(const BuilderLeg&) const = default;
};

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

// Submission order: long options, then the perpetual, then short options (stable within each group).
std::vector<BuilderLeg> submission_order(const std::vector<BuilderLeg>& legs);
[[nodiscard]] bool writes_options(const std::vector<BuilderLeg>& legs);

// Why the legs cannot be combined: "empty", "too_many", "two_perps", "qty"; "" when they can.
std::string legs_problem(const std::vector<BuilderLeg>& legs);

// One builder leg as the order it sends: a series id (0 = the perpetual), side and quantity text.
struct OrderLeg {
  int series = 0;
  bool buy = true;
  std::string qty;
  bool operator==(const OrderLeg&) const = default;
};

// Perpetual contracts for `units` of the underlying, on the contract's lot grid.
std::string perp_contracts(const GameState& g, double units);
// The legs' orders in submission order; nullopt when a strike is not listed on `expiry`.
std::optional<std::vector<OrderLeg>> order_legs(const GameState& g, int expiry, const std::vector<BuilderLeg>& legs);

// The player's long perpetual ("use my long"): its quantity and entry as the engine wrote them.
struct HeldLong {
  std::string qty;
  std::string entry;
};
std::optional<HeldLong> held_long(const GameState& g);
// Option contracts covering a held long: its underlying units over the option multiplier, to the lot.
double cover_contracts(const GameState& g, const HeldLong& held);
[[nodiscard]] bool covers_long(Template t);

// A what-if or dry-run leg as the engine reads it ({"series"} or {"kind":"perp"}, "side", "qty",
// optional "entry").
struct AnalysisLeg {
  int series = 0;  // 0 with kind "perp"
  bool buy = true;
  std::string qty;
  std::optional<std::string> entry;
  bool operator==(const AnalysisLeg&) const = default;
};

std::vector<AnalysisLeg> combo_legs(const std::vector<OrderLeg>& orders);
// The combo's orders at the prices they would cross, plus the held long when "use my long" replaces
// the perpetual order (valued from its entry).
std::vector<AnalysisLeg> whatif_legs(const std::vector<OrderLeg>& orders, const std::optional<HeldLong>& held);
// The player's open positions as what-if legs at their entries (the terminal lab "from positions").
std::vector<AnalysisLeg> position_legs(const GameState& g);

// {"legs":[...]} for tl_combo_preview, or the "legs" array of a what-if request.
std::string legs_json(const std::vector<AnalysisLeg>& legs);

}  // namespace tradelab::desktop::game
