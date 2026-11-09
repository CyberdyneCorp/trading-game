// Strategy comparison of the strategy missions (add-academy-and-strategies design D8, scoring v13;
// m18-m22 since scoring v14, add-practice-and-advanced-strategies D13; report-only). After a run ends, each mission's reference structures are played on a fresh copy of
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
// AfterRelease, BeforeRelease (v14): everything closed `bars` decisions after or before the first release.
enum class ExitRule : std::uint8_t { Hold, CloseBefore, RollBefore, AfterRelease, BeforeRelease };
// Announcement and BeforeRelease (v14) anchor a row at the first headline's announcement or `bars`
// decisions before its release, whatever the player did (m20 compares timings).
enum class EntryRule : std::uint8_t { FirstDecision, AfterHeadline, Announcement, BeforeRelease };
// v14 delta_hedge: after entry, a perpetual order brings the net delta to zero, rounded toward zero to the
// lot, at every `param`-th close (EveryBars) or whenever |delta| exceeds `param`% of the contracts (Band).
enum class HedgeRule : std::uint8_t { None, EveryBars, Band };

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
  std::uint32_t bars = 0;  // CloseBefore / RollBefore: decision bars before the expiry; AfterRelease: after it
  bool atr_stop = false;   // the perpetual leg carries a stop 2 x ATR(14) below the entry close
  std::int32_t leverage = 0;                  // v14: the perpetual leg's leverage (0 = the mission's cap)
  HedgeRule hedge = HedgeRule::None;          // v14
  std::uint32_t hedge_param = 0;              // v14: bars (EveryBars) or percent of the contracts (Band)
  EntryRule entry = EntryRule::FirstDecision;  // v14: the row's own anchor (FirstDecision = the comparison's)
  std::uint32_t entry_bars = 0;               // v14: BeforeRelease: decisions before the release
};

// v14 managed_exit (m22): a perpetual trade sized by its stop, played by play_trade instead of legs.
enum class TradeExit : std::uint8_t { None, AllOut2R, Managed, Hold };

struct ComparisonDef {
  std::string_view mission;
  EntryRule entry = EntryRule::FirstDecision;
  std::vector<RefStructure> rows;
  bool trades = false;  // v14: the rows are TradeExit rules (m22), in the order AllOut2R, Managed, Hold
};

constexpr std::int64_t kReferenceContracts = 10;  // quantity when the player traded no option
constexpr std::uint32_t kAtrPeriod = 14;

constexpr RefLeg perp_long() { return {}; }
constexpr RefLeg perp_short() { return {LegKind::Perp, OrderSide::Sell, 10000, 0, 0}; }
constexpr std::uint32_t kLastExpiry = 99;  // clamped to the last expiry still far enough
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

// Scoring v14 (add-practice-and-advanced-strategies D13): m18-m22.
std::vector<ComparisonDef> definitions_v14() {
  using enum ComparisonStrategy;
  constexpr OrderSide kBuy = OrderSide::Buy;
  constexpr OrderSide kSell = OrderSide::Sell;
  constexpr LegKind kCall = LegKind::Call;
  constexpr LegKind kPut = LegKind::Put;
  const std::vector<RefLeg> straddle = {option(kCall, kBuy, 10000, 0), option(kPut, kBuy, 10000, 0)};
  const std::vector<RefLeg> far_straddle = {option(kCall, kBuy, 10000, 0, kLastExpiry), option(kPut, kBuy, 10000, 0, kLastExpiry)};
  const std::vector<RefLeg> short_straddle = {option(kCall, kSell, 10000, 0), option(kPut, kSell, 10000, 0)};
  const std::vector<RefLeg> carry = {option(kCall, kBuy, 10000, 0, kLastExpiry), option(kPut, kSell, 10000, 0, kLastExpiry), perp_short()};
  const RefStructure base{};
  RefStructure every4 = base;
  every4.hedge = HedgeRule::EveryBars;
  every4.hedge_param = 4;
  RefStructure band = base;
  band.hedge = HedgeRule::Band;
  band.hedge_param = 40;
  const auto row = [](ComparisonStrategy n, std::vector<RefLeg> legs, RefStructure r) {
    r.name = n;
    r.legs = std::move(legs);
    return r;
  };
  const auto at = [](ExitRule x, std::uint32_t bars, EntryRule e, std::uint32_t entry_bars) {
    RefStructure r;
    r.exit = x;
    r.bars = bars;
    r.entry = e;
    r.entry_bars = entry_bars;
    return r;
  };
  const auto lev = [](std::int32_t x) {
    RefStructure r;
    r.leverage = x;
    return r;
  };
  RefStructure near_close;
  near_close.exit = ExitRule::CloseBefore;
  return {
      {"m18-gamma-scalp",
       EntryRule::FirstDecision,
       {row(UnhedgedStraddle, straddle, base), row(HedgedEvery4, straddle, every4), row(HedgedBand, straddle, band)}},
      {"m19-funding-carry",
       EntryRule::FirstDecision,
       {row(UnhedgedShort, {perp_short()}, lev(5)), row(HedgedCarry5x, carry, lev(5)), row(HedgedCarry2x, carry, lev(2))}},
      {"m20-event-vol",
       EntryRule::FirstDecision,
       {row(EarlySellBefore, far_straddle, at(ExitRule::BeforeRelease, 1, EntryRule::Announcement, 0)),
        row(EarlyHoldThrough, far_straddle, at(ExitRule::AfterRelease, 3, EntryRule::Announcement, 0)),
        row(LateHoldThrough, straddle, at(ExitRule::AfterRelease, 3, EntryRule::BeforeRelease, 1)),
        row(ShortStraddleThrough, short_straddle, at(ExitRule::AfterRelease, 3, EntryRule::BeforeRelease, 1))}},
      {"m21-calendar",
       EntryRule::FirstDecision,
       {row(CalendarAtm, {option(kCall, kSell, 10000, 0), option(kCall, kBuy, 10000, 0, 1)}, near_close),
        row(DiagonalUp, {option(kCall, kSell, 10000, 0), option(kCall, kBuy, 10000, 1, 1)}, near_close),
        row(ShortStraddleNear, short_straddle, base)}},
      {"m22-scale-manage", EntryRule::AfterHeadline, {row(AllOut2R, {}, base), row(ManagedExit, {}, base), row(HoldInitialStop, {}, base)}, true},
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
Command perp_order(const Sim& sim, OrderSide side, Qty units, bool atr_stop) {
  Command c;
  c.order_type = OrderType::Market;
  c.side = side;
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
  const auto perp = std::find_if(r.legs.begin(), r.legs.end(), [](const RefLeg& l) { return l.kind == LegKind::Perp; });
  if (perp != r.legs.end()) {
    const Qty units = Qty::from_raw(static_cast<std::int64_t>(div_round(static_cast<i128>(qty.raw) * sim.sc.options.multiplier.raw, Qty::kScale, Rounding::TowardZero)));
    Command lev;
    lev.type = CommandType::SetLeverage;
    // The mission's cap, so the long fits beside the options (v14: a row may declare its own leverage).
    lev.leverage = r.leverage > 0 ? r.leverage : run_leverage_cap(sim.sc, sim.st);
    sim.send(lev);
    const CommandResult res = sim.send(perp_order(sim, perp->side, units, r.atr_stop));
    if (!res.accepted) return {{}, res.reason};
  }
  return out;
}

// ---- v14: delta_hedge, release anchors ----

void close_options(Sim& sim, const std::vector<std::uint32_t>& series);
std::uint32_t first_expiry(const Scenario& sc, const std::vector<std::uint32_t>& series);

// Net delta in perpetual units at the close: options at the engine's quote, plus the perpetual.
i128 net_delta_raw(const Sim& sim) {
  i128 d = sim.st.position.qty.raw;
  const Price f = sim.sc.bars[sim.st.step].close;
  for (const OptionPosition& p : sim.st.option_positions) {
    const OptionSeries* s = find_series(sim.sc, p.series);
    if (s == nullptr || s->expiry_step <= sim.st.step) continue;
    const i128 units = div_round(static_cast<i128>(p.qty.raw) * sim.sc.options.multiplier.raw, Qty::kScale, Rounding::TowardZero);
    d += div_round(units * quote(sim.sc, *s, sim.st.step, f).delta.raw, Rate::kScale, Rounding::TowardZero);
  }
  return d;
}

// A perpetual market order bringing the net delta to zero, rounded toward zero to the lot.
void hedge_now(Sim& sim) {
  const i128 d = net_delta_raw(sim);
  const std::int64_t lot = sim.sc.contract.lot_size.raw;
  const auto q = static_cast<std::int64_t>((d < 0 ? -d : d) / lot * lot);
  if (q < sim.sc.contract.min_qty.raw) return;
  Command c;
  c.order_type = OrderType::Market;
  c.side = d > 0 ? OrderSide::Sell : OrderSide::Buy;
  c.qty = Qty::from_raw(q);
  sim.send(c);
}

bool hedge_due(const Sim& sim, const RefStructure& r, std::uint32_t since, Qty qty) {
  if (r.hedge == HedgeRule::EveryBars) return r.hedge_param > 0 && (sim.st.step - since) % r.hedge_param == 0;
  const i128 d = net_delta_raw(sim);
  const i128 band = div_round(static_cast<i128>(qty.raw) * sim.sc.options.multiplier.raw * r.hedge_param, static_cast<i128>(Qty::kScale) * 100,
                             Rounding::TowardZero);
  return (d < 0 ? -d : d) > band;
}

// Hedges at each close until the structure's first expiry, then closes the hedge at that expiry.
void run_hedged(Sim& sim, const RefStructure& r, const Placed& held, Qty qty) {
  const std::uint32_t since = sim.st.step;
  const std::uint32_t expiry = first_expiry(sim.sc, held.series);
  sim.run_to(since + 1);
  while (sim.active() && sim.st.step < expiry) {
    if (hedge_due(sim, r, since, qty)) hedge_now(sim);
    sim.run_to(sim.st.step + 1);
  }
  if (sim.active() && !sim.st.position.flat()) {
    Command c;
    c.type = CommandType::ClosePosition;
    sim.send(c);
  }
}

std::uint32_t release_step(const Scenario& sc) { return sc.news.empty() ? last_decision_step(sc) : sc.news.front().release_step; }

// Closes every option of the structure and the perpetual at the release anchor.
void exit_at_release(Sim& sim, const RefStructure& r, const Placed& held) {
  const std::uint32_t at = r.exit == ExitRule::AfterRelease ? release_step(sim.sc) + r.bars : release_step(sim.sc) - r.bars;
  sim.run_to(std::min(at, last_decision_step(sim.sc)));
  if (!sim.active()) return;
  close_options(sim, held.series);
  if (!sim.st.position.flat()) {
    Command c;
    c.type = CommandType::ClosePosition;
    sim.send(c);
  }
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
  if (r.hedge != HedgeRule::None) {
    run_hedged(sim, r, held, qty);
  } else if (r.exit == ExitRule::AfterRelease || r.exit == ExitRule::BeforeRelease) {
    exit_at_release(sim, r, held);
  } else if (r.exit == ExitRule::CloseBefore) {
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

// ---- v14 managed_exit (m22): one perpetual trade sized by its stop ----

constexpr std::uint32_t kTradeDelay = 3;    // decisions after the headline
constexpr std::int64_t kTradeRiskBp = 100;  // 1% of equity at risk

struct TradePlan {
  OrderSide side = OrderSide::Buy;
  std::int64_t entry = 0;  // the entry decision's close (raw)
  std::int64_t risk = 0;   // 2 x ATR(14) per unit (raw)
  Qty qty;
};

std::int64_t to_tick(const Scenario& sc, std::int64_t px) { return px / sc.contract.tick_size.raw * sc.contract.tick_size.raw; }

std::optional<TradePlan> trade_plan(const Sim& sim, std::uint32_t headline) {
  const std::optional<Price> a = atr(sim.sc, sim.st.step, kAtrPeriod)[sim.st.step];
  if (!a || a->raw <= 0) return std::nullopt;
  TradePlan t;
  t.entry = sim.sc.bars[sim.st.step].close.raw;
  t.side = t.entry >= sim.sc.bars[headline].close.raw ? OrderSide::Buy : OrderSide::Sell;
  t.risk = 2 * a->raw;
  const i128 budget = div_round(static_cast<i128>(account_view(sim.sc, sim.st).equity.raw) * kTradeRiskBp, 10'000, Rounding::Floor);
  const std::int64_t lot = sim.sc.contract.lot_size.raw;
  t.qty = Qty::from_raw(static_cast<std::int64_t>(budget * Qty::kScale / t.risk / lot * lot));
  if (t.qty.raw < sim.sc.contract.min_qty.raw) return std::nullopt;
  return t;
}

std::int64_t sgn(const TradePlan& t) { return t.side == OrderSide::Buy ? 1 : -1; }

Command reduce_only(OrderType type, OrderSide side, Qty q, std::int64_t px) {
  Command c;
  c.order_type = type;
  c.side = side;
  c.qty = q;
  c.price = Price::from_raw(px);
  c.reduce_only = true;
  return c;
}

// After the half at +1R fills: the stop to breakeven, then a trailing stop 2 x ATR behind the best close.
void manage_rest(Sim& sim, const TradePlan& t, std::uint32_t stop_id) {
  const OrderSide out = t.side == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy;
  while (sim.active() && !sim.st.position.flat() && std::abs(sim.st.position.qty.raw) == t.qty.raw) sim.run_to(sim.st.step + 1);
  if (!sim.active() || sim.st.position.flat()) return;
  Command move;
  move.type = CommandType::MoveOrder;
  move.order_id = stop_id;
  move.price = Price::from_raw(to_tick(sim.sc, t.entry));
  sim.send(move);
  const std::optional<Price> a = atr(sim.sc, sim.st.step, kAtrPeriod)[sim.st.step];
  Command trail = reduce_only(OrderType::TrailingStop, out, Qty::from_raw(std::abs(sim.st.position.qty.raw)), 0);
  trail.price = Price{};
  trail.trail_distance = Price::from_raw(to_tick(sim.sc, 2 * (a ? a->raw : t.risk / 2)));
  sim.send(trail);
}

void play_trade(Sim& sim, const TradePlan& t, TradeExit x) {
  const OrderSide out = t.side == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy;
  Command c;
  c.order_type = OrderType::Market;
  c.side = t.side;
  c.qty = t.qty;
  c.attach_stop = Price::from_raw(to_tick(sim.sc, t.entry - sgn(t) * t.risk));
  if (x == TradeExit::AllOut2R) c.attach_tp = Price::from_raw(to_tick(sim.sc, t.entry + sgn(t) * 2 * t.risk));
  if (!sim.send(c).accepted) return;
  sim.run_to(sim.st.step + 1);
  if (x != TradeExit::Managed || sim.st.position.flat()) return;
  std::uint32_t stop_id = 0;
  for (const Order& o : sim.st.orders) {
    if (o.active() && o.reduce_only && o.type == OrderType::StopMarket) stop_id = o.id;
  }
  const Qty half = Qty::from_raw(t.qty.raw / 2 / sim.sc.contract.lot_size.raw * sim.sc.contract.lot_size.raw);
  sim.send(reduce_only(OrderType::Limit, out, half, to_tick(sim.sc, t.entry + sgn(t) * t.risk)));
  manage_rest(sim, t, stop_id);
}

ComparisonRow play_managed(const Scenario& sc, const RunState& played, ComparisonStrategy name, TradeExit x) {
  Sim sim(sc, played);
  const std::uint32_t headline = std::clamp(release_step(sc), sim.st.step, last_decision_step(sc));
  sim.run_to(std::min(headline + kTradeDelay, last_decision_step(sc)));
  ComparisonRow row;
  row.name = name;
  row.entry_step = sim.st.step;
  const std::optional<TradePlan> t = sim.active() ? trade_plan(sim, headline) : std::nullopt;
  if (!t) {
    row.refused = sim.active() ? Reason::InvalidQuantity : Reason::RunFinished;
    return row;
  }
  play_trade(sim, *t, x);
  sim.run_to(static_cast<std::uint32_t>(sc.bars.size()));
  row.exit_step = exit_step(sim);
  row.net_pnl = Money::from_raw(account_view(sc, sim.st).equity.raw - run_initial_balance(sc, sim.st).raw);
  row.max_drawdown = sim.st.metrics.max_drawdown;
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

// v14: a row's own timing anchor (m20), else the comparison's entry step.
std::uint32_t row_entry(const Scenario& sc, const RunState& played, const RefStructure& r, std::uint32_t entry) {
  if (sc.news.empty() || (r.entry != EntryRule::Announcement && r.entry != EntryRule::BeforeRelease)) return entry;
  const NewsItem& n = sc.news.front();
  const std::uint32_t at = r.entry == EntryRule::Announcement ? n.release_step - n.announce_lead : n.release_step - r.entry_bars;
  return std::clamp(at, start_run(sc, played.seed, played.options).step, last_decision_step(sc));
}

std::vector<ComparisonDef> all_definitions() {
  std::vector<ComparisonDef> defs = definitions();
  std::vector<ComparisonDef> more = definitions_v14();
  defs.insert(defs.end(), more.begin(), more.end());
  return defs;
}

constexpr TradeExit trade_exit(ComparisonStrategy n) {
  if (n == ComparisonStrategy::AllOut2R) return TradeExit::AllOut2R;
  return n == ComparisonStrategy::ManagedExit ? TradeExit::Managed : TradeExit::Hold;
}

}  // namespace

std::optional<StrategyComparison> strategy_comparison(const Scenario& sc, const RunState& st) {
  const std::vector<ComparisonDef> defs = all_definitions();
  const auto def = std::find_if(defs.begin(), defs.end(), [&sc](const ComparisonDef& d) { return d.mission == sc.id; });
  if (def == defs.end() || st.status == RunStatus::Active) return std::nullopt;
  StrategyComparison out;
  const auto mine = first_option_entry(st);
  out.from_player = mine.has_value();
  out.entry_step = mine ? std::min(mine->first, last_decision_step(sc)) : reference_step(sc, start_run(sc, st.seed, st.options), def->entry);
  out.qty = mine ? mine->second : Qty::from_raw(kReferenceContracts * Qty::kScale);
  for (const RefStructure& r : def->rows) {
    if (def->trades) out.rows.push_back(play_managed(sc, st, r.name, trade_exit(r.name)));
    else out.rows.push_back(play(sc, st, r, row_entry(sc, st, r, out.entry_step), out.qty));
  }
  return out;
}

}  // namespace tradelab
