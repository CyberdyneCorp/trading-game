#include "viewmodel/game/strategies.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>

#include "model/storage/json_writer.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/whatif_lab.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr std::array<std::string_view, 11> kTemplateCodes = {
    "bull_call",   "bear_put",      "covered_call", "protective_put", "straddle",    "strangle",
    "iron_condor", "calendar_call", "calendar_put", "diagonal_call",  "diagonal_put"};

bool writes(Template t) {
  return t == Template::BullCall || t == Template::BearPut || t == Template::CoveredCall || t == Template::IronCondor ||
         two_expiry(t);
}

// The strike nearest to the forward (the lower one on a tie).
double nearest(const std::vector<double>& strikes, double f) {
  double best = strikes.front();
  for (const double k : strikes) {
    if (std::fabs(k - f) < std::fabs(best - f)) best = k;
  }
  return best;
}

std::optional<double> at(const std::vector<double>& v, std::ptrdiff_t i) {
  if (i < 0 || i >= static_cast<std::ptrdiff_t>(v.size())) return std::nullopt;
  return v[static_cast<std::size_t>(i)];
}

struct Around {
  std::vector<double> below;  // descending
  std::vector<double> above;  // ascending
};

Around around(const std::vector<double>& strikes, double f) {
  Around a;
  for (const double k : strikes) {
    if (k < f) a.below.insert(a.below.begin(), k);
    if (k > f) a.above.push_back(k);
  }
  return a;
}

std::optional<BuilderLeg> opt(LegKind kind, bool buy, std::optional<double> strike, double qty) {
  if (!strike) return std::nullopt;
  return BuilderLeg{kind, buy, strike, qty};
}

std::optional<double> at_or_below(const std::vector<double>& strikes, double f) {
  std::optional<double> out;
  for (const double k : strikes) {
    if (k <= f) out = k;
  }
  return out;
}

std::vector<std::optional<BuilderLeg>> raw_legs(Template t, const std::vector<double>& strikes, double f, double n) {
  const double atm = nearest(strikes, f);
  const auto i = std::find(strikes.begin(), strikes.end(), atm) - strikes.begin();
  const Around a = around(strikes, f);
  const BuilderLeg perp{LegKind::Perp, true, std::nullopt, n};
  switch (t) {
    case Template::BullCall: return {opt(LegKind::Call, true, atm, n), opt(LegKind::Call, false, at(strikes, i + 1), n)};
    case Template::BearPut: return {opt(LegKind::Put, true, atm, n), opt(LegKind::Put, false, at(strikes, i - 1), n)};
    case Template::CoveredCall: return {perp, opt(LegKind::Call, false, at(a.above, 0), n)};
    case Template::ProtectivePut: return {perp, opt(LegKind::Put, true, at_or_below(strikes, f), n)};
    case Template::Straddle: return {opt(LegKind::Call, true, atm, n), opt(LegKind::Put, true, atm, n)};
    case Template::Strangle: return {opt(LegKind::Call, true, at(a.above, 0), n), opt(LegKind::Put, true, at(a.below, 0), n)};
    case Template::IronCondor:
      return {opt(LegKind::Put, true, at(a.below, 1), n), opt(LegKind::Put, false, at(a.below, 0), n),
              opt(LegKind::Call, false, at(a.above, 0), n), opt(LegKind::Call, true, at(a.above, 1), n)};
    default: break;  // two-expiry templates: two_expiry_template_legs
  }
  return {std::nullopt};
}

bool is_call_template(Template t) { return t == Template::CalendarCall || t == Template::DiagonalCall; }

std::optional<BuilderLeg> with_expiry(std::optional<BuilderLeg> l, int expiry) {
  if (l) l->expiry = expiry;
  return l;
}

// The near expiry sold, the far one bought: a calendar at the money on both, a diagonal selling the next
// strike out of the money (above for calls, below for puts).
std::vector<std::optional<BuilderLeg>> two_expiry_legs(Template t, const ExpiryStrikes& near, const ExpiryStrikes& far, double f,
                                                       double n) {
  const LegKind kind = is_call_template(t) ? LegKind::Call : LegKind::Put;
  const double atm = nearest(near.strikes, f);
  const auto i = std::find(near.strikes.begin(), near.strikes.end(), atm) - near.strikes.begin();
  const bool far_has = std::find(far.strikes.begin(), far.strikes.end(), atm) != far.strikes.end();
  const bool calendar = t == Template::CalendarCall || t == Template::CalendarPut;
  const std::optional<double> short_strike =
      calendar ? std::optional<double>(atm) : at(near.strikes, kind == LegKind::Call ? i + 1 : i - 1);
  return {with_expiry(opt(kind, false, short_strike, n), near.step),
          with_expiry(opt(kind, true, far_has ? std::optional<double>(atm) : std::nullopt, n), far.step)};
}

// The legs when every one exists (each side flipped for the short version); empty otherwise.
std::vector<BuilderLeg> unwrap(const std::vector<std::optional<BuilderLeg>>& legs, bool flip) {
  std::vector<BuilderLeg> out;
  for (const auto& l : legs) {
    if (!l) return {};
    out.push_back(*l);
    if (flip) out.back().buy = !out.back().buy;
  }
  return out;
}

int rank(const BuilderLeg& l) {
  if (l.kind == LegKind::Perp) return 1;
  return l.buy ? 0 : 2;
}

}  // namespace

std::string_view template_code(Template t) { return kTemplateCodes[static_cast<std::size_t>(t)]; }

std::optional<Template> template_from(std::string_view code) {
  for (const Template t : kTemplates) {
    if (template_code(t) == code) return t;
  }
  return std::nullopt;
}

std::string_view leg_kind_code(LegKind k) {
  switch (k) {
    case LegKind::Call: return "call";
    case LegKind::Put: return "put";
    case LegKind::Perp: break;
  }
  return "perp";
}

std::optional<LegKind> leg_kind_from(std::string_view code) {
  if (code == "call") return LegKind::Call;
  if (code == "put") return LegKind::Put;
  if (code == "perp") return LegKind::Perp;
  return std::nullopt;
}

bool two_expiry(Template t) {
  return t == Template::CalendarCall || t == Template::CalendarPut || t == Template::DiagonalCall || t == Template::DiagonalPut;
}

bool template_shortable(Template t) { return t == Template::Straddle || t == Template::Strangle || two_expiry(t); }

bool template_writes(Template t, bool short_side) { return writes(t) || (short_side && template_shortable(t)); }

std::string strategy_of(Template t, bool short_side) {
  const std::string code(template_code(t));
  return short_side && template_shortable(t) ? "short_" + code : code;
}

std::vector<int> expiries_of(const std::vector<OptionQuote>& chain) {
  std::set<int> out;
  for (const auto& q : chain) {
    if (q.listed && !q.settling) out.insert(q.expiry_step);
  }
  return {out.begin(), out.end()};
}

std::vector<double> strikes_of(const std::vector<OptionQuote>& chain, int expiry) {
  std::set<double> out;
  for (const auto& q : chain) {
    if (q.expiry_step == expiry) out.insert(q.strike);
  }
  return {out.begin(), out.end()};
}

const OptionQuote* series_for(const std::vector<OptionQuote>& chain, int expiry, LegKind kind, double strike) {
  const std::string_view want = leg_kind_code(kind);
  for (const auto& q : chain) {
    if (q.expiry_step == expiry && q.kind == want && q.strike == strike) return &q;
  }
  return nullptr;
}

std::optional<std::vector<BuilderLeg>> template_legs(Template t, const std::vector<double>& strikes, double f, double n,
                                                     bool short_side) {
  if (strikes.empty()) return std::nullopt;
  std::vector<BuilderLeg> out = unwrap(raw_legs(t, strikes, f, n), short_side && template_shortable(t));
  if (out.empty()) return std::nullopt;
  return out;
}

std::optional<std::vector<BuilderLeg>> two_expiry_template_legs(Template t, const ExpiryStrikes& near, const ExpiryStrikes& far,
                                                                double f, double n, bool short_side) {
  if (!two_expiry(t) || near.strikes.empty() || far.step <= near.step) return std::nullopt;
  std::vector<BuilderLeg> out = unwrap(two_expiry_legs(t, near, far, f, n), short_side);
  if (out.empty()) return std::nullopt;
  return out;
}

std::vector<BuilderLeg> submission_order(const std::vector<BuilderLeg>& legs) {
  std::vector<BuilderLeg> out = legs;
  std::stable_sort(out.begin(), out.end(), [](const BuilderLeg& a, const BuilderLeg& b) { return rank(a) < rank(b); });
  return out;
}

bool writes_options(const std::vector<BuilderLeg>& legs) {
  return std::any_of(legs.begin(), legs.end(), [](const BuilderLeg& l) { return l.kind != LegKind::Perp && !l.buy; });
}

std::string legs_problem(const std::vector<BuilderLeg>& legs, int expiry) {
  if (legs.empty()) return "empty";
  const auto perps = std::count_if(legs.begin(), legs.end(), [](const BuilderLeg& l) { return l.kind == LegKind::Perp; });
  if (legs.size() - static_cast<std::size_t>(perps) > kMaxOptionLegs) return "too_many";
  if (perps > 1) return "two_perps";
  if (leg_expiries(legs, expiry).size() > 2) return "expiries";
  return std::all_of(legs.begin(), legs.end(), [](const BuilderLeg& l) { return l.qty > 0; }) ? "" : "qty";
}

std::vector<int> leg_expiries(const std::vector<BuilderLeg>& legs, int expiry) {
  std::set<int> out;
  for (const auto& l : legs) {
    if (l.kind != LegKind::Perp) out.insert(l.expiry.value_or(expiry));
  }
  return {out.begin(), out.end()};
}

std::string premium_text(double v, double tick) {
  return fmt::to_fixed_text(std::round(v / tick) * tick, fmt::step_decimals(tick));
}

double default_limit(double mid, bool buy, double tick) {
  const double ticks = mid / tick;
  const double n = buy ? std::floor(ticks + 1e-9) : std::ceil(ticks - 1e-9);
  return std::max(1.0, n) * tick;
}

std::string perp_contracts(const GameState& g, double units) {
  return fmt::qty_to_lot(units / g.contract.multiplier, g.contract.lot);
}

std::optional<std::vector<OrderLeg>> order_legs(const GameState& g, int expiry, const std::vector<BuilderLeg>& legs) {
  std::vector<OrderLeg> out;
  for (const auto& l : submission_order(legs)) {
    if (l.kind == LegKind::Perp) {
      out.push_back({0, l.buy, perp_contracts(g, l.qty), std::nullopt});
      continue;
    }
    const OptionQuote* q = l.strike ? series_for(g.options.chain, l.expiry.value_or(expiry), l.kind, *l.strike) : nullptr;
    if (q == nullptr) return std::nullopt;
    std::optional<std::string> premium;
    if (l.limit) premium = premium_text(*l.limit, g.options.premium_tick);
    out.push_back({q->id, l.buy, fmt::qty_to_lot(l.qty, g.options.lot), premium});
  }
  return out;
}

namespace {

bool closes_long(const Order& o, double units) {
  const bool stop_type = o.type == "stop_market" || o.type == "trailing_stop";
  return o.series == 0 && o.reduce_only && o.side == "sell" && stop_type && o.price > 0 && o.qty - o.filled >= units;
}

}  // namespace

std::optional<std::string> long_stop(const GameState& g) {
  const double units = g.position.qty;
  if (units <= 0) return std::nullopt;
  const Order* best = nullptr;
  for (const auto& o : g.orders) {
    if (closes_long(o, units) && (best == nullptr || o.price > best->price)) best = &o;
  }
  return best != nullptr ? std::optional<std::string>(best->price_text) : std::nullopt;
}

std::optional<HeldLong> held_long(const GameState& g) {
  if (g.position.qty <= 0) return std::nullopt;
  return HeldLong{g.position.qty_text, g.position.entry_text, long_stop(g)};
}

double cover_contracts(const GameState& g, const HeldLong& held) {
  const double units = fmt::num(held.qty) * g.contract.multiplier;
  return fmt::num(fmt::qty_to_lot(units / g.options.multiplier, g.options.lot));
}

bool covers_long(Template t) { return t == Template::CoveredCall || t == Template::ProtectivePut; }

std::vector<AnalysisLeg> combo_legs(const std::vector<OrderLeg>& orders, const std::optional<ComboTif>& tif) {
  std::vector<AnalysisLeg> out;
  out.reserve(orders.size());
  for (const auto& o : orders) {
    AnalysisLeg leg{o.series, o.buy, o.qty, std::nullopt, std::nullopt, std::nullopt, {}, 0};
    if (o.series != 0 && o.premium) {
      leg.premium = o.premium;
      if (tif && tif->tif != "gtc") {
        leg.tif = tif->tif;
        leg.tif_bars = tif->tif == "bars" ? tif->tif_bars : 0;
      }
    }
    out.push_back(std::move(leg));
  }
  return out;
}

std::vector<AnalysisLeg> whatif_legs(const std::vector<OrderLeg>& orders, const std::optional<HeldLong>& held) {
  std::vector<AnalysisLeg> out;
  for (const auto& o : orders) {
    const bool limit = o.series != 0 && o.premium;
    out.push_back({o.series, o.buy, o.qty, limit ? o.premium : std::nullopt, std::nullopt, std::nullopt, {}, 0});
  }
  if (held) out.push_back({0, true, held->qty, held->entry, held->stop, std::nullopt, {}, 0});
  return out;
}

std::optional<double> contracts_for_risk(double equity, double risk_pct, std::optional<double> max_loss, double n, double lot) {
  const bool usable = max_loss && *max_loss > 0 && n > 0 && risk_pct > 0 && equity > 0 && lot > 0;
  if (!usable) return std::nullopt;
  const double per_contract = *max_loss / n;
  return fmt::num(fmt::qty_to_lot(equity * risk_pct / 100 / per_contract, lot));
}

namespace {

// String(Math.abs(Number(q))): "-1.50000000" -> "1.5".
std::string unsigned_text(const std::string& decimal) { return js_number(std::fabs(fmt::num(decimal))); }

}  // namespace

std::vector<AnalysisLeg> position_legs(const GameState& g) {
  std::vector<AnalysisLeg> out;
  for (const auto& p : g.options.positions) {
    out.push_back({p.series, p.qty > 0, unsigned_text(p.qty_text), p.avg_premium_text, std::nullopt, std::nullopt, {}, 0});
  }
  if (g.position.qty != 0) {
    out.push_back(
        {0, g.position.qty > 0, unsigned_text(g.position.qty_text), g.position.entry_text, std::nullopt, std::nullopt, {}, 0});
  }
  return out;
}

namespace {

// A limit leg of the dry run: its premium and the combo's time in force (D2).
void write_limit(storage::JsonWriter& w, const AnalysisLeg& l) {
  if (l.premium) w.field("type", "limit").field("premium", *l.premium);
  if (!l.tif.empty()) w.field("tif", l.tif);
  if (l.tif == "bars") w.field("tifBars", l.tif_bars);
}

}  // namespace

std::string legs_json(const std::vector<AnalysisLeg>& legs) {
  storage::JsonWriter w;
  w.begin_array();
  for (const auto& l : legs) {
    w.begin_object();
    if (l.series == 0) w.field("kind", "perp");
    else w.field("series", l.series);
    w.field("side", l.buy ? "buy" : "sell").field("qty", l.qty);
    if (l.entry) w.field("entry", *l.entry);
    if (l.stop) w.field("stop", *l.stop);
    write_limit(w, l);
    w.end_object();
  }
  return w.end_array().text();
}

namespace {

std::optional<double> strike_nearest(const std::vector<double>& strikes, double k) {
  if (strikes.empty()) return std::nullopt;
  return nearest(strikes, k);
}

struct HeldQuote {
  HeldLeg held;
  const OptionQuote* quote = nullptr;
};

std::vector<HeldQuote> held_quotes(const std::vector<OptionQuote>& chain, const std::vector<HeldLeg>& held) {
  std::vector<HeldQuote> out;
  for (const auto& h : held) {
    const auto q = std::find_if(chain.begin(), chain.end(), [&](const OptionQuote& x) { return x.id == h.series; });
    if (q != chain.end() && h.qty != 0) out.push_back({h, &*q});
  }
  return out;
}

// Re-centring: every strike shifted by the strike at the money minus the structure's middle strike.
double recenter_shift(const std::vector<HeldQuote>& near, const std::vector<double>& strikes, double f) {
  double lo = near.front().quote->strike;
  double hi = lo;
  for (const auto& l : near) {
    lo = std::min(lo, l.quote->strike);
    hi = std::max(hi, l.quote->strike);
  }
  return strike_nearest(strikes, f).value_or(f) - (lo + hi) / 2;
}

LegKind kind_of(const OptionQuote& q) { return q.kind == "call" ? LegKind::Call : LegKind::Put; }

}  // namespace

std::optional<RollPlan> roll_orders(const GameState& g, const std::vector<HeldLeg>& held, int to, bool recenter, double f) {
  const auto& chain = g.options.chain;
  const std::vector<HeldQuote> legs = held_quotes(chain, held);
  if (legs.empty()) return std::nullopt;
  int from = legs.front().quote->expiry_step;
  for (const auto& l : legs) from = std::min(from, l.quote->expiry_step);
  std::vector<int> held_expiries;
  for (const auto& l : legs) held_expiries.push_back(l.quote->expiry_step);
  const std::vector<int> targets = roll_targets(chain, from, held_expiries);
  if (std::find(targets.begin(), targets.end(), to) == targets.end()) return std::nullopt;
  std::vector<HeldQuote> near;
  std::copy_if(legs.begin(), legs.end(), std::back_inserter(near), [&](const HeldQuote& l) { return l.quote->expiry_step == from; });
  const std::vector<double> strikes = strikes_of(chain, to);
  const double shift = recenter ? recenter_shift(near, strikes, f) : 0;
  std::vector<BuilderLeg> out;
  for (const auto& [h, q] : near) {
    const auto k = strike_nearest(strikes, q->strike + shift);
    if (!k || series_for(chain, to, kind_of(*q), *k) == nullptr) return std::nullopt;
    const double units = std::fabs(h.qty);
    out.push_back({kind_of(*q), h.qty < 0, q->strike, units, from, std::nullopt});
    out.push_back({kind_of(*q), h.qty > 0, *k, units, to, std::nullopt});
  }
  RollPlan plan{{}, from, to};
  for (const auto& l : submission_order(out)) {
    const OptionQuote* q = series_for(chain, *l.expiry, l.kind, *l.strike);
    plan.orders.push_back({q->id, l.buy, fmt::qty_to_lot(l.qty, g.options.lot), std::nullopt});
  }
  return plan;
}

std::string margin_label_key(bool spread_margin, bool calendar_margin, bool two_expiries) {
  if (!spread_margin) return "builder.marginPerLeg";
  return two_expiries && !calendar_margin ? "builder.marginSameExpiry" : "builder.marginSpread";
}

// Later listed expiries, leaving out those the combo already holds: rolling a calendar's short onto its
// long's expiry would net the two and close the calendar (web rollTargets).
std::vector<int> roll_targets(const std::vector<OptionQuote>& chain, int from, const std::vector<int>& held) {
  std::vector<int> out;
  for (const int e : expiries_of(chain)) {
    if (e > from && std::find(held.begin(), held.end(), e) == held.end()) out.push_back(e);
  }
  return out;
}

std::optional<OrderLeg> hedge_order(const GameState& g, double delta_units) {
  const double lot = g.contract.lot;
  const double contracts = -delta_units / g.contract.multiplier;
  const double lots = std::trunc(std::fabs(contracts) / lot + 1e-9);
  if (lots < 1) return std::nullopt;
  return OrderLeg{0, contracts > 0, fmt::qty_to_lot(lots * lot, lot), std::nullopt};
}

}  // namespace tradelab::desktop::game
