#include "viewmodel/game/options_vm.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr const char* kWarn = "\xE2\x9A\xA0 ";  // ⚠

ChainCell cell(const Texts& t, const OptionQuote& q, std::optional<int> selected) {
  const Locale L = t.locale();
  return {q.id,
          fmt::fmt_num(q.bid, L, 2),
          fmt::fmt_num(q.ask, L, 2),
          fmt::fmt_pct(q.iv, L, 0),
          fmt::fmt_num(q.delta, L),
          selected && *selected == q.id,
          q.settling};
}

const OptionQuote* find_quote(const GameState& g, int expiry, double strike, const char* kind) {
  for (const auto& q : g.options.chain) {
    if (q.expiry_step == expiry && q.strike == strike && q.kind == kind) return &q;
  }
  return nullptr;
}

bool held(const GameState& g, const OptionQuote* q) {
  if (q == nullptr) return false;
  return std::any_of(g.options.positions.begin(), g.options.positions.end(),
                     [&](const OptionPosition& p) { return p.series == q->id; });
}

ChainRow chain_row(const Texts& t, const GameState& g, int expiry, double strike, std::optional<int> selected) {
  const OptionQuote* call = find_quote(g, expiry, strike, "call");
  const OptionQuote* put = find_quote(g, expiry, strike, "put");
  ChainRow row{fmt::fmt_num(strike, t.locale(), 2), g.account.mark > strike, held(g, call) || held(g, put), std::nullopt,
               std::nullopt};
  if (call != nullptr) row.call = cell(t, *call, selected);
  if (put != nullptr) row.put = cell(t, *put, selected);
  return row;
}

}  // namespace

std::vector<ChainExpiry> option_chain(const Texts& t, const GameState& g, std::optional<int> selected) {
  std::set<int> expiries;
  for (const auto& q : g.options.chain) expiries.insert(q.expiry_step);
  std::vector<ChainExpiry> out;
  for (const int exp : expiries) {
    ChainExpiry e{exp, t.count("opt.expiry", exp - g.step, {{"step", std::to_string(exp)}}), {}};
    std::set<double> strikes;
    for (const auto& q : g.options.chain) {
      if (q.expiry_step == exp) strikes.insert(q.strike);
    }
    for (const double k : strikes) e.rows.push_back(chain_row(t, g, exp, k, selected));
    out.push_back(std::move(e));
  }
  return out;
}

struct OptionTicketViewModel::Calc {
  const OptionQuote* q = nullptr;
  double mult = 1, lot = 0.1, f = 0, qty = 0, held = 0, premium = 0, fee = 0, units = 0;
  bool opens_short = false, short_blocked = false, cant_afford = false, no_bid = false, settling = false;
  OptionLeg leg;
  std::optional<double> loss, margin, cost;
  bool ok = false;
};

void OptionTicketViewModel::pick(const OptionQuote& q, bool buy_side) {
  series = q.id;
  buy = buy_side;
}

const OptionQuote* OptionTicketViewModel::quote(const GameState& g) const {
  if (!series) return nullptr;
  for (const auto& q : g.options.chain) {
    if (q.id == *series) return &q;
  }
  return nullptr;
}

// A write posts margin, a buy pays the premium plus the fee: either must fit the available balance.
void OptionTicketViewModel::costs(Calc& c, double fee_rate) const {
  if (c.opens_short) {
    c.margin = write_margin(*c.q, c.f, c.qty, c.mult);
    c.cost = c.margin;
  } else if (buy) {
    c.cost = option_buy_cost(c.leg, c.f, fee_rate);
  }
}

std::optional<OptionTicketViewModel::Calc> OptionTicketViewModel::calc(const GameState& g) const {
  Calc c;
  c.q = quote(g);
  if (c.q == nullptr) return std::nullopt;
  const Options& o = g.options;
  c.mult = o.multiplier;
  c.lot = o.lot;
  c.f = g.account.mark;
  const double typed = fmt::parse_decimal(qty);
  c.qty = std::floor((std::isfinite(typed) ? typed : 0) / c.lot + 1e-9) * c.lot;
  for (const auto& p : o.positions) {
    if (p.series == c.q->id) c.held = p.qty;
  }
  c.opens_short = !buy && c.qty > std::max(0.0, c.held);
  c.short_blocked = c.opens_short && !o.allow_short;
  c.leg = leg_from_quote(*c.q, buy, c.qty, c.mult);
  const double typed_limit = fmt::parse_decimal(limit_price);
  c.premium = limit && typed_limit > 0 ? typed_limit : c.leg.premium;
  c.leg.premium = c.premium;
  c.units = c.qty * c.mult * (buy ? 1 : -1);
  c.loss = max_loss(c.leg);
  c.fee = option_fee(c.leg, c.f, o.fee_rate);
  costs(c, o.fee_rate);
  c.cant_afford = c.cost && *c.cost > g.account.available;
  c.no_bid = !buy && !limit && c.q->bid <= 0;
  c.settling = c.q->settling;
  c.ok = c.qty > 0 && !c.short_blocked && !c.cant_afford && !c.no_bid && !c.settling && (!limit || typed_limit > 0);
  return c;
}

OptionTicketView OptionTicketViewModel::view(const Texts& t, const GameState& g, bool busy) const {
  OptionTicketView v;
  const auto c = calc(g);
  if (!c) return v;
  const Locale L = t.locale();
  v.has_quote = true;
  v.series = series_label(*c->q, L);
  v.premium = fmt::fmt_num(c->premium, L, 2) + " \xC3\x97 " + fmt::fmt_num(c->qty, L, 1) + " \xC3\x97 " +
              fmt::fmt_num(c->mult, L, 0);
  v.breakeven = fmt::fmt_num(breakeven(c->leg), L, 2);
  if (c->loss) v.max_loss = fmt::fmt_money(*c->loss, L);
  v.fee = fmt::fmt_money(c->fee, L);
  if (c->margin) v.margin = fmt::fmt_money(*c->margin, L);
  v.greeks = "\xCE\x94 " + fmt::fmt_num(c->q->delta * c->units, L) + " \xC2\xB7 \xCE\x98 " +
             fmt::fmt_num(c->q->theta * c->units, L, 3) + " \xC2\xB7 \xCE\xBD " + fmt::fmt_num(c->q->vega * c->units, L);
  if (c->short_blocked) v.short_locked = t.tr("opt.shortLocked");
  if (c->opens_short && !c->short_blocked) v.unbounded = kWarn + t.tr("opt.unbounded");
  if (c->no_bid) v.no_bid = kWarn + t.tr("opt.noBid");
  if (c->settling) v.settling = kWarn + t.tr("opt.settling", {{"min", std::to_string(g.options.settlement_minutes)}});
  if (c->cant_afford) {
    v.cant_afford = kWarn + t.tr(c->opens_short ? "opt.cantAffordMargin" : "opt.cantAffordPremium",
                                 {{"avail", fmt::fmt_money(g.account.available, L)}});
  }
  v.limit_placeholder = fmt::fmt_num(buy ? c->q->ask : c->q->bid, L, 2);
  v.can_submit = !busy && c->ok;
  if (c->qty > 0) {
    v.lo = std::min(c->leg.strike, c->f) * 0.8;
    v.hi = std::max(c->leg.strike, c->f) * 1.2;
    v.curve = payoff_curve(c->leg, v.lo, v.hi);
    v.mark = c->f;
    v.breakeven_price = breakeven(c->leg);
  }
  return v;
}

std::optional<OptionDraft> OptionTicketViewModel::draft(const Texts& t, const GameState& g, bool busy) const {
  const auto c = calc(g);
  if (!c || busy || !c->ok) return std::nullopt;
  OptionDraft d;
  d.series = c->q->id;
  d.label = series_label(*c->q, t.locale());
  d.buy = buy;
  d.limit = limit;
  d.qty = fmt::qty_to_lot(c->qty, c->lot);
  if (limit) d.premium = fmt::price_to_tick(fmt::parse_decimal(limit_price), g.options.premium_tick);
  d.opens_exposure = buy ? c->held >= 0 : c->opens_short;
  d.risk = c->loss ? c->loss : c->margin;
  return d;
}

}  // namespace tradelab::desktop::game
