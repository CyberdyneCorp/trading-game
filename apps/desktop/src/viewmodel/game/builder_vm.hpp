// Strategy builder viewmodel (web StrategyBuilder.svelte, strategy-builder spec, design D3): templates
// from the chain, free legs on one expiry, "use my long", the engine's analysis (the what-if of the legs
// at the prices they would cross and the dry run of the orders, which changes nothing) and the combo
// draft the terminal submits as ordinary market orders, longs first. Every number it shows is the
// engine's; this class only chooses legs and formats answers.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/game_state.hpp"
#include "viewmodel/game/combos.hpp"
#include "viewmodel/game/lab_vm.hpp"
#include "viewmodel/game/strategies.hpp"
#include "viewmodel/game/texts.hpp"
#include "viewmodel/game/whatif_lab.hpp"

namespace tradelab::desktop::game {

// A combo ready to send: legs in submission order, its tag, name and the risk for the plan.
struct ComboDraft {
  std::vector<OrderLeg> orders;
  std::string combo;     // "c1"
  std::string strategy;  // "iron_condor", "custom", ...
  std::string label;     // "Iron condor · 4 pernas"
  std::optional<double> risk;
  bool unbounded = false;  // no maximum loss at expiry: `risk` is the margin required, not a worst case
};

struct TemplateButton {
  Template id = Template::BullCall;
  std::string label;
  bool enabled = true;
  bool selected = false;
};

struct BuilderLegRow {
  int kind = 0;    // 0 call, 1 put, 2 perpetual (the kind box's items)
  int side = 0;    // 0 buy, 1 sell
  int strike = -1;  // index in BuilderView::strikes, -1 for the perpetual
  std::string qty;
  bool no_bid = false;  // a market sell with no bid now: it tends to be cancelled at the open
};

struct BuilderView {
  bool available = false;  // options on and at least one expiry takes orders
  std::string title;
  std::vector<std::string> expiries;  // "barra 100 (40 barras)"
  int expiry_index = 0;
  std::vector<TemplateButton> templates;
  std::string write_locked;  // the reason, "" when writing is allowed
  std::string caption;       // the structure's name
  std::vector<std::string> kinds, sides, strikes;
  std::vector<BuilderLegRow> legs;
  bool can_add_leg = false;
  std::string use_long;  // the checkbox text, "" when it does not apply
  std::string problem;
  std::string no_analysis;
  std::vector<Line> preview;  // the dry run, leg by leg
  std::vector<std::pair<std::string, std::string>> totals;
  std::vector<std::pair<std::string, std::string>> analysis;
  std::vector<Line> table;
  bool has_whatif = false;
  std::string lab_positions;  // button text when the player holds positions
  std::string legging;
  bool can_submit = false;
};

class BuilderViewModel {
 public:
  int expiry = 0;
  std::optional<Template> tmpl;
  bool short_side = false;
  std::string qty = "1";  // contracts per leg as typed
  std::vector<BuilderLeg> legs;
  std::string strategy = "custom";
  bool use_long = false;

  // Keeps the expiry on one that takes orders now.
  void sync(const GameState& g);
  void set_expiry_index(const GameState& g, int index);
  void pick(const GameState& g, Template t, Locale L);
  void toggle_short(const GameState& g, Locale L);
  // The quantity field committed: every leg takes it (contracts; the perpetual its units).
  void apply_qty(Locale L);
  void set_kind(const GameState& g, std::size_t i, LegKind kind);
  void set_side(std::size_t i, bool buy);
  void set_strike(const GameState& g, std::size_t i, int strike_index);
  void set_leg_qty(std::size_t i, const std::string& text);
  void remove_leg(std::size_t i);
  void add_leg(const GameState& g, Locale L);

  [[nodiscard]] double contracts(Locale L) const;
  [[nodiscard]] std::optional<HeldLong> covered(const GameState& g) const;  // the held long "use my long" covers
  [[nodiscard]] std::vector<BuilderLeg> sent(const GameState& g) const;
  [[nodiscard]] std::optional<std::vector<OrderLeg>> orders(const GameState& g) const;
  // "empty" | "too_many" | "two_perps" | "qty" | "unlisted" | "write_locked" | "".
  [[nodiscard]] std::string problem(const GameState& g) const;

  // The analysis requests (what-if, dry run) for the legs as they stand; nullopt when there is nothing
  // to analyse. `key` changes with the legs and with the state they are valued in.
  struct Requests {
    std::string whatif, preview, key;
  };
  [[nodiscard]] std::optional<Requests> requests(const GameState& g) const;
  void clear_analysis();
  void answer_whatif(const std::string& text);
  void answer_preview(const std::string& text);
  [[nodiscard]] const std::optional<Analysis<WhatIfResult>>& whatif() const { return whatif_; }
  [[nodiscard]] const std::optional<Analysis<ComboPreview>>& preview() const { return preview_; }
  [[nodiscard]] bool pending() const { return pending_ > 0; }
  void expect(int replies) { pending_ = replies; }
  AnalysisTicket ticket;

  [[nodiscard]] bool can_submit(const GameState& g, bool busy) const;
  [[nodiscard]] std::optional<ComboDraft> draft(const Texts& t, const GameState& g, const std::vector<ComboRecord>& combos,
                                                bool busy) const;
  // The terminal lab's legs: the combo at the prices it would cross (plus the covered long).
  [[nodiscard]] std::vector<AnalysisLeg> lab_legs(const GameState& g) const;
  [[nodiscard]] BuilderView view(const Texts& t, const GameState& g, bool busy) const;

 private:
  [[nodiscard]] std::optional<double> risk() const;
  void view_legs(const Texts& t, const GameState& g, BuilderView& v) const;
  void view_analysis(const Texts& t, const GameState& g, BuilderView& v) const;

  std::optional<Analysis<WhatIfResult>> whatif_;
  std::optional<Analysis<ComboPreview>> preview_;
  int pending_ = 0;
};

// "+1 C 105,00" / "−2,5 perpétuo": a combo leg as the builder and the incomplete notice name it.
std::string order_leg_name(const Texts& t, const GameState& g, const OrderLeg& o);

}  // namespace tradelab::desktop::game
