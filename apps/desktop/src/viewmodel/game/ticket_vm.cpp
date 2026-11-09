#include "viewmodel/game/ticket_vm.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr const char* kDash = "\xE2\x80\x94";  // —
constexpr const char* kWarn = "\xE2\x9A\xA0 ";  // ⚠

std::optional<double> typed_level(const std::string& text) {
  if (text.empty()) return std::nullopt;
  return fmt::parse_decimal(text);
}

double or_zero(double v) { return std::isfinite(v) ? v : 0; }

std::optional<double> last_atr(const GameState& g) {
  const Series& atr = g.indicators.atr14;
  if (atr.empty() || !atr.back()) return std::nullopt;
  return atr.back();
}

// The leverage field as the engine gets it: whole, at least 1, at most the cap (empty or invalid: 1).
int clamp_leverage(const std::string& text, int cap) {
  const double typed = fmt::parse_decimal(text);
  const long rounded = std::isfinite(typed) && typed != 0 ? std::lround(typed) : 1;
  return static_cast<int>(std::min<long>(std::max<long>(1, rounded), cap));
}

std::string price_text(double v, Locale L) { return fmt::fmt_num(v, L, 2); }

std::string opt_price(const std::optional<double>& v, Locale L) { return v ? price_text(*v, L) : std::string(kDash); }

std::vector<LevelChip> stop_chips(const Texts& t, const GameState& g, Side side, double from,
                                  const std::optional<double>& atr) {
  const double tick = g.contract.tick;
  return {{"1\xC3\x97" "ATR", t.tr("level.atrTitle", {{"n", "1"}}), stop_from_atr(side, from, atr, 1)},
          {"2\xC3\x97" "ATR", t.tr("level.atrTitle", {{"n", "2"}}), stop_from_atr(side, from, atr, 2)},
          {"Swing", t.tr("level.swingTitle"), swing_stop(side, from, g.bars, tick)},
          {"1%", t.tr("level.pctTitle", {{"n", "1"}}), stop_from_pct(side, from, 1)},
          {"2%", t.tr("level.pctTitle", {{"n", "2"}}), stop_from_pct(side, from, 2)}};
}

std::vector<LevelChip> target_chips(const Texts& t, Side side, double from, std::optional<double> stop) {
  std::vector<LevelChip> out;
  for (int r = 1; r <= 3; ++r) {
    out.push_back({std::to_string(r) + "R", t.tr("level.rTitle", {{"n", std::to_string(r)}}), target_from_r(side, from, stop, r)});
  }
  return out;
}

std::optional<std::string> stop_hint(Locale L, double from, std::optional<double> level, std::optional<double> atr) {
  const auto d = distance_of(from, level, atr);
  if (!d) return std::nullopt;
  std::string out = price_text(d->abs, L) + " \xC2\xB7 " + fmt::fmt_num(d->pct, L) + "%";
  if (d->atr) out += " \xC2\xB7 " + fmt::fmt_num(*d->atr, L, 1) + "\xC3\x97" "ATR";
  return out;
}

std::optional<std::string> target_hint(Locale L, Side side, double from, std::optional<double> stop,
                                       std::optional<double> level, std::optional<double> atr) {
  auto base = stop_hint(L, from, level, atr);
  const auto rr = reward_risk(side, from, stop, level);
  if (base && rr) *base += " \xC2\xB7 R:R 1:" + fmt::fmt_num(*rr, L, 1);
  return base;
}

std::string_view opposing_key(Opposing k) {
  if (k == Opposing::Reduce) return "ticket.opposing.reduce";
  if (k == Opposing::Close) return "ticket.opposing.close";
  return "ticket.opposing.reverse";
}

std::string_view amount_key(SizeBy by) {
  switch (by) {
    case SizeBy::Risk: return "ticket.riskPct";
    case SizeBy::Notional: return "ticket.notional";
    case SizeBy::Equity: return "ticket.equityPct";
    case SizeBy::Qty: break;
  }
  return "ticket.qty";
}

OrderType order_type_of(TicketType t) {
  if (t == TicketType::Limit) return OrderType::Limit;
  if (t == TicketType::StopMarket) return OrderType::StopMarket;
  return OrderType::Market;
}

}  // namespace

struct TicketViewModel::Calc {
  Costs k;
  double tick = 0.01, lot = 0.1, mark = 0, last = 0;  // liquidation at the mark, orders at the last price
  int cap = 1;
  double pos_qty = 0;
  std::optional<double> atr;
  double entry = 0;
  std::optional<double> stop, tp;
  double qty = 0;
  std::string qty_str;
  int lev = 1;
  TicketEstimate est;
  bool short_blocked = false, increases = false;
  std::optional<OpposingEffect> opposing;
  double added_qty = 0, need_margin = 0, notional_after = 0;
  bool cant_afford = false, above_tier = false;
  Tier tier;
  bool stop_wrong = false, tp_wrong = false, needs_price = false, needs_beyond_confirm = false;
  double typed_price = 0;
};

TicketViewModel::Calc TicketViewModel::calc(const GameState& g, Locale locale) const {
  Calc c;
  const Contract& k = g.contract;
  c.k = costs_of(k, g.liquidity);
  c.tick = k.tick;
  c.lot = k.lot;
  c.mark = g.account.mark;
  c.last = g.last_price();
  c.cap = k.leverage_cap;
  c.pos_qty = g.position.qty;
  c.atr = last_atr(g);
  c.typed_price = fmt::parse_decimal(price);
  c.needs_price = type != TicketType::Market;
  const double typed_entry = or_zero(c.typed_price);
  const double limit_entry = typed_entry != 0 ? typed_entry : c.last;
  c.entry = type == TicketType::Market ? estimated_market_entry(side, c.last, c.k) : limit_entry;
  c.stop = typed_level(stop);
  c.tp = typed_level(take_profit);
  // Typed sizing amounts; the notional is in credits, so "1.000" in PT-BR is one thousand.
  const double typed = size_by == SizeBy::Notional ? fmt::parse_amount(notional, locale) : fmt::parse_decimal(this->amount());
  c.qty = sized_qty(size_by, {g.account.equity, c.entry, c.stop, or_zero(fmt::parse_decimal(qty)), or_zero(typed)}, c.k,
                    c.lot);
  c.qty_str = fmt::qty_to_lot(c.qty, c.lot);
  c.lev = clamp_leverage(leverage, c.cap);
  exposure(g, c);
  return c;
}

// What the order does to the account: estimate, added exposure, margin, tier and every refusal check.
void TicketViewModel::exposure(const GameState& g, Calc& c) const {
  const Contract& k = g.contract;
  const OpenPosition open{c.pos_qty, g.position.entry, g.position.margin, g.leverage};
  c.est = estimate({side, c.qty, c.entry, c.lev, c.stop}, c.k, open);
  const bool same_side = c.pos_qty > 0 == is_buy(side);
  c.short_blocked = !k.allow_short && !is_buy(side) && c.pos_qty <= 0;
  c.increases = c.pos_qty == 0 || same_side || c.qty > std::fabs(c.pos_qty);
  c.opposing = opposing_effect(c.pos_qty, side, c.qty, c.lot);
  c.added_qty = c.pos_qty == 0 || same_side ? c.qty : std::max(0.0, c.qty - std::fabs(c.pos_qty));
  c.need_margin = required_margin(c.added_qty, c.entry, c.lev, c.k);
  c.cant_afford = c.added_qty > 0 && c.need_margin > g.account.available;
  const double held_notional = c.pos_qty != 0 && same_side ? std::fabs(c.pos_qty) * g.position.entry * c.k.multiplier : 0;
  c.notional_after = held_notional + c.added_qty * c.entry * c.k.multiplier;
  c.tier = margin_tier(c.k, c.notional_after);
  c.above_tier = c.added_qty > 0 && c.lev > c.tier.max_leverage;
  c.stop_wrong = c.stop && !stop_on_losing_side(side, c.entry, *c.stop);
  c.tp_wrong = c.tp && !target_on_winning_side(side, c.entry, *c.tp);
  c.needs_beyond_confirm = c.est.stop_beyond_liquidation && g.rules.confirm_stop_beyond_liq;
}

std::string& TicketViewModel::amount() {
  return const_cast<std::string&>(static_cast<const TicketViewModel&>(*this).amount());
}

const std::string& TicketViewModel::amount() const {
  switch (size_by) {
    case SizeBy::Risk: return risk;
    case SizeBy::Notional: return notional;
    case SizeBy::Equity: return equity;
    case SizeBy::Qty: break;
  }
  return qty;
}

void TicketViewModel::sync(const GameState& g) {
  if (!synced_ || g.leverage != lev_seen_ || g.contract.leverage_cap != cap_seen_) {
    lev_seen_ = g.leverage;
    cap_seen_ = g.contract.leverage_cap;
    leverage = std::to_string(std::min(std::max(1, g.leverage), g.contract.leverage_cap));
  }
  if (synced_ && g.totals.fills != fills_seen_) {
    stop.clear();
    take_profit.clear();
  }
  fills_seen_ = g.totals.fills;
  synced_ = true;
}

std::string TicketViewModel::beyond_key() const {
  return std::to_string(static_cast<int>(side)) + "|" + qty + "|" + risk + "|" + notional + "|" + equity + "|" +
         std::to_string(static_cast<int>(size_by)) + "|" + leverage + "|" + stop + "|" +
         std::to_string(static_cast<int>(type)) + "|" + price;
}

void TicketViewModel::confirm_beyond(const GameState& /*g*/, bool on) {
  beyond_confirmed_ = on;
  beyond_for_ = beyond_key();
}

bool TicketViewModel::beyond_confirmed(const GameState& /*g*/) const {
  return beyond_confirmed_ && beyond_for_ == beyond_key();
}

void TicketViewModel::confirm_prot_beyond(bool on) {
  prot_confirmed_ = on;
  prot_confirmed_for_ = prot_stop;
}

TicketView TicketViewModel::view(const Texts& t, const GameState& g, bool busy) const {
  const Locale locale = t.locale();
  const Calc c = calc(g, locale);
  const Locale L = t.locale();
  TicketView v;
  v.leverage_label = t.tr("ticket.leverage") + " (\xE2\x89\xA4 " + std::to_string(c.cap) + "x)";  // ≤
  v.needs_price = c.needs_price;
  v.price_label = t.tr(type == TicketType::Limit ? "ticket.price" : "ticket.trigger");
  v.price_placeholder = price_text(c.last, L);
  v.amount_label = t.tr(amount_key(size_by));
  v.est_qty = fmt::fmt_qty(c.qty_str, L);
  v.est_notional = fmt::fmt_money(c.est.notional, L);
  v.est_entry = price_text(c.entry, L);
  v.est_margin = fmt::fmt_money(c.est.margin, L);
  v.est_fee = fmt::fmt_money(c.est.fee, L);
  v.est_liq = opt_price(c.est.liquidation, L);
  v.est_risk = c.est.planned_risk ? fmt::fmt_money(*c.est.planned_risk, L) : std::string(kDash);
  v.stop_chips = stop_chips(t, g, side, c.entry, c.atr);
  v.tp_chips = target_chips(t, side, c.entry, c.stop);
  v.stop_hint = stop_hint(L, c.entry, c.stop, c.atr);
  v.tp_hint = target_hint(L, side, c.entry, c.stop, c.tp, c.atr);
  const std::string at = price_text(c.entry, L);
  if (c.stop_wrong) v.stop_warn = t.tr("level.stopSide", {{"side", is_buy(side) ? "<" : ">"}, {"price", at}});
  if (c.tp_wrong) v.tp_warn = t.tr("level.tpSide", {{"side", is_buy(side) ? ">" : "<"}, {"price", at}});
  notes(t, g, c, v);
  const bool confirmed = beyond_confirmed(g);
  v.needs_beyond_confirm = c.needs_beyond_confirm;
  v.can_submit = !busy && c.qty > 0 && !c.short_blocked && !c.cant_afford && !c.above_tier && !c.stop_wrong &&
                 !c.tp_wrong && (!c.needs_beyond_confirm || confirmed) && (!c.needs_price || c.typed_price > 0) && tif_now(g).ok();
  order_rules(t, g, c, v);
  v.submit_label = std::string(is_buy(side) ? "\xE2\x96\xB2 " : "\xE2\x96\xBC ") + t.tr("ticket.submit") + "  [Enter]";
  if (c.qty > 0) v.preview = TicketPreview{c.entry, c.stop, c.tp, c.est.liquidation};
  protection(t, g, c, busy, v);
  return v;
}

// Sending at another leverage re-margins the open position too (engine set_leverage).
void TicketViewModel::relever_note(const Texts& t, const GameState& g, const Calc& c, TicketView& v) const {
  if (c.pos_qty == 0 || c.lev == g.leverage) return;
  const Locale L = t.locale();
  const auto to = relevered_liquidation(c.pos_qty > 0 ? Side::Buy : Side::Sell, c.pos_qty, g.position.entry, c.lev, c.k);
  const auto from = g.account.liquidation;
  v.relever_closer = to && (!from || std::fabs(c.mark - *to) < std::fabs(c.mark - *from));
  v.relever = (v.relever_closer ? kWarn : "") + t.tr("ticket.relever", {{"from", std::to_string(g.leverage)},
                                                                        {"to", std::to_string(c.lev)},
                                                                        {"liqFrom", opt_price(from, L)},
                                                                        {"liqTo", opt_price(to, L)}});
}

void TicketViewModel::notes(const Texts& t, const GameState& g, const Calc& c, TicketView& v) const {
  const Locale L = t.locale();
  if (c.short_blocked) v.short_locked = t.tr("ticket.shortLocked");
  if (size_by == SizeBy::Risk && !c.stop) v.risk_needs_stop = "\xE2\x86\xB3 " + t.tr("ticket.riskNeedsStop");  // ↳
  if (g.liquidity) {
    const ShockFactors f = shock_factors(g.contract, *g.liquidity);
    v.shock = kWarn + t.tr("ticket.shock", {{"cost", fmt::fmt_num(f.cost, L, 1)}, {"capacity", fmt::fmt_pct(f.capacity, L, 0)}});
  }
  relever_note(t, g, c, v);
  if (c.opposing) {
    v.opposing = kWarn + t.tr(opposing_key(c.opposing->kind), {{"open", fmt::fmt_num(c.opposing->open, L, 1)},
                                                               {"after", fmt::fmt_num(c.opposing->after, L, 1)}});
  }
  if (c.cant_afford) {
    v.cant_afford = kWarn + t.tr("ticket.cantAfford", {{"need", fmt::fmt_money(c.need_margin, L)},
                                                       {"avail", fmt::fmt_money(g.account.available, L)}});
  }
  if (c.above_tier) {
    v.above_tier = kWarn + t.tr("ticket.aboveTier", {{"notional", fmt::fmt_money(c.notional_after, L)},
                                                     {"max", std::to_string(static_cast<long>(c.tier.max_leverage))},
                                                     {"mmr", fmt::fmt_pct(c.tier.mmr, L, 1)}});
  }
  if (c.est.stop_beyond_liquidation) v.beyond_liq = kWarn + t.tr("ticket.stopBeyondLiq");
  if (!c.stop && c.qty > 0) v.no_stop = t.tr("ticket.noStop");
}

// Trailing presets (ATR multiples) and, by percent, the first trigger on the tick.
void TicketViewModel::trailing(const Texts& t, const GameState& g, const Calc& c, TicketView& v) const {
  for (int m = 1; m <= 3; ++m) {
    v.trail_chips.push_back({std::to_string(m) + "\xC3\x97" "ATR", t.tr("level.trailTitle", {{"n", std::to_string(m)}}),
                             c.atr ? std::optional<double>(m * *c.atr) : std::nullopt});
  }
  const Side exit = g.position.qty > 0 ? Side::Sell : Side::Buy;
  const auto trigger = prot_trail_pct.empty()
                           ? std::nullopt
                           : trail_trigger_from_percent(exit, c.last, fmt::parse_decimal(prot_trail_pct), c.tick);
  v.trail_hint = trigger ? t.tr("ticket.trailPctHint", {{"price", price_text(*trigger, t.locale())}}) : t.tr("ticket.trailPctNote");
}

void TicketViewModel::protection(const Texts& t, const GameState& g, const Calc& c, bool busy, TicketView& v) const {
  if (c.pos_qty == 0) return;
  const Locale L = t.locale();
  v.protect_visible = true;
  const bool has_stop = std::any_of(g.orders.begin(), g.orders.end(), [](const Order& o) {
    return o.reduce_only && (o.type == "stop_market" || o.type == "trailing_stop");
  });
  if (!has_stop) v.unprotected = kWarn + t.tr("ticket.unprotected");
  const Side pos_side = c.pos_qty > 0 ? Side::Buy : Side::Sell;
  const auto pstop = typed_level(prot_stop);
  const auto ptp = typed_level(prot_tp);
  v.prot_stop_chips = stop_chips(t, g, pos_side, c.last, c.atr);
  v.prot_tp_chips = target_chips(t, pos_side, g.position.entry, pstop);
  v.prot_stop_hint = stop_hint(L, g.position.entry, pstop, c.atr);
  v.prot_tp_hint = target_hint(L, pos_side, g.position.entry, pstop, ptp, c.atr);
  const std::string at = price_text(c.last, L);
  if (pstop && !stop_on_losing_side(pos_side, c.last, *pstop)) v.prot_stop_warn = t.tr("level.protStopSide", {{"price", at}});
  if (ptp && !target_on_winning_side(pos_side, c.last, *ptp)) v.prot_tp_warn = t.tr("level.protTpSide", {{"price", at}});
  const auto liq = g.account.liquidation;
  v.prot_beyond = pstop && liq && (c.pos_qty > 0 ? *pstop <= *liq : *pstop >= *liq) && g.rules.confirm_stop_beyond_liq;
  trailing(t, g, c, v);
  const bool trail_set = trail_by_pct ? trail_rate_from_percent(fmt::parse_decimal(prot_trail_pct)).has_value()
                                      : !prot_trail.empty();
  v.can_protect = !busy && !v.prot_stop_warn && !v.prot_tp_warn && (!v.prot_beyond || prot_beyond_confirmed()) &&
                  (pstop || ptp || trail_set);
}

std::string TicketViewModel::type_code() const {
  if (type == TicketType::Limit) return "limit";
  return type == TicketType::StopMarket ? "stop_market" : "market";
}

// A choice the order type cannot take falls back to GTC (the selector shows the block first).
TifOrder TicketViewModel::tif_now(const GameState& g) const {
  const TifChoice c = tif_block(tif_choice, type_code()).empty() ? tif_choice : TifChoice::Gtc;
  return tif_order(c, fmt::parse_decimal(tif_bars), g.step_seconds, bars_left(g.total_bars, g.step));
}

std::optional<PlannedTrail> TicketViewModel::planned_trail() const {
  if (!protect_with_trail) return std::nullopt;
  const std::string& value = plan_trail_by_pct ? plan_trail_pct : plan_trail;
  if (value.empty()) return std::nullopt;
  return PlannedTrail{plan_trail_by_pct, value};
}

void TicketViewModel::fill_planned_trail(const PlannedTrail& p) {
  trail_by_pct = p.pct;
  (p.pct ? prot_trail_pct : prot_trail) = p.value;
  protect_tab = ProtectTab::Trail;
}

// Time in force, "Protect with" and the protective tabs (D6, D10).
void TicketViewModel::order_rules(const Texts& t, const GameState& g, const Calc& c, TicketView& v) const {
  v.tif = tif_view(t, tif_choice, tif_bars, type_code(), g.step_seconds, g.step, bars_left(g.total_bars, g.step));
  v.protect_with = t.tr("ticket.protectWith");
  v.protect_fixed = t.tr("ticket.protectFixed");
  v.protect_trail = t.tr("ticket.protectTrail");
  if (protect_with_trail) v.trail_planned = t.tr("ticket.protectTrail") + " " + t.tr("ticket.trailPlanned");
  const bool trail_set = trail_by_pct ? !prot_trail_pct.empty() : !prot_trail.empty();
  const std::array<std::pair<const char*, bool>, 3> tabs = {
      {{"ticket.tab.stop", !prot_stop.empty()}, {"ticket.tab.trail", trail_set}, {"ticket.tab.target", !prot_tp.empty()}}};
  for (const auto& [key, filled] : tabs) v.tabs.push_back(t.tr(key) + (filled ? " \xE2\x80\xA2" : ""));
  if (c.pos_qty == 0) return;
  const bool exit_sell = c.pos_qty > 0;
  const std::string close = fmt::price_to_tick(c.last, c.tick);
  const std::string tick = fmt::to_fixed_text(c.tick, 8);
  std::string trigger;
  if (trail_by_pct) {
    if (const auto rate = trail_rate_from_percent(fmt::parse_decimal(prot_trail_pct))) trigger = trail_trigger(exit_sell, close, {}, *rate, tick);
  } else if (!prot_trail.empty()) {
    const std::string dist = fmt::price_to_tick(fmt::parse_decimal(prot_trail), c.tick);
    if (!dist.empty()) trigger = trail_trigger(exit_sell, close, dist, {}, tick);
  }
  v.trail_rule = trigger.empty() ? t.tr("ticket.trailExplain") : t.tr("ticket.trailRule", {{"price", fmt::fmt_price(trigger, t.locale())}});
}

std::optional<OrderDraft> TicketViewModel::draft(const GameState& g, bool busy, Locale locale) const {
  // view() checks everything the web's canSubmit does; the draft is only built when it passes.
  const Calc c = calc(g, locale);
  const bool confirmed = beyond_confirmed(g);
  const TifOrder tif = tif_now(g);
  const bool ok = !busy && c.qty > 0 && !c.short_blocked && !c.cant_afford && !c.above_tier && !c.stop_wrong &&
                  !c.tp_wrong && (!c.needs_beyond_confirm || confirmed) && (!c.needs_price || c.typed_price > 0) && tif.ok();
  if (!ok) return std::nullopt;
  OrderDraft d;
  d.side = side;
  d.type = order_type_of(type);
  d.qty = c.qty_str;
  if (c.needs_price) d.price = fmt::price_to_tick(c.typed_price, c.tick);
  d.leverage = c.lev;
  if (c.stop) d.stop = fmt::price_to_tick(*c.stop, c.tick);
  if (c.tp) d.take_profit = fmt::price_to_tick(*c.tp, c.tick);
  d.estimate = c.est;
  d.increases = c.increases;
  d.required = c.need_margin;
  d.tif = tif.tif == "gtc" ? std::string() : tif.tif;
  d.tif_bars = tif.bars;
  d.planned_trail = planned_trail();
  if (d.planned_trail) d.stop.clear();  // a trailing stop is planned instead of the fixed bracket stop
  return d;
}

std::vector<GameCommand> TicketViewModel::protect_commands(const GameState& g) const {
  std::vector<GameCommand> out;
  if (g.position.qty == 0) return out;
  const bool exit_buy = g.position.qty < 0;
  std::string size = g.position.qty_text;
  if (!size.empty() && size[0] == '-') size.erase(0, 1);
  const double tick = g.contract.tick;
  if (!prot_stop.empty()) {
    out.push_back(GameCommand::order(OrderType::StopMarket, exit_buy, size, fmt::price_to_tick(fmt::parse_decimal(prot_stop), tick), true));
  }
  if (!prot_tp.empty()) {
    out.push_back(GameCommand::order(OrderType::TakeProfit, exit_buy, size, fmt::price_to_tick(fmt::parse_decimal(prot_tp), tick), true));
  }
  if (trail_by_pct) {
    if (const auto rate = trail_rate_from_percent(fmt::parse_decimal(prot_trail_pct))) {
      out.push_back(GameCommand::trailing_rate(exit_buy, size, *rate));
    }
  } else if (!prot_trail.empty()) {
    out.push_back(GameCommand::order(OrderType::TrailingStop, exit_buy, size, {}, true, {}, {},
                                     fmt::price_to_tick(fmt::parse_decimal(prot_trail), tick)));
  }
  return out;
}

void TicketViewModel::clear_protection() {
  prot_stop.clear();
  prot_tp.clear();
  prot_trail.clear();
  prot_trail_pct.clear();
}

void TicketViewModel::apply_advice(const GameState& g, const Advice& a, Locale locale) {
  const Contract& k = g.contract;
  side = a.stance == "short" ? Side::Sell : Side::Buy;
  stop = fmt::price_input_text(fmt::num(a.stop), k.tick, locale);
  size_by = SizeBy::Qty;
  // The read is sized for a market entry at the last price: a stale limit price would change its risk.
  type = TicketType::Market;
  const Costs costs = costs_of(k, g.liquidity);
  const double entry = estimated_market_entry(side, g.last_price(), costs);
  const int typed = clamp_leverage(leverage, k.leverage_cap);
  const Fit fit = fit_to_margin(fmt::num(a.size), entry, g.account.available, typed, k.leverage_cap, costs, k.lot);
  qty = fmt::qty_input_text(fit.qty, k.lot, locale);
  leverage = std::to_string(fit.leverage);
}

void TicketViewModel::set_level(bool stop_level, double value, double tick, Locale locale) {
  (stop_level ? stop : take_profit) = fmt::price_input_text(value, tick, locale);
}

void TicketViewModel::set_prot_level(bool stop_level, double value, double tick, Locale locale) {
  (stop_level ? prot_stop : prot_tp) = fmt::price_input_text(value, tick, locale);
}

void TicketViewModel::set_trail_pct(int pct, Locale /*locale*/) { prot_trail_pct = std::to_string(pct); }

}  // namespace tradelab::desktop::game
