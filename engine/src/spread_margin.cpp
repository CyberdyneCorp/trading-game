// Defined-risk (vertical spread) margin, declared per scenario (design D5 of
// add-academy-and-strategies). Without OptionSpec::spread_margin every short is margined on its own.

#include "spread_margin.hpp"

#include <algorithm>
#include <limits>

#include "accounting.hpp"

namespace tradelab::detail {
namespace {

Money plus(Money a, Money b) { return Money::from_raw(a.raw + b.raw); }

// Worst loss at expiry per unit of a short paired with a long of the same kind and expiry.
Price pair_requirement(const OptionSeries& short_s, const OptionSeries& long_s) {
  const std::int64_t d = short_s.kind == OptionKind::Call ? long_s.strike.raw - short_s.strike.raw
                                                          : short_s.strike.raw - long_s.strike.raw;
  return Price::from_raw(std::max<std::int64_t>(0, d));
}

struct Candidate {
  std::size_t index;
  Price requirement;
  std::uint32_t series;
};

// One short after pairing: the paired and unpaired quantities and the paired requirement.
struct PairedShort {
  const OptionSeries* series = nullptr;
  Qty paired;
  Qty unpaired;
  Money requirement;
  bool can_lose = false;  // some paired unit has a requirement > 0
  ShortMargin paired_margin;
  ShortMargin unpaired_margin;
};

std::vector<Candidate> candidates_for(const Scenario& sc, const OptionSeries& s, const std::vector<Holding>& h,
                                      const std::vector<std::int64_t>& long_left) {
  std::vector<Candidate> out;
  for (std::size_t j = 0; j < h.size(); ++j) {
    if (long_left[j] <= 0) continue;
    const OptionSeries* l = find_series(sc, h[j].series);
    if (l->kind != s.kind || l->expiry_step != s.expiry_step) continue;
    out.push_back({j, pair_requirement(s, *l), l->id});
  }
  std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
    return a.requirement != b.requirement ? a.requirement < b.requirement : a.series < b.series;
  });
  return out;
}

// Greedy pairing of one short with the longs still free, lowest requirement first.
PairedShort pair_short(const Scenario& sc, const OptionSeries& s, Qty short_qty, const std::vector<Holding>& h,
                       std::vector<std::int64_t>& long_left) {
  PairedShort p;
  p.series = &s;
  std::int64_t left = short_qty.raw;
  for (const Candidate& c : candidates_for(sc, s, h, long_left)) {
    if (left == 0) break;
    const std::int64_t take = std::min(left, long_left[c.index]);
    long_left[c.index] -= take;
    left -= take;
    p.paired = Qty::from_raw(p.paired.raw + take);
    p.requirement = plus(p.requirement, units_value(sc, Qty::from_raw(take), c.requirement, Rounding::Ceil));
    p.can_lose = p.can_lose || c.requirement.raw > 0;
  }
  p.unpaired = Qty::from_raw(left);
  return p;
}

void margin_pair(const Scenario& sc, PairedShort& p, std::uint32_t step, Price f) {
  const OptionQuote q = quote(sc, *p.series, step, f);
  const ShortMargin per_leg = short_margin(sc, *p.series, p.paired, q, f);
  p.paired_margin = {std::min(per_leg.initial, p.requirement), std::min(per_leg.maintenance, p.requirement)};
  p.unpaired_margin = p.unpaired.raw > 0 ? short_margin(sc, *p.series, p.unpaired, q, f) : ShortMargin{};
}

// Both sides of one expiry cannot lose at once when every losing put pair's short strike is at or
// below every losing call pair's short strike: the side with the smaller sum is then not margined.
bool sides_exclusive(const std::vector<PairedShort*>& calls, const std::vector<PairedShort*>& puts) {
  Price put_max{};
  Price call_min = Price::from_raw(std::numeric_limits<std::int64_t>::max());
  bool any_call = false;
  bool any_put = false;
  for (const PairedShort* c : calls) {
    if (!c->can_lose) continue;
    any_call = true;
    call_min = std::min(call_min, c->series->strike);
  }
  for (const PairedShort* p : puts) {
    if (!p->can_lose) continue;
    any_put = true;
    put_max = std::max(put_max, p->series->strike);
  }
  return any_call && any_put && put_max <= call_min;
}

Money side_sum(const std::vector<PairedShort*>& side, bool initial) {
  Money total{};
  for (const PairedShort* p : side) total = plus(total, initial ? p->paired_margin.initial : p->paired_margin.maintenance);
  return total;
}

void zero_side(std::vector<PairedShort*>& side, bool initial) {
  for (PairedShort* p : side) (initial ? p->paired_margin.initial : p->paired_margin.maintenance) = Money{};
}

// The condor rule for one expiry, separately for initial and maintenance margin (ties keep calls).
void apply_condor_rule(std::vector<PairedShort>& shorts, std::uint32_t expiry) {
  std::vector<PairedShort*> calls;
  std::vector<PairedShort*> puts;
  for (PairedShort& p : shorts) {
    if (p.series->expiry_step != expiry) continue;
    (p.series->kind == OptionKind::Call ? calls : puts).push_back(&p);
  }
  if (!sides_exclusive(calls, puts)) return;
  for (const bool initial : {true, false}) {
    if (side_sum(calls, initial) >= side_sum(puts, initial)) zero_side(puts, initial);
    else zero_side(calls, initial);
  }
}

std::vector<ShortRequirement> per_leg(const Scenario& sc, const std::vector<Holding>& h, std::uint32_t step, Price f) {
  std::vector<ShortRequirement> out;
  for (const Holding& x : h) {
    if (x.qty.raw >= 0) continue;
    const OptionSeries* s = find_series(sc, x.series);
    const ShortMargin m = short_margin(sc, *s, x.qty, quote(sc, *s, step, f), f);
    out.push_back({x.series, m.initial, m.maintenance});
  }
  return out;
}

}  // namespace

std::vector<Holding> holdings_of(const RunState& st, bool working_buys) {
  std::vector<Holding> h;
  h.reserve(st.option_positions.size());
  for (const OptionPosition& p : st.option_positions) h.push_back({p.series, p.qty});
  if (working_buys) {
    for (const Order& o : st.orders) {
      if (!o.active() || o.series == 0 || o.side != OrderSide::Buy || o.placed_step != st.step) continue;
      add_holding(h, o.series, o.remaining());
    }
  }
  return h;
}

void add_holding(std::vector<Holding>& h, std::uint32_t series, Qty delta) {
  for (Holding& x : h) {
    if (x.series != series) continue;
    x.qty = Qty::from_raw(x.qty.raw + delta.raw);
    return;
  }
  h.push_back({series, delta});
}

std::vector<ShortRequirement> short_requirements(const Scenario& sc, std::vector<Holding> h, std::uint32_t step, Price f) {
  if (!sc.options.spread_margin) return per_leg(sc, h, step, f);  // position order, exactly as before
  std::sort(h.begin(), h.end(), [](const Holding& a, const Holding& b) { return a.series < b.series; });
  std::vector<std::int64_t> long_left(h.size());
  for (std::size_t j = 0; j < h.size(); ++j) long_left[j] = std::max<std::int64_t>(0, h[j].qty.raw);
  std::vector<PairedShort> shorts;
  for (const Holding& x : h) {
    if (x.qty.raw >= 0) continue;
    shorts.push_back(pair_short(sc, *find_series(sc, x.series), abs_qty(x.qty), h, long_left));
    margin_pair(sc, shorts.back(), step, f);
  }
  std::vector<std::uint32_t> expiries;
  expiries.reserve(shorts.size());
  for (const PairedShort& p : shorts) expiries.push_back(p.series->expiry_step);
  std::sort(expiries.begin(), expiries.end());
  expiries.erase(std::unique(expiries.begin(), expiries.end()), expiries.end());
  for (const std::uint32_t e : expiries) apply_condor_rule(shorts, e);
  std::vector<ShortRequirement> out;
  out.reserve(shorts.size());
  for (const PairedShort& p : shorts) {
    out.push_back({p.series->id, plus(p.paired_margin.initial, p.unpaired_margin.initial),
                   plus(p.paired_margin.maintenance, p.unpaired_margin.maintenance)});
  }
  return out;
}

Money total_initial(const std::vector<ShortRequirement>& r) {
  Money total{};
  for (const ShortRequirement& x : r) total = plus(total, x.initial);
  return total;
}

}  // namespace tradelab::detail
