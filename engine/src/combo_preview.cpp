// Combo dry run (design D4 of add-academy-and-strategies, tl_combo_preview): the builder's legs are
// applied in order, as market orders, to a copy of the run state with the real command functions.
// The copy is discarded; the run is never touched.

#include <algorithm>

#include "accounting.hpp"
#include "options_exec.hpp"
#include "tradelab/whatif.hpp"

namespace tradelab {
namespace {

Money plus(Money a, Money b) { return Money::from_raw(a.raw + b.raw); }

std::uint64_t next_client_id(const RunState& st) {
  std::uint64_t id = 0;
  for (const CommandRecord& r : st.command_log) id = std::max(id, r.client_id);
  return id + 1;
}

Command leg_command(const RunState& st, const ComboLeg& l, std::uint64_t client_id) {
  Command c;
  c.type = l.perp ? CommandType::PlaceOrder : CommandType::PlaceOptionOrder;
  c.seq = st.last_command_seq + 1;
  c.decision_step = st.step;
  c.client_id = client_id;
  c.order_type = OrderType::Market;
  c.side = l.side;
  c.qty = l.qty;
  c.series = l.series;
  return c;
}

// The estimate shown before placing: the quote crossed, the premium and the fee.
void estimate_option(const Scenario& sc, const RunState& st, const ComboLeg& l, ComboLegResult& r) {
  const OptionSeries* s = find_series(sc, l.series);
  const bool listed = s != nullptr && st.step >= s->list_step && st.step < s->expiry_step;
  if (!listed) return;
  const Price f = sc.bars[st.step].close;
  const OptionQuote q = quote(sc, *s, st.step, f);
  r.quote = l.side == OrderSide::Buy ? q.ask : q.bid;
  r.premium = detail::units_value(sc, l.qty, r.quote, Rounding::HalfEven);
  r.fee = detail::option_fee(sc, l.qty, r.quote, f);
  r.required = detail::option_order_requirement(sc, st, *s, l.side, l.qty);
}

void estimate_perp(const Scenario& sc, const RunState& st, const ComboLeg& l, ComboLegResult& r) {
  r.quote = sc.bars[st.step].close;
  r.fee = detail::fee_for(detail::notional(sc, l.qty, r.quote, Rounding::Ceil), sc.contract.taker_fee);
}

void fill_estimate(const Scenario& sc, RunState& st, const ComboLeg& l) {
  if (!l.perp) {
    detail::fill_option_at_quote(sc, st, *find_series(sc, l.series), l.side, l.qty);
    return;
  }
  detail::FillSpec f;
  f.side = l.side;
  f.qty = l.qty;
  f.price = sc.bars[st.step].close;
  f.requested = f.price;
  detail::apply_fill(sc, st, f);
}

void add_totals(ComboPreview& p, const ComboLeg& l, const ComboLegResult& r) {
  if (!r.accepted) return;
  const bool credit = !l.perp && l.side == OrderSide::Sell;
  p.net_premium = plus(p.net_premium, Money::from_raw(credit ? r.premium.raw : -r.premium.raw));
  p.fees = plus(p.fees, r.fee);
  p.required = plus(p.required, r.required);
}

}  // namespace

ComboPreview combo_preview(const Scenario& sc, const RunState& st, const std::vector<ComboLeg>& legs) {
  ComboPreview p;
  RunState placed = st;  // the dry run: placements on a copy
  RunState filled = st;  // the after-fill estimate: accepted legs traded at today's quotes
  std::uint64_t client_id = next_client_id(st);
  p.accepted = !legs.empty();
  for (const ComboLeg& l : legs) {
    ComboLegResult r;
    const Money before = account_view(sc, placed).available;
    if (l.perp) estimate_perp(sc, placed, l, r);
    else estimate_option(sc, placed, l, r);
    const CommandResult res = apply_command(sc, placed, leg_command(placed, l, client_id++));
    r.accepted = res.accepted;
    r.reason = res.reason;
    r.available_after = account_view(sc, placed).available;
    if (l.perp) r.required = Money::from_raw(before.raw - r.available_after.raw);
    if (r.accepted && placed.status == RunStatus::Active) fill_estimate(sc, filled, l);
    p.accepted = p.accepted && r.accepted;
    add_totals(p, l, r);
    p.legs.push_back(r);
  }
  p.available_after = account_view(sc, placed).available;
  const AccountView after = account_view(sc, filled);
  p.option_margin_after = after.option_initial_margin;
  p.position_margin_after = after.position_margin;
  p.available_after_fill = after.available;
  return p;
}

}  // namespace tradelab
