// Perpetual order ticket (web OrderTicket.svelte + LevelField/TrailField): side, market/limit/stop,
// leverage, the four sizing modes (risk % with a stop, quantity, notional, % of equity), stop/target with
// presets and hints, the margin/fee/liquidation estimate with margin tiers and liquidity shocks, every
// warning the web shows, the stop-beyond-liquidation confirmation, and the protection of an open
// position (stop, target, trailing by price or percent). Fields hold what the player typed (decimal
// comma or point); amounts sent to the engine are snapped to the tick and lot here.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_command.hpp"
#include "model/game_state.hpp"
#include "viewmodel/game/chart_vm.hpp"
#include "viewmodel/game/sizing.hpp"
#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::game {

enum class TicketType : unsigned char { Market, Limit, StopMarket };

struct LevelChip {
  std::string label;
  std::string title;
  std::optional<double> value;  // nullopt: unavailable (e.g. ATR still warming up)
};

struct OrderDraft {
  Side side = Side::Buy;
  OrderType type = OrderType::Market;
  std::string qty;
  std::string price;  // "" for market
  int leverage = 1;
  std::string stop;
  std::string take_profit;
  TicketEstimate estimate;
  bool increases = false;
  double required = 0;  // margin + fee of the part that adds exposure (quoted when refused)
};

struct TicketView {
  // Labels
  std::string leverage_label, price_label, amount_label;
  bool needs_price = false;
  std::string price_placeholder;
  // Estimate block
  std::string est_qty, est_notional, est_entry, est_margin, est_fee, est_liq, est_risk;
  // Levels
  std::vector<LevelChip> stop_chips, tp_chips;
  std::optional<std::string> stop_hint, stop_warn, tp_hint, tp_warn;
  // Notes and warnings, in the web's order (empty = not shown)
  std::string short_locked, risk_needs_stop, shock, relever, opposing, cant_afford, above_tier, beyond_liq, no_stop;
  bool relever_closer = false;
  bool needs_beyond_confirm = false;
  bool can_submit = false;
  std::string submit_label;
  std::optional<TicketPreview> preview;
  // Protection of the open position
  bool protect_visible = false;
  std::string unprotected;
  std::vector<LevelChip> prot_stop_chips, prot_tp_chips, trail_chips;
  std::optional<std::string> prot_stop_hint, prot_stop_warn, prot_tp_hint, prot_tp_warn;
  std::string trail_hint;
  bool prot_beyond = false;
  bool can_protect = false;
};

class TicketViewModel {
 public:
  Side side = Side::Buy;
  TicketType type = TicketType::Market;
  std::string leverage = "1";
  std::string price;
  SizeBy size_by = SizeBy::Risk;
  std::string qty = "10";
  std::string risk = "1";
  std::string notional = "1000";
  std::string equity = "10";
  std::string stop;
  std::string take_profit;
  // Protection
  std::string prot_stop;
  std::string prot_tp;
  std::string prot_trail;
  std::string prot_trail_pct;
  bool trail_by_pct = false;

  // The amount field of the sizing mode (quantity, risk %, notional or % of equity).
  [[nodiscard]] std::string& amount();
  [[nodiscard]] const std::string& amount() const;

  // The engine state changed: the leverage field follows the account's only when it (or the cap)
  // changes, and stop/target are cleared after a fill (they belonged to that order).
  void sync(const GameState& g);

  [[nodiscard]] TicketView view(const Texts& t, const GameState& g, bool busy) const;
  // The order to send, when the ticket can submit.
  [[nodiscard]] std::optional<OrderDraft> draft(const GameState& g, bool busy, Locale locale) const;
  // Protective orders for the open position (stop, target, trailing), in that order.
  [[nodiscard]] std::vector<GameCommand> protect_commands(const GameState& g) const;
  void clear_protection();

  // The stop beyond liquidation confirmation belongs to the exact order it was given for.
  void confirm_beyond(const GameState& g, bool on);
  [[nodiscard]] bool beyond_confirmed(const GameState& g) const;
  void confirm_prot_beyond(bool on);
  [[nodiscard]] bool prot_beyond_confirmed() const { return prot_confirmed_ && prot_confirmed_for_ == prot_stop; }

  // Advisor "Apply": side, stop and size (fitted to the margin), market type; never submits.
  void apply_advice(const GameState& g, const Advice& a, Locale locale);
  // A level picked on the chart or dragged there, snapped to the tick and written in the locale.
  void set_level(bool stop_level, double price, double tick, Locale locale);
  void set_prot_level(bool stop_level, double price, double tick, Locale locale);
  void set_trail_pct(int pct, Locale locale);

 private:
  struct Calc;
  [[nodiscard]] Calc calc(const GameState& g, Locale locale) const;
  void exposure(const GameState& g, Calc& c) const;
  void notes(const Texts& t, const GameState& g, const Calc& c, TicketView& v) const;
  void relever_note(const Texts& t, const GameState& g, const Calc& c, TicketView& v) const;
  void trailing(const Texts& t, const GameState& g, const Calc& c, TicketView& v) const;
  void protection(const Texts& t, const GameState& g, const Calc& c, bool busy, TicketView& v) const;
  [[nodiscard]] std::string beyond_key() const;

  bool synced_ = false;
  int lev_seen_ = -1;
  int cap_seen_ = -1;
  int fills_seen_ = 0;
  bool beyond_confirmed_ = false;
  std::string beyond_for_;
  bool prot_confirmed_ = false;
  std::string prot_confirmed_for_;
};

}  // namespace tradelab::desktop::game
