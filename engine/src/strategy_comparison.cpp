// Strategy comparison of the strategy missions (add-academy-and-strategies design D8, scoring v13,
// report-only). After a run ends, each mission's reference structures are played on a fresh copy of
// the same run (same scenario, seed and start options) with the engine's own commands, so their fills,
// fees, funding, margin and settlement are the engine's. Nothing here touches the player's run, its
// log or a digest, and it is computed only once the run is over, so it uses no future at a decision.

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

#include "tradelab/indicators.hpp"
#include "tradelab/options.hpp"
#include "tradelab/scoring.hpp"

namespace tradelab {
namespace {

enum class LegKind : std::uint8_t { Perp, Call, Put };
enum class ExitRule : std::uint8_t { Hold, CloseBefore, RollBefore };
enum class EntryRule : std::uint8_t { FirstDecision, AfterHeadline };

// A leg relative to the market at entry: the listed strike nearest close x moneyness, then `steps`
// strikes away, on the `expiry`-th expiry still far enough for the exit rule (clamped to the last).
struct RefLeg {
  LegKind kind = LegKind::Perp;
  OrderSide side = OrderSide::Buy;
  std::int64_t moneyness_bp = 10000;
  std::int32_t steps = 0;
  std::uint32_t expiry = 0;
};

struct RefStructure {
  ComparisonStrategy name = ComparisonStrategy::NakedCall;
  std::vector<RefLeg> legs;
  ExitRule exit = ExitRule::Hold;
  std::uint32_t bars = 0;  // CloseBefore / RollBefore: decision bars before the expiry
  bool atr_stop = false;   // the perpetual leg carries a stop 2 x ATR(14) below the entry close
};

struct ComparisonDef {
  std::string_view mission;
  EntryRule entry = EntryRule::FirstDecision;
  std::vector<RefStructure> rows;
};

constexpr std::int64_t kReferenceContracts = 10;  // quantity when the player traded no option
constexpr std::uint32_t kAtrPeriod = 14;

constexpr RefLeg perp_long() { return {}; }
constexpr RefLeg option(LegKind k, OrderSide side, std::int64_t moneyness_bp, std::int32_t steps, std::uint32_t expiry = 0) {
  return {k, side, moneyness_bp, steps, expiry};
}

std::vector<ComparisonDef> definitions() {
  using enum ComparisonStrategy;
  constexpr OrderSide kBuy = OrderSide::Buy;
  constexpr OrderSide kSell = OrderSide::Sell;
  constexpr LegKind kCall = LegKind::Call;
  constexpr LegKind kPut = LegKind::Put;
  const RefLeg atm_call = option(kCall, kBuy, 10000, 0);
  const RefLeg short_atm_put = option(kPut, kSell, 10000, 0);
  return {
      {"m13-call-spread",
       EntryRule::AfterHeadline,
       {{NakedCall, {atm_call}, ExitRule::CloseBefore, 3, false},
        {BullCallSpread, {atm_call, option(kCall, kSell, 10000, 1)}, ExitRule::CloseBefore, 3, false}}},
      {"m14-covered-call",
       EntryRule::FirstDecision,
       {{LongOnly, {perp_long()}, ExitRule::Hold, 0, false},
        {CoveredCall, {perp_long(), option(kCall, kSell, 10500, 0)}, ExitRule::RollBefore, 0, false}}},
      {"m15-protective-put",
       EntryRule::FirstDecision,
       {{LongOnly, {perp_long()}, ExitRule::Hold, 0, false},
        {AtrStop, {perp_long()}, ExitRule::Hold, 0, true},
        {AtmPut, {perp_long(), option(kPut, kBuy, 10000, 0, 1)}, ExitRule::Hold, 0, false},
        {OtmPut, {perp_long(), option(kPut, kBuy, 9500, 0, 1)}, ExitRule::Hold, 0, false}}},
      {"m16-range-condor",
       EntryRule::FirstDecision,
       {{ShortStrangle, {option(kPut, kSell, 10000, -2), option(kCall, kSell, 10000, 2)}, ExitRule::Hold, 0, false},
        {IronCondor,
         {option(kPut, kBuy, 10000, -4), option(kPut, kSell, 10000, -2), option(kCall, kSell, 10000, 2), option(kCall, kBuy, 10000, 4)},
         ExitRule::Hold,
         0,
         false}}},
      {"m17-before-expiry",
       EntryRule::FirstDecision,
       {{HoldToSettlement, {short_atm_put}, ExitRule::Hold, 0, false},
        {CloseBeforeExpiry, {short_atm_put}, ExitRule::CloseBefore, 5, false},
        {RollBeforeExpiry, {short_atm_put}, ExitRule::RollBefore, 5, false}}},
  };
}

// ---- A fresh copy of the run, driven by commands ----

struct Sim {
  const Scenario& sc;
  RunState st;
  std::uint64_t seq = 1;

  Sim(const Scenario& s, const RunState& played) : sc(s), st(start_run(s, played.seed, played.options)) {}

  CommandResult send(Command c) {
    c.seq = seq;
    c.client_id = seq++;
    c.decision_step = st.step;
    return apply_command(sc, st, c);
  }
  void run_to(std::uint32_t step) {
    while (st.status == RunStatus::Active && st.step < step) advance(sc, st);
  }
  [[nodiscard]] bool active() const { return st.status == RunStatus::Active; }
};

// Distinct expiries listed at `step` that leave more than `bars` decision bars before them, soonest first.
std::vector<std::uint32_t> expiries_after(const Scenario& sc, std::uint32_t step, std::uint32_t bars) {
  std::vector<std::uint32_t> out;
  for (const OptionSeries& s : sc.options.series) {
    if (s.list_step <= step && s.expiry_step > step + bars) out.push_back(s.expiry_step);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::int64_t distance(Price a, Price b) { return a.raw > b.raw ? a.raw - b.raw : b.raw - a.raw; }

// The series of this kind and expiry nearest `target` (the lower strike on a tie), `steps` strikes away.
const OptionSeries* series_near(const Scenario& sc, OptionKind kind, std::uint32_t expiry, Price target, std::int32_t steps) {
  std::vector<OptionSeries> grid;
  for (const OptionSeries& s : sc.options.series) {
    if (s.kind == kind && s.expiry_step == expiry) grid.push_back(s);
  }
  std::sort(grid.begin(), grid.end(), [](const OptionSeries& a, const OptionSeries& b) { return a.strike < b.strike || (a.strike == b.strike && a.id < b.id); });
  std::size_t best = 0;
  for (std::size_t i = 1; i < grid.size(); ++i) {
    if (distance(grid[i].strike, target) < distance(grid[best].strike, target)) best = i;
  }
  const auto at = static_cast<std::ptrdiff_t>(best) + steps;
  if (grid.empty() || at < 0 || at >= static_cast<std::ptrdiff_t>(grid.size())) return nullptr;
  return find_series(sc, grid[static_cast<std::size_t>(at)].id);
}

Price scaled(Price p, std::int64_t bp) {
  return Price::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(p.raw) * bp, 10000, Rounding::HalfEven)));
}

Command option_order(std::uint32_t series, OrderSide side, Qty qty) {
  Command c;
  c.type = CommandType::PlaceOptionOrder;
  c.order_type = OrderType::Market;
  c.series = series;
  c.side = side;
  c.qty = qty;
  return c;
}

// The perpetual entry, with a stop 2 x ATR(14) below the entry close (rounded down to the tick) when asked.
Command perp_order(const Sim& sim, Qty units, bool atr_stop) {
  Command c;
  c.order_type = OrderType::Market;
  c.side = OrderSide::Buy;
  c.qty = units;
  if (!atr_stop) return c;
  const std::optional<Price> a = atr(sim.sc, sim.st.step, kAtrPeriod)[sim.st.step];
  const std::int64_t tick = sim.sc.contract.tick_size.raw;
  const std::int64_t stop = sim.sc.bars[sim.st.step].close.raw - 2 * (a ? a->raw : 0);
  if (a && stop > 0 && tick > 0) c.attach_stop = Price::from_raw(stop / tick * tick);
  return c;
}

struct Placed {
  std::vector<std::uint32_t> series;  // option series held by the structure, in leg order
  Reason refused = Reason::None;
};

// The listed series of an option leg: on its expiry (clamped to the last), near close x moneyness.
const OptionSeries* leg_series(const Sim& sim, const RefLeg& l, const std::vector<std::uint32_t>& expiries) {
  const std::uint32_t e = expiries[std::min<std::size_t>(l.expiry, expiries.size() - 1)];
  const OptionKind k = l.kind == LegKind::Call ? OptionKind::Call : OptionKind::Put;
  return series_near(sim.sc, k, e, scaled(sim.sc.bars[sim.st.step].close, l.moneyness_bp), l.steps);
}

// Places the option legs on `expiries` (longs first, shorts last, as the strategy builder sends them).
Placed place_options(Sim& sim, const RefStructure& r, const std::vector<std::uint32_t>& expiries, Qty qty) {
  Placed out;
  for (const OrderSide side : {OrderSide::Buy, OrderSide::Sell}) {
    for (const RefLeg& l : r.legs) {
      if (l.kind == LegKind::Perp || l.side != side) continue;
      const OptionSeries* s = leg_series(sim, l, expiries);
      if (s == nullptr) return {{}, Reason::SeriesNotListed};
      const CommandResult res = sim.send(option_order(s->id, side, qty));
      if (!res.accepted) return {{}, res.reason};
      out.series.push_back(s->id);
    }
  }
  return out;
}

// Opens the structure at the current decision step: the option legs, then the perpetual leg.
Placed open_structure(Sim& sim, const RefStructure& r, Qty qty, std::uint32_t exit_bars) {
  const std::vector<std::uint32_t> expiries = expiries_after(sim.sc, sim.st.step, exit_bars);
  const bool has_options = std::any_of(r.legs.begin(), r.legs.end(), [](const RefLeg& l) { return l.kind != LegKind::Perp; });
  if (has_options && expiries.empty()) return {{}, Reason::SeriesNotListed};
  Placed out = has_options ? place_options(sim, r, expiries, qty) : Placed{};
  if (out.refused != Reason::None) return out;
  if (std::any_of(r.legs.begin(), r.legs.end(), [](const RefLeg& l) { return l.kind == LegKind::Perp; })) {
    const Qty units = Qty::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(qty.raw) * sim.sc.options.multiplier.raw, Qty::kScale, Rounding::TowardZero)));
    Command lev;
    lev.type = CommandType::SetLeverage;
    lev.leverage = run_leverage_cap(sim.sc, sim.st);  // the mission's cap, so the long fits beside the options
    sim.send(lev);
    const CommandResult res = sim.send(perp_order(sim, units, r.atr_stop));
    if (!res.accepted) return {{}, res.reason};
  }
  return out;
}

// Buys back or sells every option position of the structure still held (market orders).
void close_options(Sim& sim, const std::vector<std::uint32_t>& series) {
  for (const OptionPosition& p : std::vector<OptionPosition>(sim.st.option_positions)) {
    if (p.qty.raw == 0 || std::find(series.begin(), series.end(), p.series) == series.end()) continue;
    sim.send(option_order(p.series, p.qty.raw > 0 ? OrderSide::Sell : OrderSide::Buy, Qty::from_raw(p.qty.raw > 0 ? p.qty.raw : -p.qty.raw)));
  }
}

std::uint32_t first_expiry(const Scenario& sc, const std::vector<std::uint32_t>& series) {
  std::uint32_t e = 0;
  for (const std::uint32_t id : series) {
    const OptionSeries* s = find_series(sc, id);
    if (s != nullptr && (e == 0 || s->expiry_step < e)) e = s->expiry_step;
  }
  return e;
}

// Rolls the option legs before each expiry (or at its settlement for 0 bars) into the next one,
// re-centred on the close then, until no later expiry is listed.
void roll(Sim& sim, const RefStructure& r, Placed held, Qty qty) {
  RefStructure options_only = r;
  std::erase_if(options_only.legs, [](const RefLeg& l) { return l.kind == LegKind::Perp; });
  for (std::uint32_t e = first_expiry(sim.sc, held.series); e != 0 && sim.active();) {
    sim.run_to(e - r.bars);
    if (!sim.active() || expiries_after(sim.sc, sim.st.step, std::max(r.bars, e - sim.st.step)).empty()) return;
    if (r.bars > 0) close_options(sim, held.series);
    held = place_options(sim, options_only, expiries_after(sim.sc, sim.st.step, std::max(r.bars, e - sim.st.step)), qty);
    if (held.refused != Reason::None) return;
    e = first_expiry(sim.sc, held.series);
  }
}

void apply_exit(Sim& sim, const RefStructure& r, const Placed& held, Qty qty) {
  if (r.exit == ExitRule::CloseBefore) {
    sim.run_to(first_expiry(sim.sc, held.series) - r.bars);
    if (sim.active()) close_options(sim, held.series);
  } else if (r.exit == ExitRule::RollBefore) {
    roll(sim, r, held, qty);
  }
  sim.run_to(static_cast<std::uint32_t>(sim.sc.bars.size()));
}

std::uint32_t exit_step(const Sim& sim) {
  const bool open = !sim.st.position.flat() ||
                    std::any_of(sim.st.option_positions.begin(), sim.st.option_positions.end(), [](const OptionPosition& p) { return p.qty.raw != 0; });
  if (open) return sim.st.step;
  std::uint32_t last = 0;
  for (const Event& e : sim.st.events) {
    const bool closes = e.type == EventType::Fill || e.type == EventType::OptionSettlement || e.type == EventType::Liquidation;
    if (closes) last = e.step;
  }
  return last;
}

ComparisonRow play(const Scenario& sc, const RunState& played, const RefStructure& r, std::uint32_t entry, Qty qty) {
  Sim sim(sc, played);
  sim.run_to(entry);
  ComparisonRow row;
  row.name = r.name;
  row.entry_step = entry;
  const std::uint32_t exit_bars = r.exit == ExitRule::Hold ? 0 : r.bars;
  const Placed held = sim.active() ? open_structure(sim, r, qty, exit_bars) : Placed{{}, Reason::RunFinished};
  if (held.refused != Reason::None) {
    row.refused = held.refused;
    return row;
  }
  apply_exit(sim, r, held, qty);
  row.exit_step = exit_step(sim);
  row.net_pnl = Money::from_raw(account_view(sc, sim.st).equity.raw - run_initial_balance(sc, sim.st).raw);
  row.max_drawdown = sim.st.metrics.max_drawdown;
  row.margin_calls = static_cast<std::uint32_t>(
      std::count_if(sim.st.events.begin(), sim.st.events.end(), [](const Event& e) { return e.type == EventType::MarginCall; }));
  return row;
}

// The player's first option fill: the decision step of its order and the order's quantity.
std::optional<std::pair<std::uint32_t, Qty>> first_option_entry(const RunState& st) {
  for (const Event& e : st.events) {
    if (e.type != EventType::Fill || e.series == 0) continue;
    for (const Order& o : st.orders) {
      if (o.id == e.order_id) return std::make_pair(o.placed_step, o.qty);
    }
    return std::make_pair(e.step - 1, e.qty);
  }
  return std::nullopt;
}

std::uint32_t reference_step(const Scenario& sc, const RunState& fresh, EntryRule rule) {
  const std::uint32_t first = fresh.step;
  if (rule == EntryRule::AfterHeadline && !sc.news.empty()) return std::clamp(sc.news.front().release_step, first, last_decision_step(sc));
  return first;
}

}  // namespace

std::optional<StrategyComparison> strategy_comparison(const Scenario& sc, const RunState& st) {
  const std::vector<ComparisonDef> defs = definitions();
  const auto def = std::find_if(defs.begin(), defs.end(), [&sc](const ComparisonDef& d) { return d.mission == sc.id; });
  if (def == defs.end() || st.status == RunStatus::Active) return std::nullopt;
  StrategyComparison out;
  const auto mine = first_option_entry(st);
  out.from_player = mine.has_value();
  out.entry_step = mine ? std::min(mine->first, last_decision_step(sc)) : reference_step(sc, start_run(sc, st.seed, st.options), def->entry);
  out.qty = mine ? mine->second : Qty::from_raw(kReferenceContracts * Qty::kScale);
  for (const RefStructure& r : def->rows) out.rows.push_back(play(sc, st, r, out.entry_step, out.qty));
  return out;
}

}  // namespace tradelab
