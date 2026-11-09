// Coach review (add-practice-and-advanced-strategies design D17, report-only). A finished run is walked
// decision by decision: at step s the rules read only what had happened by the close of s (the events
// recorded up to s, the orders placed by s with their triggers and working state at s, the plans
// recorded by s, the closed trades), the bars up to s and the releases announced by s (without perks).
// The run handle keeps no command list (a checkpoint carries none), so the walk reads the recorded
// state as of each step instead of replaying commands; it never changes the run, its log or a digest.

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "bias_rules.hpp"
#include "tradelab/indicators.hpp"
#include "tradelab/scoring.hpp"
#include "tradelab/whatif.hpp"

namespace tradelab {
namespace {

using namespace bias;  // NOLINT(google-build-using-namespace): the shared bias helpers
using enum CoachMomentCode;

constexpr std::uint32_t kStructureBars = 10;
constexpr std::uint32_t kRevengeBars = 3;
constexpr std::int64_t kRevengeRatioBp = 15'000;
constexpr std::uint32_t kFomoCloses = 4;
constexpr std::int64_t kFomoExtension = 200;  // hundredths of ATR
constexpr std::int64_t kOneR = kBp;

// An order with the step it stopped working and its trigger after each update.
struct OrderFacts {
  const Order* order = nullptr;
  std::uint32_t end = UINT32_MAX;
  std::vector<std::pair<std::uint32_t, Price>> triggers;  // (step, requested) of each order update
};

std::vector<OrderFacts> order_facts(const RunState& st) {
  std::vector<OrderFacts> out;
  out.reserve(st.orders.size());
  for (const Order& o : st.orders) out.push_back({&o, o.active() ? UINT32_MAX : o.placed_step, {}});
  for (const Event& e : st.events) {
    if (e.type != EventType::OrderUpdate && e.type != EventType::Fill) continue;
    auto it = std::find_if(out.begin(), out.end(), [&e](const OrderFacts& f) { return f.order->id == e.order_id; });
    if (it == out.end()) continue;
    if (!it->order->active()) it->end = e.step;
    if (e.type == EventType::OrderUpdate) it->triggers.emplace_back(e.step, e.requested);
  }
  return out;
}

bool working_at(const OrderFacts& f, std::uint32_t s) { return f.order->placed_step <= s && f.end > s; }

std::optional<Price> trigger_at(const OrderFacts& f, std::uint32_t s) {
  std::optional<Price> out;
  for (const auto& [step, price] : f.triggers) {
    if (step <= s) out = price;
  }
  return out;
}

bool exit_order_of(const Order& o, OrderSide exit) { return o.series == 0 && o.reduce_only && o.side == exit; }

std::int64_t raw_or_zero(const std::optional<Price>& p) { return p ? p->raw : 0; }

class Review {
 public:
  Review(const Scenario& sc, const RunState& st)
      : sc_(sc), st_(st), reads_{atr(sc, st.step, kAtrPeriod), sma(sc, st.step, kSmaPeriod)}, facts_(order_facts(st)),
        trades_(score_run(sc, st).trades) {
    shadow_.st.wallet = run_initial_balance(sc, st);
    shadow_.st.options = st.options;
  }

  std::vector<CoachMoment> run() {
    for (std::uint32_t s = sc_.mission.warmup_bars; s <= st_.step && s < sc_.bars.size(); ++s) at_step(s);
    return std::move(out_);
  }

 private:
  const Scenario& sc_;
  const RunState& st_;
  Reads reads_;
  std::vector<OrderFacts> facts_;
  std::vector<TradeRow> trades_;
  Book book_;
  Shadow shadow_;
  std::size_t next_ = 0;
  bool grew_ = false;            // the position grew during this bar
  bool warned_no_stop_ = false;  // entry_without_stop already said for this position
  bool traded_ = false;          // any perpetual or option fill so far
  Money episode_plan_;           // planned risk of the position's entries (an add adds its risk)
  Money first_plan_;             // the first entry's planned risk
  std::vector<std::uint32_t> episode_orders_;
  std::vector<std::uint32_t> revenge_trades_;
  std::vector<RiskLimitKind> breached_;
  std::vector<std::pair<std::uint32_t, Price>> last_trigger_;  // order id -> its trigger after the last update
  std::vector<CoachMoment> out_;

  void add(std::uint32_t s, CoachMomentCode code, const CoachEvidence& ev) {
    const bool good = code == CutAtInvalidation || code == StopToBreakevenAfter1r || code == PartialAtTarget || code == StayedFlatBeforeRelease;
    out_.push_back({s, code, good ? FindingTone::Good : FindingTone::Warn, ev});
  }

  [[nodiscard]] Price close(std::uint32_t s) const { return sc_.bars[s].close; }
  [[nodiscard]] OrderSide exit_side() const { return book_.qty > 0 ? OrderSide::Sell : OrderSide::Buy; }

  [[nodiscard]] const OrderFacts* facts_of(std::uint32_t id) const {
    auto it = std::find_if(facts_.begin(), facts_.end(), [id](const OrderFacts& f) { return f.order->id == id; });
    return it == facts_.end() ? nullptr : &*it;
  }

  void at_step(std::uint32_t s) {
    grew_ = false;
    for (; next_ < st_.events.size() && st_.events[next_].step <= s; ++next_) on_event(st_.events[next_], s);
    shadow_.st.step = s;
    for (const OrderFacts& f : facts_) {
      if (f.order->placed_step == s && entry_order(f.order) && !holds_options(st_, s)) on_entry(*f.order, s);
    }
    after_decisions(s);
  }

  // ---- events of the bar (fills, stop moves) ----

  void on_event(const Event& e, std::uint32_t s) {
    shadow_.apply(sc_, e);
    if (e.type == EventType::Fill) traded_ = true;
    if (e.type == EventType::Liquidation) reset_position();
    else if (perp_fill(e)) on_perp_fill(e);
    else if (e.type == EventType::OrderUpdate) on_update(e, s);
  }

  void on_update(const Event& e, std::uint32_t s) {
    auto it = std::find_if(last_trigger_.begin(), last_trigger_.end(), [&e](const auto& p) { return p.first == e.order_id; });
    if (it == last_trigger_.end()) {
      last_trigger_.emplace_back(e.order_id, e.requested);
      return;
    }
    if (e.reason == Reason::OrderMoved && it->second != e.requested) on_move(e, s, it->second);
    it->second = e.requested;
  }

  void reset_position() {
    book_ = Book{};
    warned_no_stop_ = false;
    episode_plan_ = Money{};
    first_plan_ = Money{};
    episode_orders_.clear();
  }

  void count_plan(std::uint32_t order_id) {
    if (std::find(episode_orders_.begin(), episode_orders_.end(), order_id) != episode_orders_.end()) return;
    const PlanRecord* p = plan_of_order(st_, order_id);
    const Money risk = p != nullptr && !p->skipped ? p->planned_risk : Money{};
    if (episode_orders_.empty()) first_plan_ = risk;
    episode_orders_.push_back(order_id);
    episode_plan_ = Money::from_raw(episode_plan_.raw + risk.raw);
  }

  void on_perp_fill(const Event& e) {
    const Book before = book_;
    const bool reduces = before.qty != 0 && (before.qty > 0) != (e.side == OrderSide::Buy);
    const std::int64_t grew = book_.apply(e.side, e.qty, e.price);
    if (reduces && book_.qty != 0 && (book_.qty > 0) == (before.qty > 0)) on_partial(before, e);
    if (reduces && book_.qty == 0) reset_position();
    if (grew == 0) return;
    if (reduces) {  // a flip opens a new position
      const Book flipped = book_;
      reset_position();
      book_ = flipped;
    }
    grew_ = true;
    count_plan(e.order_id);
  }

  void on_partial(const Book& before, const Event& e) {
    if (episode_plan_.raw <= 0) return;
    const std::int64_t dir = before.qty > 0 ? 1 : -1;
    const i128 profit = static_cast<i128>(dir) * (e.price.raw - before.entry.raw) * abs64(before.qty) / Qty::kScale;
    const std::int64_t r = ratio_bp(profit, episode_plan_.raw);
    if (r < kOneR) return;
    CoachEvidence ev;
    ev.qty = Qty::from_raw(std::min(e.qty.raw, abs64(before.qty)));
    ev.price = e.price;
    ev.entry = before.entry;
    ev.planned_risk = episode_plan_;
    ev.profit_r_bp = r;
    add(e.step, PartialAtTarget, ev);
  }

  void on_move(const Event& e, std::uint32_t s, Price from) {
    const OrderFacts* f = facts_of(e.order_id);
    if (f == nullptr || book_.qty == 0 || !exit_order_of(*f->order, exit_side()) || f->order->type != OrderType::StopMarket) return;
    const std::int64_t dir = book_.qty > 0 ? 1 : -1;  // a long's stop moves up toward the price
    CoachEvidence ev;
    ev.from = from;
    ev.to = e.requested;
    ev.entry = book_.entry;
    if (dir * (e.requested.raw - from.raw) < 0) return add(s, StopMovedAway, ev);
    const bool past_entry = dir * (e.requested.raw - book_.entry.raw) >= 0 && dir * (from.raw - book_.entry.raw) < 0;
    if (!past_entry || episode_plan_.raw <= 0) return;
    const i128 open = static_cast<i128>(dir) * (close(s).raw - book_.entry.raw) * abs64(book_.qty) / Qty::kScale;
    const std::int64_t r = ratio_bp(open, episode_plan_.raw);
    if (r < kOneR) return;
    ev.planned_risk = episode_plan_;
    ev.profit_r_bp = r;
    add(s, StopToBreakevenAfter1r, ev);
  }

  // ---- entries decided at this step ----

  void on_entry(const Order& o, std::uint32_t s) {
    const bool same_side = book_.qty != 0 && (book_.qty > 0) == (o.side == OrderSide::Buy);
    const bool opens = book_.qty == 0 || same_side;
    revenge(o, s);
    if (opens) fomo(o, s);
    if (opens) risk_budget(o, s);
    if (!same_side) return;
    against_structure(o, s);
    averaging(o, s);
  }

  // The entry's protective stop as of s: its bracket stop, else the nearest reduce-only stop working.
  [[nodiscard]] Price stop_for(const Order& o, std::uint32_t s) const {
    if (o.bracket_stop.raw > 0) return o.bracket_stop;
    const OrderSide exit = o.side == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy;
    std::int64_t best = 0;
    for (const OrderFacts& f : facts_) {
      if (!exit_order_of(*f.order, exit) || !is_stop(*f.order) || !working_at(f, s)) continue;
      const std::int64_t p = raw_or_zero(trigger_at(f, s));
      const bool nearer = best == 0 || (exit == OrderSide::Sell ? p > best : p < best);
      if (p > 0 && nearer) best = p;
    }
    return Price::from_raw(best);
  }

  [[nodiscard]] Price fill_estimate(const Order& o, std::uint32_t s) const { return o.type == OrderType::Market ? close(s) : o.price; }

  void revenge(const Order& o, std::uint32_t s) {
    for (const TradeRow& t : trades_) {
      if (t.series != 0 || !t.exit_step || t.net.raw >= 0 || *t.exit_step > s || s > *t.exit_step + kRevengeBars) continue;
      if (std::find(revenge_trades_.begin(), revenge_trades_.end(), t.n) != revenge_trades_.end()) continue;
      const PlanRecord* plan = plan_of_order(st_, o.id);
      const bool planned = plan != nullptr && !plan->skipped;
      const bool by_risk = t.planned_risk.raw > 0 && planned && plan->planned_risk.raw > 0;
      const std::int64_t ratio = by_risk ? ratio_bp(plan->planned_risk.raw, t.planned_risk.raw) : ratio_bp(o.qty.raw, t.qty.raw);
      if (planned && ratio < kRevengeRatioBp) continue;
      revenge_trades_.push_back(t.n);
      CoachEvidence ev;
      ev.trade = t.n;
      ev.exit_step = *t.exit_step;
      ev.loss = Money::from_raw(-t.net.raw);
      ev.size_ratio_bp = ratio;
      ev.planned = planned;
      return add(s, RevengeTrade, ev);
    }
  }

  void fomo(const Order& o, std::uint32_t s) {
    const std::int64_t dir = dir_of(o.side);
    const std::uint32_t closes = closes_in_direction(sc_, s, dir);
    const std::optional<Price> a = reads_.atr[s];
    const std::optional<Price> m = reads_.sma[s];
    if (closes < kFomoCloses || !a || !m || a->raw <= 0) return;
    const std::int64_t c = close(s).raw;
    const auto ext = static_cast<std::int64_t>(div_round(static_cast<i128>(dir) * (c - m->raw) * 100, a->raw, Rounding::HalfEven));
    const Price stop = stop_for(o, s);
    if (ext <= kFomoExtension || (stop.raw != 0 && dir * (c - stop.raw) <= 2 * a->raw)) return;
    CoachEvidence ev;
    ev.closes = closes;
    ev.extension_atr = ext;
    ev.close = close(s);
    ev.sma = *m;
    ev.stop = stop;
    add(s, FomoEntry, ev);
  }

  void risk_budget(const Order& o, std::uint32_t s) {
    const Price stop = stop_for(o, s);
    if (stop.raw == 0) return;
    Book after = book_;
    after.apply(o.side, o.qty, fill_estimate(o, s));
    const Money risk = risk_to(after.entry, stop, abs64(after.qty));
    const Money budget = Money::from_raw(run_initial_balance(sc_, st_).raw / 50);  // 2%, the score's oversized rule
    if (risk <= budget) return;
    CoachEvidence ev;
    ev.qty = o.qty;
    ev.stop = stop;
    ev.risk = risk;
    ev.budget = budget;
    add(s, RiskAboveBudget, ev);
  }

  void against_structure(const Order& o, std::uint32_t s) {
    const std::optional<Price> m = reads_.sma[s];
    if (!m || s < kStructureBars) return;
    const bool is_long = o.side == OrderSide::Buy;
    std::int64_t level = is_long ? sc_.bars[s - 1].low.raw : sc_.bars[s - 1].high.raw;
    for (std::uint32_t b = s - kStructureBars; b < s; ++b) {
      level = is_long ? std::min(level, sc_.bars[b].low.raw) : std::max(level, sc_.bars[b].high.raw);
    }
    const std::int64_t c = close(s).raw;
    const bool broke = is_long ? c < m->raw && c < level : c > m->raw && c > level;
    if (!broke) return;
    CoachEvidence ev;
    ev.qty = o.qty;
    ev.close = close(s);
    ev.sma = *m;
    ev.level = Price::from_raw(level);
    add(s, AddedAgainstStructure, ev);
  }

  void averaging(const Order& o, std::uint32_t s) {
    const std::int64_t dir = book_.qty > 0 ? 1 : -1;
    const Price px = fill_estimate(o, s);
    const std::int64_t q = abs64(book_.qty);
    const i128 unreal = static_cast<i128>(dir) * (close(s).raw - book_.entry.raw) * q / Qty::kScale;
    if (dir * (px.raw - book_.entry.raw) >= 0 || unreal >= 0) return;
    Book after = book_;
    after.apply(o.side, o.qty, px);
    const Price stop = stop_for(o, s);
    CoachEvidence ev;
    ev.avg_before = book_.entry;
    ev.avg_after = after.entry;
    ev.stop = stop;
    ev.loss = Money::from_raw(static_cast<std::int64_t>(-unreal));
    ev.risk_before = risk_to(book_.entry, stop, q);
    ev.risk_after = risk_to(after.entry, stop, abs64(after.qty));
    ev.plan_risk = first_plan_;
    const PlanRecord* plan = plan_of_order(st_, o.id);
    ev.planned = plan != nullptr && !plan->skipped;
    const Money limit = first_plan_.raw > 0 ? first_plan_ : *ev.risk_before;
    if (stop.raw != 0 && *ev.risk_after <= limit) return;
    add(s, AveragingDown, ev);
  }

  // ---- the position after this step's decisions ----

  // A stop or trailing stop on the exit side working after the decision, or a market close sent now.
  [[nodiscard]] bool protected_at(std::uint32_t s) const {
    const OrderSide exit = exit_side();
    return std::any_of(facts_.begin(), facts_.end(), [&](const OrderFacts& f) {
      const bool closing = f.order->type == OrderType::Market && f.order->placed_step == s;
      return exit_order_of(*f.order, exit) && working_at(f, s) && (is_stop(*f.order) || closing);
    });
  }

  [[nodiscard]] bool entry_working(std::uint32_t s) const {
    return std::any_of(facts_.begin(), facts_.end(), [s](const OrderFacts& f) { return entry_order(f.order) && working_at(f, s); });
  }

  // The release announced by s (no perks) on the next bar, if any.
  [[nodiscard]] std::optional<std::uint32_t> release_next(std::uint32_t s) const {
    for (const NewsItem& n : sc_.news) {
      if (n.announce_lead > 0 && n.release_step == s + 1 && s + n.announce_lead >= n.release_step) return n.release_step;
    }
    return std::nullopt;
  }

  void after_decisions(std::uint32_t s) {
    const bool open = book_.qty != 0;
    const bool hedging = holds_options(st_, s);
    if (open && grew_ && !warned_no_stop_ && !hedging && !protected_at(s)) {
      warned_no_stop_ = true;
      CoachEvidence ev;
      ev.qty = Qty::from_raw(abs64(book_.qty));
      ev.entry = book_.entry;
      ev.close = close(s);
      add(s, EntryWithoutStop, ev);
    }
    const std::optional<RiskBlock> risk = holdings_risk();
    release(s, risk);
    limits(s, risk);
    cuts(s);
  }

  [[nodiscard]] std::optional<RiskBlock> holdings_risk() const {
    if (shadow_.st.position.flat() && shadow_.st.option_positions.empty()) return std::nullopt;
    WhatIfRequest req;
    req.holdings = true;
    return run_option_whatif(sc_, shadow_.st, req).risk;
  }

  static const RiskLimitCheck* vega_breach(const std::optional<RiskBlock>& risk) {
    if (!risk) return nullptr;
    auto it = std::find_if(risk->limits.begin(), risk->limits.end(), [](const RiskLimitCheck& c) { return c.kind == RiskLimitKind::Vega && c.exceeded; });
    return it == risk->limits.end() ? nullptr : &*it;
  }

  void release(std::uint32_t s, const std::optional<RiskBlock>& risk) {
    const std::optional<std::uint32_t> at = release_next(s);
    if (!at) return;
    CoachEvidence ev;
    ev.release_step = *at;
    const bool unprotected = book_.qty != 0 && !protected_at(s);
    const RiskLimitCheck* vega = vega_breach(risk);
    if (unprotected || vega != nullptr) {
      if (unprotected) ev.qty = Qty::from_raw(abs64(book_.qty));
      if (vega != nullptr) {
        ev.limit = vega->kind;
        ev.limit_bp = vega->limit_bp;
        ev.value_bp = vega->value_bp;
      }
      return add(s, HeldIntoReleaseUnhedged, ev);
    }
    const bool flat = book_.qty == 0 && shadow_.st.option_positions.empty() && !entry_working(s);
    if (flat && traded_) add(s, StayedFlatBeforeRelease, ev);
  }

  void limits(std::uint32_t s, const std::optional<RiskBlock>& risk) {
    std::vector<RiskLimitKind> now;
    if (risk) {
      for (const RiskLimitCheck& c : risk->limits) {
        if (!c.exceeded) continue;
        now.push_back(c.kind);
        if (std::find(breached_.begin(), breached_.end(), c.kind) != breached_.end()) continue;
        CoachEvidence ev;
        ev.limit = c.kind;
        ev.limit_bp = c.limit_bp;
        ev.value_bp = c.value_bp;
        add(s, RiskLimitExceeded, ev);
      }
    }
    breached_ = std::move(now);
  }

  void cuts(std::uint32_t s) {
    for (const TradeRow& t : trades_) {
      if (t.series != 0 || t.exit_step != s || t.net.raw >= 0 || t.plan_respected != true) continue;
      CoachEvidence ev;
      ev.loss = Money::from_raw(-t.net.raw);
      ev.planned_risk = t.planned_risk;
      ev.price = t.exit_avg;
      add(s, CutAtInvalidation, ev);
    }
  }
};

// Risk impact of a warning, most severe first (the selection's order before the step).
int impact_rank(CoachMomentCode c) {
  constexpr CoachMomentCode kOrder[] = {RiskLimitExceeded, AveragingDown,   RevengeTrade,          HeldIntoReleaseUnhedged, EntryWithoutStop,
                                        RiskAboveBudget,   FomoEntry,       AddedAgainstStructure, StopMovedAway};
  const auto* it = std::find(std::begin(kOrder), std::end(kOrder), c);
  return static_cast<int>(it - std::begin(kOrder));
}

std::vector<CoachMoment> select(const std::vector<CoachMoment>& all) {
  std::vector<CoachMoment> warn;
  std::vector<CoachMoment> good;
  for (const CoachMoment& m : all) (m.tone == FindingTone::Good ? good : warn).push_back(m);
  std::stable_sort(warn.begin(), warn.end(), [](const CoachMoment& a, const CoachMoment& b) {
    const int ra = impact_rank(a.code);
    const int rb = impact_rank(b.code);
    return ra != rb ? ra < rb : a.step < b.step;
  });
  if (warn.size() > kCoachMaxWarn) warn.resize(kCoachMaxWarn);
  if (good.size() > kCoachMaxGood) good.resize(kCoachMaxGood);
  warn.insert(warn.end(), good.begin(), good.end());
  // Within a step, what happened during the bar (an exit) comes before the decisions at its close.
  const auto phase = [](CoachMomentCode c) { return c == CutAtInvalidation || c == PartialAtTarget ? 0 : 1; };
  std::stable_sort(warn.begin(), warn.end(), [&phase](const CoachMoment& a, const CoachMoment& b) {
    if (a.step != b.step) return a.step < b.step;
    if (phase(a.code) != phase(b.code)) return phase(a.code) < phase(b.code);
    return static_cast<int>(a.code) < static_cast<int>(b.code);
  });
  return warn;
}

}  // namespace

std::vector<CoachMoment> coach_review(const Scenario& sc, const RunState& st) { return select(Review(sc, st).run()); }

const char* coach_lesson(CoachMomentCode code) {
  switch (code) {
    case EntryWithoutStop:
    case RiskAboveBudget: return "risk-sizing";
    case StopMovedAway:
    case CutAtInvalidation: return "mind-plan";
    case AddedAgainstStructure: return "structure-zones";
    case HeldIntoReleaseUnhedged:
    case StayedFlatBeforeRelease: return "events-surprise";
    case RevengeTrade:
    case FomoEntry:
    case AveragingDown: return "mind-biases";
    case RiskLimitExceeded: return "options-greeks";
    case StopToBreakevenAfter1r:
    case PartialAtTarget: return "management-scaling";
  }
  return "risk-sizing";
}

const char* coach_glossary(CoachMomentCode code) {
  switch (code) {
    case EntryWithoutStop:
    case StopMovedAway: return "stop-loss";
    case AddedAgainstStructure: return "trend";
    case RiskAboveBudget: return "risk-budget";
    case HeldIntoReleaseUnhedged: return "gap";
    case RevengeTrade: return "revenge-trading";
    case FomoEntry: return "fomo";
    case AveragingDown: return "averaging-down";
    case RiskLimitExceeded: return "risk-limit";
    case CutAtInvalidation: return "invalidation";
    case StopToBreakevenAfter1r: return "breakeven-stop";
    case PartialAtTarget: return "partial-take-profit";
    case StayedFlatBeforeRelease: return "surprise";
  }
  return "stop-loss";
}

}  // namespace tradelab
