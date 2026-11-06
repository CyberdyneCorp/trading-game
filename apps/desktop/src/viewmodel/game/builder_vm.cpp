#include "viewmodel/game/builder_vm.hpp"

#include <algorithm>
#include <cmath>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::game {

namespace {


int kind_index(LegKind k) {
  if (k == LegKind::Call) return 0;
  return k == LegKind::Put ? 1 : 2;
}

int strike_index(const std::vector<double>& strikes, const std::optional<double>& k) {
  if (!k) return -1;
  const auto it = std::find(strikes.begin(), strikes.end(), *k);
  return it == strikes.end() ? -1 : static_cast<int>(it - strikes.begin());
}

bool write_allowed(const GameState& g) { return g.rules.options_write; }

}  // namespace

std::string order_leg_name(const Texts& t, const GameState& g, const OrderLeg& o) {
  const Locale L = t.locale();
  std::string what = t.tr("lab.perp");
  for (const auto& q : g.options.chain) {
    if (q.id == o.series && o.series != 0) what = std::string(q.kind == "call" ? "C " : "P ") + fmt::fmt_price(q.strike_text, L);
  }
  return std::string(o.buy ? "+" : "\xE2\x88\x92") + fmt::fmt_qty(o.qty, L) + " " + what;
}

void BuilderViewModel::sync(const GameState& g) {
  const auto e = expiries_of(g.options.chain);
  if (std::find(e.begin(), e.end(), expiry) == e.end()) expiry = e.empty() ? 0 : e.front();
}

void BuilderViewModel::set_expiry_index(const GameState& g, int index) {
  const auto e = expiries_of(g.options.chain);
  if (index >= 0 && index < static_cast<int>(e.size())) expiry = e[static_cast<std::size_t>(index)];
}

double BuilderViewModel::contracts(Locale L) const {
  const double n = fmt::parse_amount(qty, L);
  return std::isfinite(n) ? std::max(0.0, n) : 0.0;
}

void BuilderViewModel::pick(const GameState& g, Template t, Locale L) {
  tmpl = t;
  strategy = strategy_of(t, short_side);
  // A covered call or protective put on a held long covers that long, all of it, by default.
  const auto held = held_long(g);
  use_long = held && covers_long(t);
  if (use_long) qty = fmt::fmt_num(cover_contracts(g, *held), L, 1);
  const double n = contracts(L);
  legs = template_legs(t, strikes_of(g.options.chain, expiry), g.last_price(), n > 0 ? n : 1, short_side)
             .value_or(std::vector<BuilderLeg>{});
}

void BuilderViewModel::toggle_short(const GameState& g, Locale L) {
  short_side = !short_side;
  if (tmpl && template_shortable(*tmpl)) pick(g, *tmpl, L);
}

void BuilderViewModel::apply_qty(Locale L) {
  const double n = contracts(L);
  for (auto& l : legs) l.qty = n;
}

void BuilderViewModel::set_kind(const GameState& g, std::size_t i, LegKind kind) {
  if (i >= legs.size()) return;
  const auto strikes = strikes_of(g.options.chain, expiry);
  BuilderLeg& l = legs[i];
  l.kind = kind;
  if (kind == LegKind::Perp) l.strike.reset();
  else if (!l.strike && !strikes.empty()) l.strike = strikes.front();
  strategy = "custom";
}

void BuilderViewModel::set_side(std::size_t i, bool buy) {
  if (i >= legs.size()) return;
  legs[i].buy = buy;
  strategy = "custom";
}

void BuilderViewModel::set_strike(const GameState& g, std::size_t i, int index) {
  const auto strikes = strikes_of(g.options.chain, expiry);
  if (i >= legs.size() || index < 0 || index >= static_cast<int>(strikes.size())) return;
  legs[i].strike = strikes[static_cast<std::size_t>(index)];
}

void BuilderViewModel::set_leg_qty(std::size_t i, const std::string& text) {
  if (i >= legs.size()) return;
  const double v = fmt::parse_decimal(text);
  legs[i].qty = std::isfinite(v) ? std::max(0.0, v) : 0.0;
}

void BuilderViewModel::remove_leg(std::size_t i) {
  if (i >= legs.size()) return;
  legs.erase(legs.begin() + static_cast<std::ptrdiff_t>(i));
  strategy = "custom";
}

void BuilderViewModel::add_leg(const GameState& g, Locale L) {
  const auto strikes = strikes_of(g.options.chain, expiry);
  std::optional<double> k;
  for (const double s : strikes) {
    if (s >= g.last_price()) {
      k = s;
      break;
    }
  }
  if (!k && !strikes.empty()) k = strikes.front();
  const double n = contracts(L);
  legs.push_back({LegKind::Call, true, k, n > 0 ? n : 1});
  strategy = "custom";
}

std::optional<HeldLong> BuilderViewModel::covered(const GameState& g) const {
  return use_long ? held_long(g) : std::nullopt;
}

std::vector<BuilderLeg> BuilderViewModel::sent(const GameState& g) const {
  if (!covered(g)) return legs;
  std::vector<BuilderLeg> out;
  std::copy_if(legs.begin(), legs.end(), std::back_inserter(out), [](const BuilderLeg& l) { return !(l.kind == LegKind::Perp && l.buy); });
  return out;
}

std::optional<std::vector<OrderLeg>> BuilderViewModel::orders(const GameState& g) const {
  return order_legs(g, expiry, sent(g));
}

std::string BuilderViewModel::problem(const GameState& g) const {
  const auto s = sent(g);
  std::string p = legs_problem(s);
  if (!p.empty()) return p;
  if (!orders(g)) return "unlisted";
  return writes_options(s) && !write_allowed(g) ? "write_locked" : "";
}

std::optional<BuilderViewModel::Requests> BuilderViewModel::requests(const GameState& g) const {
  const auto o = orders(g);
  if (!o || o->empty() || !legs_problem(sent(g)).empty()) return std::nullopt;
  Requests r;
  r.whatif = run_request(whatif_legs(*o, covered(g)));
  r.preview = combo_request(combo_legs(*o));
  // Valued again whenever the state they are valued in moves (a bar, a fill, an order placed).
  r.key = r.whatif + r.preview + "|" + std::to_string(g.step) + "|" + std::to_string(g.events.size()) + "|" +
          std::to_string(g.account.available);
  return r;
}

void BuilderViewModel::clear_analysis() {
  whatif_.reset();
  preview_.reset();
  pending_ = 0;
  ticket.forget();
}

void BuilderViewModel::answer_whatif(const std::string& text) {
  whatif_ = whatif_from_json(text);
  pending_ = std::max(0, pending_ - 1);
}

void BuilderViewModel::answer_preview(const std::string& text) {
  preview_ = preview_from_json(text);
  pending_ = std::max(0, pending_ - 1);
}

bool BuilderViewModel::can_submit(const GameState& g, bool busy) const {
  const auto o = orders(g);
  const bool combo_ok = !preview_ || !preview_->ok() || preview_->value->accepted;
  return problem(g).empty() && !pending() && !busy && o && !o->empty() && combo_ok && g.active();
}

std::optional<double> BuilderViewModel::risk() const {
  if (whatif_ && whatif_->ok() && whatif_->value->expiry && whatif_->value->expiry->max_loss) {
    return std::max(0.0, fmt::num(*whatif_->value->expiry->max_loss));
  }
  if (preview_ && preview_->ok()) return fmt::num(preview_->value->required);
  return std::nullopt;
}

std::optional<ComboDraft> BuilderViewModel::draft(const Texts& t, const GameState& g, const std::vector<ComboRecord>& combos,
                                                  bool busy) const {
  if (!can_submit(g, busy)) return std::nullopt;
  ComboDraft d;
  d.orders = *orders(g);
  d.combo = next_combo_id(combos);
  d.strategy = strategy;
  d.label = t.count("combo.label", static_cast<long>(d.orders.size()), {{"name", t.tr("strategy." + strategy)}});
  d.risk = risk();
  d.unbounded = whatif_ && whatif_->ok() && whatif_->value->expiry && !whatif_->value->expiry->max_loss;
  return d;
}

std::vector<AnalysisLeg> BuilderViewModel::lab_legs(const GameState& g) const {
  const auto o = orders(g);
  return o ? whatif_legs(*o, covered(g)) : std::vector<AnalysisLeg>{};
}

BuilderView BuilderViewModel::view(const Texts& t, const GameState& g, bool busy) const {
  BuilderView v;
  v.title = t.tr("builder.title") + "  [" + t.tr("opt.simulated") + "]";
  if (!position_legs(g).empty()) v.lab_positions = "\xE2\x9A\x97 " + t.tr("builder.labPositions");  // ⚗
  const auto expiries = expiries_of(g.options.chain);
  v.available = g.options.enabled && !expiries.empty();
  if (!v.available) {
    v.problem = t.tr("builder.noExpiry");
    return v;
  }
  for (std::size_t i = 0; i < expiries.size(); ++i) {
    v.expiries.push_back(t.tr("builder.expiryAt", {{"step", std::to_string(expiries[i])}, {"n", std::to_string(expiries[i] - g.step)}}));
    if (expiries[i] == expiry) v.expiry_index = static_cast<int>(i);
  }
  for (const Template id : kTemplates) {
    v.templates.push_back({id, t.tr("strategy." + strategy_of(id, short_side)), !(template_writes(id, short_side) && !write_allowed(g)),
                           tmpl == id});
  }
  if (!write_allowed(g)) v.write_locked = t.tr("builder.writeLocked");
  v.caption = t.tr("strategy." + strategy);
  view_legs(t, g, v);
  const std::string p = problem(g);
  if (!p.empty() && !legs.empty()) v.problem = t.tr("builder.problem." + p);
  view_analysis(t, g, v);
  v.legging = t.tr("builder.legging");
  v.can_submit = can_submit(g, busy);
  return v;
}

void BuilderViewModel::view_legs(const Texts& t, const GameState& g, BuilderView& v) const {
  const Locale L = t.locale();
  const auto strikes = strikes_of(g.options.chain, expiry);
  v.kinds = {"Call", "Put", t.tr("lab.perp")};
  v.sides = {t.tr("opt.buy"), t.tr("opt.sell")};
  for (const double k : strikes) v.strikes.push_back(fmt::fmt_price(js_number(k), L));
  for (const auto& l : legs) {
    BuilderLegRow row{kind_index(l.kind), l.buy ? 0 : 1, strike_index(strikes, l.strike), fmt::fmt_num(l.qty, L, 1), false};
    if (l.kind != LegKind::Perp && !l.buy && l.strike) {
      const OptionQuote* q = series_for(g.options.chain, expiry, l.kind, *l.strike);
      row.no_bid = q != nullptr && q->bid <= 0;
    }
    v.legs.push_back(row);
  }
  const auto options = std::count_if(legs.begin(), legs.end(), [](const BuilderLeg& l) { return l.kind != LegKind::Perp; });
  v.can_add_leg = static_cast<std::size_t>(options) < kMaxOptionLegs;
  const auto held = held_long(g);
  const bool perp_buy = std::any_of(legs.begin(), legs.end(), [](const BuilderLeg& l) { return l.kind == LegKind::Perp && l.buy; });
  if (held && perp_buy) v.use_long = t.tr("builder.useLong", {{"qty", fmt::fmt_num(fmt::num(held->qty), L, 1)}});
}

void BuilderViewModel::view_analysis(const Texts& t, const GameState& g, BuilderView& v) const {
  const Locale L = t.locale();
  if (whatif_ && !whatif_->ok() && whatif_->error == "unsupported") v.no_analysis = t.tr("lab.unsupported");
  const auto o = orders(g);
  if (preview_ && preview_->ok() && o && preview_->value->legs.size() == o->size()) {
    const ComboPreview& p = *preview_->value;
    v.preview.push_back({t.tr("builder.order"), Tone::Muted});
    for (std::size_t i = 0; i < p.legs.size(); ++i) {
      const auto& r = p.legs[i];
      const std::string head = std::to_string(i + 1) + ". " + order_leg_name(t, g, (*o)[i]) + " @ " + fmt::fmt_price(r.quote, L) + " \xE2\x80\x94 ";
      v.preview.push_back(r.accepted ? Line{head + "\xE2\x9C\x93 " + t.tr("builder.accepted"), Tone::Plain}
                                     : Line{head + "\xE2\x9C\x96 " + rejection_text(t, r.reason), Tone::Down});
    }
    const double net = fmt::num(p.net_premium);
    v.totals.emplace_back(t.tr(net >= 0 ? "builder.credit" : "builder.debit"), fmt::fmt_money(std::fabs(net), L));
    v.totals.emplace_back(t.tr("ticket.estFee"), fmt::fmt_money(p.fees, L));
    v.totals.emplace_back(t.tr(g.rules.spread_margin ? "builder.marginSpread" : "builder.marginPerLeg"),
                          fmt::fmt_money(p.option_margin_after_fill, L));
    v.totals.emplace_back(t.tr("builder.availableAfter"), fmt::fmt_money(p.available_after_fill, L));
  }
  if (!whatif_ || !whatif_->ok()) return;
  const WhatIfResult& w = *whatif_->value;
  v.has_whatif = true;
  if (w.expiry) {
    v.analysis.emplace_back(t.tr("opt.breakeven"), breakevens_text(w, L));
    v.analysis.emplace_back(t.tr("lab.maxProfit"), max_profit_text(t, w));
    v.analysis.emplace_back(t.tr("lab.maxLoss"), max_loss_text(t, w));
  }
  v.table = whatif_compact(t, w);
}

}  // namespace tradelab::desktop::game
