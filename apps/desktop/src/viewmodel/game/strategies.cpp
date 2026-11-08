#include "viewmodel/game/strategies.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "model/storage/json_writer.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/whatif_lab.hpp"

namespace tradelab::desktop::game {

namespace {

constexpr std::array<std::string_view, 7> kTemplateCodes = {"bull_call", "bear_put", "covered_call", "protective_put",
                                                            "straddle",  "strangle", "iron_condor"};

bool writes(Template t) {
  return t == Template::BullCall || t == Template::BearPut || t == Template::CoveredCall || t == Template::IronCondor;
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
  }
  return {};
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

bool template_shortable(Template t) { return t == Template::Straddle || t == Template::Strangle; }

bool template_writes(Template t, bool short_side) { return writes(t) || (short_side && template_shortable(t)); }

std::string strategy_of(Template t, bool short_side) {
  if (short_side && t == Template::Straddle) return "short_straddle";
  if (short_side && t == Template::Strangle) return "short_strangle";
  return std::string(template_code(t));
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
  std::vector<BuilderLeg> out;
  for (const auto& l : raw_legs(t, strikes, f, n)) {
    if (!l) return std::nullopt;
    out.push_back(*l);
  }
  if (short_side && template_shortable(t)) {
    for (auto& l : out) l.buy = !l.buy;
  }
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

std::string legs_problem(const std::vector<BuilderLeg>& legs) {
  if (legs.empty()) return "empty";
  const auto perps = std::count_if(legs.begin(), legs.end(), [](const BuilderLeg& l) { return l.kind == LegKind::Perp; });
  if (legs.size() - static_cast<std::size_t>(perps) > kMaxOptionLegs) return "too_many";
  if (perps > 1) return "two_perps";
  return std::all_of(legs.begin(), legs.end(), [](const BuilderLeg& l) { return l.qty > 0; }) ? "" : "qty";
}

std::string perp_contracts(const GameState& g, double units) {
  return fmt::qty_to_lot(units / g.contract.multiplier, g.contract.lot);
}

std::optional<std::vector<OrderLeg>> order_legs(const GameState& g, int expiry, const std::vector<BuilderLeg>& legs) {
  std::vector<OrderLeg> out;
  for (const auto& l : submission_order(legs)) {
    if (l.kind == LegKind::Perp) {
      out.push_back({0, l.buy, perp_contracts(g, l.qty)});
      continue;
    }
    const OptionQuote* q = l.strike ? series_for(g.options.chain, expiry, l.kind, *l.strike) : nullptr;
    if (q == nullptr) return std::nullopt;
    out.push_back({q->id, l.buy, fmt::qty_to_lot(l.qty, g.options.lot)});
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

std::vector<AnalysisLeg> combo_legs(const std::vector<OrderLeg>& orders) {
  std::vector<AnalysisLeg> out;
  out.reserve(orders.size());
  for (const auto& o : orders) out.push_back({o.series, o.buy, o.qty, std::nullopt, std::nullopt});
  return out;
}

std::vector<AnalysisLeg> whatif_legs(const std::vector<OrderLeg>& orders, const std::optional<HeldLong>& held) {
  std::vector<AnalysisLeg> out = combo_legs(orders);
  if (held) out.push_back({0, true, held->qty, held->entry, held->stop});
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
    out.push_back({p.series, p.qty > 0, unsigned_text(p.qty_text), p.avg_premium_text, std::nullopt});
  }
  if (g.position.qty != 0) {
    out.push_back({0, g.position.qty > 0, unsigned_text(g.position.qty_text), g.position.entry_text, std::nullopt});
  }
  return out;
}

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
    w.end_object();
  }
  return w.end_array().text();
}

}  // namespace tradelab::desktop::game
