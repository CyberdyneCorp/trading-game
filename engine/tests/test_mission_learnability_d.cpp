// Learnability bands of missions m18-m25 (add-practice-and-advanced-strategies design D12): scripted
// reference players trade through the engine over 60 seeds. Each band says what a disciplined player
// can learn there and that nothing is guaranteed. Signals are read at a bar's close and act from the
// next open.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "learnability.hpp"

using namespace tradelab;
using namespace tradelab::learn;

namespace {

double mean_of(const std::vector<double>& x) {
  double s = 0.0;
  for (const double a : x) s += a;
  return x.empty() ? 0.0 : s / static_cast<double>(x.size());
}

double stdev_of(const std::vector<double>& x) {
  const double mu = mean_of(x);
  double v = 0.0;
  for (const double a : x) v += (a - mu) * (a - mu);
  return x.empty() ? 0.0 : std::sqrt(v / static_cast<double>(x.size()));
}

int count_if_positive(const std::vector<double>& x) {
  return static_cast<int>(std::count_if(x.begin(), x.end(), [](double a) { return a > 0.0; }));
}

// Net delta in perpetual units: every option position's quantity x its delta at the close, plus the perpetual.
double net_delta(const Play& p) {
  double d = num(p.st.position.qty);
  for (const OptionPosition& o : p.st.option_positions) {
    const OptionSeries* s = find_series(p.sc, o.series);
    if (s == nullptr || o.qty.raw == 0 || s->expiry_step <= p.st.step) continue;
    d += num(o.qty) * num(quote(p.sc, *s, p.st.step, p.sc.bars[p.st.step].close).delta);
  }
  return d;
}

// A perpetual order that brings the net delta to zero, rounded toward zero to the 0.1 lot.
void hedge_delta(Play& p) {
  const double d = net_delta(p);
  const double q = std::floor(std::abs(d) * 10.0) / 10.0;
  if (q < 0.1) return;
  p.order(OrderType::Market, d > 0.0 ? OrderSide::Sell : OrderSide::Buy, lots(q));
}

double perp_fees(const Play& p) {
  double f = 0.0;
  for (const Event& e : p.st.events) {
    if (e.type == EventType::Fill && e.series == 0) f += num(e.amount);
  }
  return f;
}

// Realized volatility (annualized, hourly bars) of the closes over (from, to].
double realized_vol(const Play& p, std::uint32_t from, std::uint32_t to) {
  double s = 0.0;
  for (std::uint32_t i = from + 1; i <= to; ++i) s += std::pow(std::log(p.close(i) / p.close(i - 1)), 2);
  return std::sqrt(s / (to - from) * 8760.0);
}

}  // namespace

// ---- m18 hedge: a long straddle, unhedged, band-hedged or hedged every bar -----------------------

namespace {

constexpr const char* kM18 = "m18-gamma-scalp";
enum class Hedge : std::uint8_t { None, Band, EveryBar };

struct ScalpSeed {
  double ratio = 0.0;  // realized over the hold / implied at entry
  double unhedged = 0.0;
  double band = 0.0;
  double every = 0.0;
  double band_fees = 0.0;
  double every_fees = 0.0;
};

// A straddle at the money of the first expiry for 3% of equity, held to settlement; the hedger trades the
// perpetual at each close (every bar, or when |delta| exceeds 40% of the contracts).
Play m18_play(std::uint64_t seed, Hedge h, double* ratio) {
  Play p(kM18, seed);
  const std::uint32_t entry = p.st.step;
  const std::uint32_t expiry = p.expiries().front();
  const OptionSeries* call = p.strike_near(OptionKind::Call, expiry, p.close(entry));
  const OptionSeries* put = p.strike_near(OptionKind::Put, expiry, p.close(entry));
  const double cost = num(quote(p.sc, *call, entry, p.sc.bars[entry].close).ask) + num(quote(p.sc, *put, entry, p.sc.bars[entry].close).ask);
  const double q = std::floor(300.0 / cost);
  REQUIRE(p.option(call->id, OrderSide::Buy, lots(q)).accepted);
  REQUIRE(p.option(put->id, OrderSide::Buy, lots(q)).accepted);
  p.step();
  while (p.active() && p.st.step < expiry) {
    const double d = net_delta(p);
    if (h == Hedge::EveryBar || (h == Hedge::Band && std::abs(d) > 0.4 * q)) hedge_delta(p);
    p.step();
  }
  if (!p.flat()) p.close_position();
  p.finish();
  if (ratio != nullptr) *ratio = realized_vol(p, entry, expiry) / p.iv_at(*call, entry);
  return p;
}

ScalpSeed m18_seed(std::uint64_t seed) {
  ScalpSeed s;
  s.unhedged = m18_play(seed, Hedge::None, &s.ratio).net();
  const Play band = m18_play(seed, Hedge::Band, nullptr);
  const Play every = m18_play(seed, Hedge::EveryBar, nullptr);
  s.band = band.net();
  s.every = every.net();
  s.band_fees = perp_fees(band);
  s.every_fees = perp_fees(every);
  return s;
}

}  // namespace

TEST_CASE("Learnability m18: the hedged straddle earns when realized beats implied and loses when it does not") {
  std::vector<double> rich_band;
  std::vector<double> rich_unhedged;
  int cheap = 0;
  int cheap_band_loses = 0;
  std::vector<double> all_band;
  std::vector<double> all_unhedged;
  double band_fees = 0.0;
  double every_fees = 0.0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const ScalpSeed s = m18_seed(seed);
    all_band.push_back(s.band);
    all_unhedged.push_back(s.unhedged);
    band_fees += s.band_fees;
    every_fees += s.every_fees;
    if (s.ratio >= 1.2) {
      rich_band.push_back(s.band);
      rich_unhedged.push_back(s.unhedged);
    } else if (s.ratio <= 0.85) {
      cheap += 1;
      cheap_band_loses += s.band < 0.0 ? 1 : 0;
    }
  }
  const auto rr = [](const std::vector<double>& x) { return stdev_of(x) > 0.0 ? mean_of(x) / stdev_of(x) : 0.0; };
  MESSAGE("m18: realized/implied >= 1.2 in " << rich_band.size() << " seeds: band-hedged wins " << count_if_positive(rich_band)
                                             << ", return/risk hedged " << rr(rich_band) << " unhedged " << rr(rich_unhedged));
  MESSAGE("m18: realized/implied <= 0.85 in " << cheap << " seeds: band-hedged loses in " << cheap_band_loses);
  MESSAGE("m18: stdev hedged " << stdev_of(all_band) << ", unhedged " << stdev_of(all_unhedged) << "; perpetual fees every bar "
                               << every_fees << ", on a band " << band_fees);
  CHECK(rich_band.size() >= 10);
  CHECK(rr(rich_band) > rr(rich_unhedged));
  CHECK(share(count_if_positive(rich_band), static_cast<int>(rich_band.size())) >= 0.70);
  CHECK(cheap >= 10);
  CHECK(share(cheap_band_loses, cheap) >= 0.70);
  CHECK(stdev_of(all_band) <= 0.6 * stdev_of(all_unhedged));
  CHECK(every_fees >= 2.0 * band_fees);
}

// ---- m19 carry: a short perpetual hedged by a synthetic long --------------------------------------

namespace {

constexpr const char* kM19 = "m19-funding-carry";
constexpr double kCarryUnits = 30.0;

bool m19_flips(const Play& p) {
  return std::any_of(p.reveal.spans.begin(), p.reveal.spans.end(), [](const RegimeSpan& s) { return s.label == RegimeLabel::HighVol; });
}

bool liquidated(const Play& p) {
  return std::any_of(p.st.events.begin(), p.st.events.end(), [](const Event& e) { return e.type == EventType::Liquidation; });
}

// A short of kCarryUnits at `leverage` from the first decision to the end; hedged: plus a long call and a
// short put at the strike nearest the close on the last expiry (a synthetic long), held to settlement.
Play m19_play(std::uint64_t seed, bool hedged, std::int32_t leverage) {
  Play p(kM19, seed);
  p.leverage(leverage);
  if (hedged) {
    const std::uint32_t e = p.expiries().back();
    REQUIRE(p.option(p.strike_near(OptionKind::Call, e, p.close(p.st.step))->id, OrderSide::Buy, lots(kCarryUnits)).accepted);
    REQUIRE(p.option(p.strike_near(OptionKind::Put, e, p.close(p.st.step))->id, OrderSide::Sell, lots(kCarryUnits)).accepted);
  }
  REQUIRE(p.order(OrderType::Market, OrderSide::Sell, lots(kCarryUnits)).accepted);
  p.finish();
  return p;
}

}  // namespace

TEST_CASE("Learnability m19: the hedged carry earns while longs pay; the unwind turns it, and leverage decides the squeeze") {
  int calm = 0;
  int calm_positive = 0;
  int flips = 0;
  int flip_negative = 0;
  int liq5 = 0;
  int liq2 = 0;
  Outcomes naked;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Play h5 = m19_play(seed, true, 5);
    const Play h2 = m19_play(seed, true, 2);
    naked.add(m19_play(seed, false, 5));
    if (!m19_flips(h5)) {
      calm += 1;
      calm_positive += h2.net() > 0.0 ? 1 : 0;
      continue;
    }
    flips += 1;
    flip_negative += h2.net() < 0.0 ? 1 : 0;
    liq5 += liquidated(h5) ? 1 : 0;
    liq2 += liquidated(h2) ? 1 : 0;
  }
  MESSAGE("m19: no unwind in " << calm << " seeds, the hedged carry nets positive in " << calm_positive << "; unwind in " << flips
                               << ", negative in " << flip_negative);
  MESSAGE("m19: perpetual leg liquidated in unwind seeds: 5x " << liq5 << ", 2x " << liq2 << "; the unhedged short loses in "
                                                               << naked.losing());
  CHECK(calm >= 10);
  CHECK(share(calm_positive, calm) >= 0.75);
  CHECK(flips >= 10);
  CHECK(share(flip_negative, flips) >= 0.60);
  CHECK(share(liq5, flips) >= 0.25);
  CHECK(share(liq2, flips) <= 0.05);
  CHECK(naked.losing() >= 0.35);
}

// ---- m20 announcement: when implied volatility is bought and sold around a release ---------------

namespace {

constexpr const char* kM20 = "m20-event-vol";
enum class Timing : std::uint8_t { EarlySellBefore, EarlyHold, LateHold, ShortHold };

struct EventSeed {
  double early_sell = 0.0;
  double early_hold = 0.0;
  double late_hold = 0.0;
  double short_hold = 0.0;
  double move = 0.0;       // |close 3 bars after the release - close the bar before|
  double late_price = 0.0;  // the straddle's ask at the late entry
  bool crushed = false;     // IV after the release <= 0.7 x IV the bar before
};

// A straddle at the money, 10 contracts: bought early (at the announcement) on the last expiry, whose
// value carries more vega and less theta; bought the bar before on the first expiry (it outlives the release).
Play m20_play(std::uint64_t seed, Timing t, double* price) {
  Play p(kM20, seed);
  const NewsItem& n = p.sc.news.front();
  const std::uint32_t r = n.release_step;
  const bool early = t == Timing::EarlySellBefore || t == Timing::EarlyHold;
  p.step((early ? r - n.announce_lead : r - 1) - p.st.step);
  const std::uint32_t e = early ? p.expiries().back() : p.expiries().front();
  const OptionSeries* call = p.strike_near(OptionKind::Call, e, p.close(p.st.step));
  const OptionSeries* put = p.strike_near(OptionKind::Put, e, p.close(p.st.step));
  const OrderSide open = t == Timing::ShortHold ? OrderSide::Sell : OrderSide::Buy;
  const OrderSide close = t == Timing::ShortHold ? OrderSide::Buy : OrderSide::Sell;
  if (price != nullptr) {
    const Price f = p.sc.bars[p.st.step].close;
    *price = num(quote(p.sc, *call, p.st.step, f).ask) + num(quote(p.sc, *put, p.st.step, f).ask);
  }
  REQUIRE(p.option(call->id, open, lots(10)).accepted);
  REQUIRE(p.option(put->id, open, lots(10)).accepted);
  p.step((t == Timing::EarlySellBefore ? r - 1 : r + 3) - p.st.step);
  p.option(call->id, close, lots(10));
  p.option(put->id, close, lots(10));
  p.finish();
  return p;
}

EventSeed m20_seed(std::uint64_t seed) {
  EventSeed s;
  s.early_sell = m20_play(seed, Timing::EarlySellBefore, nullptr).net();
  s.early_hold = m20_play(seed, Timing::EarlyHold, nullptr).net();
  const Play late = m20_play(seed, Timing::LateHold, &s.late_price);
  s.late_hold = late.net();
  s.short_hold = m20_play(seed, Timing::ShortHold, nullptr).net();
  const std::uint32_t r = late.sc.news.front().release_step;
  s.move = std::abs(late.close(r + 3) - late.close(r - 1));
  const auto& iv = late.sc.options.iv_base;
  s.crushed = static_cast<double>(iv[r + 1].raw) <= 0.7 * static_cast<double>(iv[r].raw);
  return s;
}

}  // namespace

TEST_CASE("Learnability m20: implied volatility is bid before the release and crushed after; holding through needs a big move") {
  int crushed = 0;
  int early_better = 0;
  int big = 0;
  int big_wins = 0;
  int small = 0;
  int small_loses = 0;
  Outcomes early_sell;
  Outcomes shorts;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const EventSeed s = m20_seed(seed);
    crushed += s.crushed ? 1 : 0;
    early_better += s.early_sell > s.early_hold ? 1 : 0;
    early_sell.net.push_back(s.early_sell);
    shorts.net.push_back(s.short_hold);
    if (s.move > s.late_price) {
      big += 1;
      big_wins += s.late_hold > 0.0 ? 1 : 0;
    } else {
      small += 1;
      small_loses += s.late_hold < 0.0 ? 1 : 0;
    }
  }
  MESSAGE("m20: IV crushed in " << crushed << "; sold before beats held through in " << early_better);
  MESSAGE("m20: move > straddle in " << big << " seeds, holding through wins " << big_wins << "; in the other " << small << ", loses "
                                     << small_loses);
  MESSAGE("m20: buy early, sell before wins " << early_sell.positive() << "; short held through wins " << shorts.positive());
  CHECK(crushed == static_cast<int>(kSeeds));
  CHECK(share(early_better, static_cast<int>(kSeeds)) >= 0.60);
  CHECK(big >= 8);
  CHECK(share(big_wins, big) >= 0.85);
  CHECK(small >= 8);
  CHECK(share(small_loses, small) >= 0.80);
  CHECK(early_sell.losing() >= 0.15);
  CHECK(shorts.losing() >= 0.15);
}

// ---- m21 calendar: short near, long far, same strike --------------------------------------------

namespace {

constexpr const char* kM21 = "m21-calendar";

struct CalendarSeed {
  double net = 0.0;
  double debit = 0.0;       // premium paid - premium received + the entry fees
  double exit_costs = 0.0;  // at the near expiry: the settlement fee and the far long's bid below its mid
  double steps = 0.0;  // |close at the near expiry - strike| in strike steps
  int margin_calls = 0;
};

// A call calendar at the money for `q` contracts at the first decision (near expiry 1, far expiry 2),
// closed at the near expiry's decision step.
CalendarSeed m21_seed(std::uint64_t seed, double q) {
  Play p(kM21, seed);
  const std::vector<std::uint32_t> e = p.expiries();
  const double k = p.close(p.st.step);
  const OptionSeries* near = p.strike_near(OptionKind::Call, e[0], k);
  const OptionSeries* far = p.strike_near(OptionKind::Call, e[1], k);
  REQUIRE(p.option(far->id, OrderSide::Buy, lots(q)).accepted);
  REQUIRE(p.option(near->id, OrderSide::Sell, lots(q)).accepted);
  p.step();
  CalendarSeed s;
  s.debit = p.premium_paid();
  for (const Event& ev : p.st.events) {
    if (ev.type == EventType::Fill && ev.series == near->id) s.debit -= num(ev.price) * num(ev.qty);
  }
  const double entry_fees = num(p.st.fees_paid);
  s.debit += entry_fees;
  p.step(e[0] - p.st.step);
  s.net = p.net();  // at the near expiry: the near short settled, the far long at its bid
  const OptionQuote fq = quote(p.sc, *far, p.st.step, p.sc.bars[p.st.step].close);
  s.exit_costs = num(p.st.fees_paid) - entry_fees + q * (num(fq.mid) - num(fq.bid));
  p.option(far->id, OrderSide::Sell, lots(q));
  p.finish();
  s.steps = std::abs(p.close(e[0]) - num(near->strike)) / 2.5;
  s.margin_calls = p.margin_calls();
  return s;
}

}  // namespace

TEST_CASE("Learnability m21: the calendar's loss is its debit; it wins near the strike and loses on a breakout") {
  int within = 0;
  int near_n = 0;
  int near_wins = 0;
  int far_n = 0;
  int far_loses = 0;
  int calls = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const CalendarSeed s = m21_seed(seed, 20.0);
    within += s.net >= -(s.debit + s.exit_costs) - 1e-6 ? 1 : 0;
    calls += s.margin_calls;
    if (s.steps <= 1.0) {
      near_n += 1;
      near_wins += s.net > 0.0 ? 1 : 0;
    } else if (s.steps > 3.0) {
      far_n += 1;
      far_loses += s.net < 0.0 ? 1 : 0;
    }
  }
  MESSAGE("m21: loss within the debit in " << within << "; near the strike " << near_n << " seeds, wins " << near_wins
                                           << "; beyond three steps " << far_n << ", loses " << far_loses << "; margin calls " << calls);
  CHECK(within == static_cast<int>(kSeeds));
  CHECK(near_n >= 8);
  CHECK(share(near_wins, near_n) >= 0.70);
  CHECK(far_n >= 8);
  CHECK(share(far_loses, far_n) >= 0.75);
  CHECK(calls == 0);
}

// ---- m22 management: all-in/all-out, a managed exit, and hold with the initial stop ---------------

namespace {

constexpr const char* kM22 = "m22-scale-manage";
enum class Exit : std::uint8_t { AllOut2R, Managed, Hold };

struct ManageSeed {
  double r_all = 0.0;
  double r_managed = 0.0;
  double r_hold = 0.0;
};

struct Entry {
  int dir = 0;
  double price = 0.0;
  double risk = 0.0;  // per unit
  double q = 0.0;
};

// Enters 3 bars after the headline in the direction of the move since it, with a stop 2 x ATR(14) away
// and 1% of equity at risk.
Entry m22_enter(Play& p) {
  const std::uint32_t r = p.sc.news.front().release_step;
  p.step(r + 3 - p.st.step);
  Entry e;
  e.dir = p.close(p.st.step) >= p.close(r) ? 1 : -1;
  e.price = p.close(p.st.step);
  e.risk = 2.0 * p.atr_at(p.st.step);
  e.q = std::floor(100.0 / e.risk * 10.0) / 10.0;
  return e;
}

std::uint32_t working_stop(const Play& p) {
  for (const Order& o : p.st.orders) {
    if ((o.status == OrderStatus::Accepted || o.status == OrderStatus::PartiallyFilled) && o.type == OrderType::StopMarket && o.reduce_only) return o.id;
  }
  return 0;
}

void move_stop(Play& p, double price) {
  Command c;
  c.type = CommandType::MoveOrder;
  c.order_id = working_stop(p);
  c.price = ticks(price);
  p.send(c);
}

// Half at +1R (a reduce-only limit), then the stop to breakeven, then a stop trailed at 2 x ATR behind the
// best close, never below breakeven.
void manage(Play& p, const Entry& e) {
  const OrderSide out = e.dir > 0 ? OrderSide::Sell : OrderSide::Buy;
  Command tp;
  tp.order_type = OrderType::Limit;
  tp.side = out;
  tp.qty = lots(std::floor(e.q * 5.0) / 10.0);
  tp.price = ticks(e.price + e.dir * e.risk);
  tp.reduce_only = true;
  p.send(tp);
  bool halved = false;
  double best = e.price;
  while (p.active() && !p.flat()) {
    p.step();
    if (!p.active() || p.flat()) break;
    halved = halved || std::abs(num(p.st.position.qty)) < e.q - 0.05;
    if (!halved) continue;
    const double c = p.close(p.st.step);
    best = e.dir > 0 ? std::max(best, c) : std::min(best, c);
    const double trail = best - e.dir * 2.0 * p.atr_at(p.st.step);
    move_stop(p, e.dir > 0 ? std::max(e.price, trail) : std::min(e.price, trail));
  }
}

double m22_r(std::uint64_t seed, Exit x, double* mfe_r) {
  Play p(kM22, seed);
  const Entry e = m22_enter(p);
  const OrderSide in = e.dir > 0 ? OrderSide::Buy : OrderSide::Sell;
  const Price stop = ticks(e.price - e.dir * e.risk);
  const Price target = x == Exit::AllOut2R ? ticks(e.price + e.dir * 2.0 * e.risk) : Price{};
  REQUIRE(p.order(OrderType::Market, in, lots(e.q), Price{}, stop, target).accepted);
  if (x == Exit::Managed) {
    p.step();
    manage(p, e);
  }
  p.finish();
  if (mfe_r != nullptr) {
    double best = 0.0;
    for (std::uint32_t i = p.sc.news.front().release_step + 4; i <= p.last(); ++i) {
      const double moved = e.dir * (p.close(i) - e.price) / e.risk;
      if (moved < -1.0) break;
      best = std::max(best, moved);
    }
    *mfe_r = best;
  }
  return p.net() / 100.0;
}

}  // namespace

TEST_CASE("Learnability m22: a managed exit wins more often with a narrower spread of R; all-out at 2R earns more when it runs") {
  std::vector<double> all;
  std::vector<double> managed;
  std::vector<double> hold;
  int far = 0;
  int far_all_better = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    double mfe = 0.0;
    all.push_back(m22_r(seed, Exit::AllOut2R, &mfe));
    managed.push_back(m22_r(seed, Exit::Managed, nullptr));
    hold.push_back(m22_r(seed, Exit::Hold, nullptr));
    if (mfe >= 2.0) {
      far += 1;
      far_all_better += all.back() > managed.back() ? 1 : 0;
    }
  }
  const auto win = [](const std::vector<double>& x) { return share(count_if_positive(x), static_cast<int>(x.size())); };
  const auto lose = [](const std::vector<double>& x) {
    return share(static_cast<int>(std::count_if(x.begin(), x.end(), [](double a) { return a < 0.0; })), static_cast<int>(x.size()));
  };
  MESSAGE("m22: R mean/stdev/win: all-out " << mean_of(all) << "/" << stdev_of(all) << "/" << win(all) << ", managed " << mean_of(managed)
                                            << "/" << stdev_of(managed) << "/" << win(managed) << ", hold " << mean_of(hold) << "/"
                                            << stdev_of(hold) << "/" << win(hold));
  MESSAGE("m22: trend-far seeds (2R target reached before the stop) " << far << ", all-out better in " << far_all_better);
  CHECK(stdev_of(managed) < stdev_of(all));
  CHECK(win(managed) >= win(all) + 0.10);
  CHECK(far >= 8);
  CHECK(share(far_all_better, far) >= 0.60);
  CHECK(lose(all) >= 0.15);
  CHECK(lose(managed) >= 0.15);
  CHECK(lose(hold) >= 0.15);
}

// ---- m23 restart: after a stop-out, wait and re-enter on the setup, or re-enter at once at double size

namespace {

constexpr const char* kM23 = "m23-second-chance";

// The 20-bar high before bar i (closes), the breakout reference.
double high20(const Play& p, std::uint32_t i) {
  double h = 0.0;
  for (std::uint32_t k = i - 20; k < i; ++k) h = std::max(h, p.close(k));
  return h;
}

double low20(const Play& p, std::uint32_t i) {
  double l = 1e18;
  for (std::uint32_t k = i - 20; k < i; ++k) l = std::min(l, p.close(k));
  return l;
}

// Long on the first close above the 20-bar high with a stop 2 x ATR below and 1% at risk; after that
// stop fills, `revenge` re-enters long at the next decision with twice the quantity, else waits 3
// bars and re-enters on the next close beyond the 20-bar high or low (either side), normal size.
// Every re-entry keeps a 2 x ATR stop and is closed 30 bars later.
double m23_net(std::uint64_t seed, bool revenge) {
  Play p(kM23, seed);
  while (p.active() && p.close(p.st.step) <= high20(p, p.st.step)) p.step();
  const auto enter = [&p](int dir, double mult) {
    const double risk = 2.0 * p.atr_at(p.st.step);
    const double q = std::floor(mult * 100.0 / risk * 10.0) / 10.0;
    p.order(OrderType::Market, dir > 0 ? OrderSide::Buy : OrderSide::Sell, lots(q), Price{}, ticks(p.close(p.st.step) - dir * risk));
  };
  enter(1, 1.0);
  p.step();
  while (p.active() && !p.flat()) p.step();
  if (!p.active()) return p.net();
  if (revenge) {
    enter(1, 2.0);
  } else {
    p.step(3);
    while (p.active() && p.close(p.st.step) <= high20(p, p.st.step) && p.close(p.st.step) >= low20(p, p.st.step)) p.step();
    if (!p.active()) return p.net();
    enter(p.close(p.st.step) > high20(p, p.st.step) ? 1 : -1, 1.0);
  }
  p.step(30);
  if (!p.flat()) p.close_position();
  p.finish();
  return p.net();
}

}  // namespace

TEST_CASE("Learnability m23: waiting after a stop-out and re-entering on the setup loses less than revenge at double size") {
  int better = 0;
  std::vector<double> revenge;
  std::vector<double> cooled;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    revenge.push_back(m23_net(seed, true));
    cooled.push_back(m23_net(seed, false));
    better += cooled.back() > revenge.back() ? 1 : 0;
  }
  MESSAGE("m23: cooled down better in " << better << "; mean revenge " << mean_of(revenge) << ", cooled " << mean_of(cooled)
                                        << "; cooled wins " << count_if_positive(cooled) << ", revenge wins " << count_if_positive(revenge));
  CHECK(share(better, static_cast<int>(kSeeds)) >= 0.70);
}

// ---- m24 rally: a planned breakout entry against a chase -------------------------------------------

namespace {

constexpr const char* kM24 = "m24-fast-market";

struct ChaseSeed {
  double planned_r = 0.0;
  double chase_r = 0.0;
  bool chased = false;
};

double sma20(const Play& p, std::uint32_t i) {
  double s = 0.0;
  for (std::uint32_t k = i - 19; k <= i; ++k) s += p.close(k);
  return s / 20.0;
}

bool rising(const Play& p, std::uint32_t i, std::uint32_t n) {
  for (std::uint32_t k = i - n + 1; k <= i; ++k) {
    if (p.close(k) <= p.close(k - 1)) return false;
  }
  return true;
}

// One long with 1% at risk, closed 50 bars after its entry (the rally and the leg after it) or at its stop. Planned: the first close one ATR
// above the 20-bar high, the stop below the 20-bar low. Chase: the first close after 4 rising closes more than
// 2 x ATR above the 20-bar mean, the stop 2 x ATR below.
double m24_r(std::uint64_t seed, bool chase, bool* entered) {
  Play p(kM24, seed);
  const auto signal = [&p, chase](std::uint32_t i) {
    return chase ? rising(p, i, 4) && p.close(i) > sma20(p, i) + 2.0 * p.atr_at(i) : p.close(i) > high20(p, i) + p.atr_at(i);
  };
  while (p.active() && !signal(p.st.step)) p.step();
  *entered = p.active();
  if (!p.active()) return 0.0;
  const double stop = chase ? p.close(p.st.step) - 2.0 * p.atr_at(p.st.step) : low20(p, p.st.step);
  const double q = std::floor(100.0 / (p.close(p.st.step) - stop) * 10.0) / 10.0;
  p.order(OrderType::Market, OrderSide::Buy, lots(q), Price{}, ticks(stop));
  p.step(50);
  if (!p.flat()) p.close_position();
  p.finish();
  return p.net() / 100.0;
}

}  // namespace

TEST_CASE("Learnability m24: the planned breakout entry beats the chase more often; the chase still wins sometimes") {
  int both = 0;
  int planned_better = 0;
  int chase_wins = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    bool a = false;
    bool b = false;
    const double planned = m24_r(seed, false, &a);
    const double chase = m24_r(seed, true, &b);
    if (!a || !b) continue;
    both += 1;
    planned_better += planned > chase ? 1 : 0;
    chase_wins += chase > 0.0 ? 1 : 0;
  }
  MESSAGE("m24: both entered in " << both << " seeds; planned better in " << planned_better << ", the chase wins in " << chase_wins);
  CHECK(both >= 40);
  CHECK(share(planned_better, both) >= 0.65);
  CHECK(share(chase_wins, both) >= 0.15);
}

// ---- m25 dip: a single planned entry against averaging down to the same stop ------------------------

namespace {

constexpr const char* kM25 = "m25-the-dip";

// Long 1% at risk on the first close above the 20-bar high after the warm-up, stop 3 x ATR below, held to
// the end; averaging down adds twice the quantity once the close is 0.75 x ATR below the entry, same stop.
double m25_net(std::uint64_t seed, bool average) {
  Play p(kM25, seed);
  while (p.active() && p.close(p.st.step) <= high20(p, p.st.step)) p.step();
  const double entry = p.close(p.st.step);
  const double a = p.atr_at(p.st.step);
  const double stop = entry - 3.0 * a;
  const double q = std::floor(100.0 / (entry - stop) * 10.0) / 10.0;
  p.order(OrderType::Market, OrderSide::Buy, lots(q), Price{}, ticks(stop));
  bool added = !average;
  while (p.active()) {
    p.step();
    if (!added && !p.flat() && p.close(p.st.step) < entry - 0.75 * a) {
      p.order(OrderType::Market, OrderSide::Buy, lots(2.0 * q), Price{}, ticks(stop));
      added = true;
    }
  }
  return p.net();
}

}  // namespace

TEST_CASE("Learnability m25: the single planned entry beats averaging down more often; averaging down sometimes wins, worse at its worst") {
  int single_better = 0;
  int average_better = 0;
  std::vector<double> single;
  std::vector<double> averaged;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    single.push_back(m25_net(seed, false));
    averaged.push_back(m25_net(seed, true));
    single_better += single.back() >= averaged.back() ? 1 : 0;
    average_better += averaged.back() > single.back() ? 1 : 0;
  }
  const double worst_single = *std::min_element(single.begin(), single.end());
  const double worst_average = *std::min_element(averaged.begin(), averaged.end());
  MESSAGE("m25: single >= averaging in " << single_better << ", averaging better in " << average_better << "; worst single " << worst_single
                                         << ", worst averaging " << worst_average);
  CHECK(share(single_better, static_cast<int>(kSeeds)) >= 0.65);
  CHECK(share(average_better, static_cast<int>(kSeeds)) >= 0.20);
  CHECK(worst_average <= 2.0 * worst_single);
}

// ---- buy-and-hold is never the obvious answer (D8.3) ---------------------------------------------

TEST_CASE("Learnability m18-m25: buy-and-hold is positive in fewer than 75% of seeds") {
  for (const char* id : {kM18, kM19, kM20, kM21, kM22, kM23, kM24, kM25}) {
    const Outcomes bh = buy_and_hold(id);
    MESSAGE(std::string(id) << ": buy-and-hold positive in " << bh.positive());
    CHECK(bh.positive() < 0.75);
    CHECK(bh.losing() >= 0.15);
  }
}
