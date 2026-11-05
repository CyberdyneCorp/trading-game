// Option chain and option ticket (web OptionChain.svelte, OptionTicket.svelte): the chain of the
// current decision step by expiry, and a ticket with premium, breakeven, max loss, the engine's fee,
// write margin, Greeks, the payoff at expiry, and the refusals the engine would give (no bid, settling
// series, short writes not allowed, not enough balance).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model/game_state.hpp"
#include "viewmodel/game/options_math.hpp"
#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::game {

struct ChainCell {
  int series = 0;
  std::string bid, ask, iv, delta;
  bool selected = false;
  bool settling = false;
};

struct ChainRow {
  std::string strike;
  bool itm_call = false;
  bool held = false;
  std::optional<ChainCell> call;
  std::optional<ChainCell> put;
};

struct ChainExpiry {
  int expiry_step = 0;
  std::string title;
  std::vector<ChainRow> rows;
};

std::vector<ChainExpiry> option_chain(const Texts& t, const GameState& g, std::optional<int> selected);

struct OptionDraft {
  int series = 0;
  std::string label;
  bool buy = true;
  bool limit = false;
  std::string qty;
  std::string premium;  // "" for market
  bool opens_exposure = false;
  std::optional<double> risk;  // max loss of a buy, initial margin of a write (plan reference)
};

struct OptionTicketView {
  bool has_quote = false;
  std::string series;
  std::string premium, breakeven, fee, greeks;
  std::optional<std::string> max_loss, margin;
  std::string short_locked, unbounded, no_bid, settling, cant_afford;
  std::string limit_placeholder;
  bool can_submit = false;
  // Payoff at expiry (premium included, fees excluded) over [lo, hi].
  std::vector<PayoffPoint> curve;
  double lo = 0, hi = 0, mark = 0, breakeven_price = 0;
};

class OptionTicketViewModel {
 public:
  std::optional<int> series;
  bool buy = true;
  bool limit = false;
  std::string qty = "1";
  std::string limit_price;

  void pick(const OptionQuote& q, bool buy_side);
  [[nodiscard]] const OptionQuote* quote(const GameState& g) const;
  [[nodiscard]] OptionTicketView view(const Texts& t, const GameState& g, bool busy) const;
  [[nodiscard]] std::optional<OptionDraft> draft(const Texts& t, const GameState& g, bool busy) const;

 private:
  struct Calc;
  [[nodiscard]] std::optional<Calc> calc(const GameState& g) const;
};

}  // namespace tradelab::desktop::game
