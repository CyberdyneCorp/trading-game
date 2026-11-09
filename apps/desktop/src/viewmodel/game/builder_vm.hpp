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
#include "viewmodel/game/order_rules.hpp"
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
  // add-practice-and-advanced-strategies D2, D3: the time in force of the limit legs, and the roll or
  // adjustment number the legs are tagged with ("" for an opening).
  std::optional<ComboTif> tif;
  std::string roll;
  std::string adjust;
};

// The time in force selector (web TifField.svelte): its items, which the order type can take, the
// choice, the N-bars field and the hint naming the bars the order can execute on.
struct TifView {
  std::string label;
  std::vector<std::string> items;
  std::vector<std::string> blocked;  // per item: the reason it cannot go with the type, "" when it can
  int index = 0;
  bool bars = false;  // the N-bars field is shown
  std::string bars_label;
  std::string hint;
  bool error = false;  // the hint is an error (N out of range)
};

// The selector's view for an order type ("market", "limit", ...) at `step` with `left` bars to go.
TifView tif_view(const Texts& t, TifChoice choice, const std::string& bars_input, std::string_view type, int step_seconds, int step,
                 int left);

struct TemplateButton {
  Template id = Template::BullCall;
  std::string label;
  bool enabled = true;
  bool selected = false;
};

struct BuilderLegRow {
  int kind = 0;    // 0 call, 1 put, 2 perpetual (the kind box's items)
  int side = 0;    // 0 buy, 1 sell
  int strike = -1;  // index in the row's strikes, -1 for the perpetual
  std::string qty;
  bool no_bid = false;  // a market sell with no bid now: it tends to be cancelled at the open
  std::vector<std::string> strikes;  // the strikes listed on the leg's expiry
  int expiry = 0;                    // index in BuilderView::leg_expiries (D3)
  bool limit = false;                // D2: a limit leg
  std::string limit_text;            // its premium as typed
};

struct BuilderView {
  bool available = false;  // options on and at least one expiry takes orders
  std::string title;
  std::vector<std::string> expiries;  // "barra 100 (40 barras)"
  int expiry_index = 0;
  // D3: the later expiries (a calendar's or diagonal's far one; empty: one expiry only) and the items of
  // a leg's expiry box (the builder's expiry, then the later ones).
  std::string far_label;
  std::vector<std::string> far_expiries;
  int far_index = 0;
  std::vector<std::string> leg_expiries;
  std::vector<std::string> leg_types;  // market | limit
  bool has_limit = false;              // D2: a limit leg: the combo's time in force is shown
  std::string limit_note, tif_note;
  TifView tif;
  std::string horizon;       // "At the first expiry (bar 150), ..." when the legs span two expiries
  std::string horizon_note;
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
  // Sizing aid (web StrategyBuilder.svelte): contracts whose maximum loss is a share of equity; on a
  // held long, a note that the size follows the long and its risk runs to the long's stop.
  std::string size_held;     // the note, "" unless "use my long" covers a held long
  bool sizing = false;       // the risk field and its button are shown
  std::string size_label;    // "Risco (% patrimônio)"
  std::string size_apply;    // "Usar 88,4 contratos"
  bool can_size = false;
  std::string size_hint;     // "Com 1 contratos a perda máxima no vencimento é ≈ 99,89 cr" ("" while re-analysing)
  std::string chart_summary;  // the payoff chart's accessible description
  std::string chart_legend;   // its price range and line styles, as in the lab
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
  std::string risk_pct = "1";  // the sizing aid's risk, % of equity, as typed
  std::vector<BuilderLeg> legs;
  std::string strategy = "custom";
  bool use_long = false;
  int far_expiry = 0;                      // D3: the later expiry of a calendar or diagonal
  TifChoice tif_choice = TifChoice::Ioc;   // D2: one time in force for the limit legs, IOC by default
  std::string tif_bars = "3";

  // Keeps the expiry on one that takes orders now.
  void sync(const GameState& g);
  void set_expiry_index(const GameState& g, int index);
  void set_far_index(const GameState& g, int index);
  // D3: a leg's expiry, by index in [the builder's expiry, the later ones]; its limit is dropped.
  void set_leg_expiry(const GameState& g, std::size_t i, int index);
  // D2: a leg switched to limit starts at its quote's mid rounded toward the passive side.
  void set_leg_limit(const GameState& g, std::size_t i, bool on);
  void set_leg_limit_text(std::size_t i, const std::string& text);
  [[nodiscard]] std::vector<int> later(const GameState& g) const;
  [[nodiscard]] bool has_limit() const;
  // The combo's time in force when it has a limit leg (nullopt otherwise or with N out of range).
  [[nodiscard]] std::optional<ComboTif> tif(const GameState& g) const;
  [[nodiscard]] bool tif_error(const GameState& g) const;
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
  // The sizing aid: contracts per leg whose maximum loss at expiry is `risk_pct` % of equity (nullopt when
  // the legs differ in size, "use my long" covers a held long, or there is no bounded maximum loss).
  [[nodiscard]] std::optional<double> sized(const GameState& g, Locale L) const;
  // Every leg (and the quantity field) takes the sized contracts; false when there is no size.
  bool apply_size(const GameState& g, Locale L);

  [[nodiscard]] double contracts(Locale L) const;
  [[nodiscard]] std::optional<HeldLong> covered(const GameState& g) const;  // the held long "use my long" covers
  [[nodiscard]] std::vector<BuilderLeg> sent(const GameState& g) const;
  [[nodiscard]] std::optional<std::vector<OrderLeg>> orders(const GameState& g) const;
  // "empty" | "too_many" | "two_perps" | "expiries" | "qty" | "unlisted" | "write_locked" | "".
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
  void view_sizing(const Texts& t, const GameState& g, BuilderView& v) const;
  void view_expiries(const Texts& t, const GameState& g, BuilderView& v) const;
  [[nodiscard]] BuilderLegRow leg_row(const GameState& g, const BuilderLeg& l, Locale L) const;
  [[nodiscard]] std::vector<int> leg_expiry_steps(const GameState& g) const;
  [[nodiscard]] bool same_qty(Locale L) const;
  [[nodiscard]] std::optional<double> max_loss_now() const;

  std::optional<Analysis<WhatIfResult>> whatif_;
  std::optional<Analysis<ComboPreview>> preview_;
  int pending_ = 0;
};

// "+1 C 105,00" / "−2,5 perpétuo": a combo leg as the builder and the incomplete notice name it.
std::string order_leg_name(const Texts& t, const GameState& g, const OrderLeg& o);

}  // namespace tradelab::desktop::game
