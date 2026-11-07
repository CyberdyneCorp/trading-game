// Learnability bands of the strategy missions m13-m17 (add-academy-and-strategies design D8):
// scripted reference players trade option structures through the engine over 60 seeds. Each band
// says what a disciplined player can learn there and that nothing is guaranteed. Signals are read at
// a bar's close and act from the next open.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "learnability.hpp"

using namespace tradelab;
using namespace tradelab::learn;

namespace {

struct Leg {
  const OptionSeries* series = nullptr;
  OrderSide side = OrderSide::Buy;
};

// Places every leg at the current decision step (buys first, as the strategy builder does).
void open_legs(Play& p, const std::vector<Leg>& legs, double q) {
  for (const OrderSide side : {OrderSide::Buy, OrderSide::Sell}) {
    for (const Leg& l : legs) {
      if (l.side == side) REQUIRE(p.option(l.series->id, side, lots(q)).accepted);
    }
  }
}

// Closes every leg at the current decision step (shorts first: a short never stays uncovered).
void close_legs(Play& p, const std::vector<Leg>& legs, double q) {
  for (const OrderSide side : {OrderSide::Sell, OrderSide::Buy}) {
    for (const Leg& l : legs) {
      if (l.side == side) p.option(l.series->id, l.side == OrderSide::Buy ? OrderSide::Sell : OrderSide::Buy, lots(q));
    }
  }
}

// Premium of this side's fills in the run so far (fill price x quantity).
double option_flow(const Play& p, OrderSide side) {
  double sum = 0.0;
  for (const Event& e : p.st.events) {
    if (e.type == EventType::Fill && e.series != 0 && e.side == side) sum += num(e.price) * num(e.qty);
  }
  return sum;
}

double fees(const Play& p) { return num(p.st.fees_paid); }

double mid(const Play& p, const OptionSeries& s, std::uint32_t i) { return num(quote(p.sc, s, i, p.sc.bars[i].close).mid); }

}  // namespace

// ---- m13 spread: a bull call spread against a naked call ------------------------------------------

namespace {

constexpr const char* kM13 = "m13-call-spread";
// The written call's distance from the bought one, in strikes (design D8 "As built": one 2.5 step, not
// two, so the written call is worth 35-65% of the bought one at the first expiry's tenor).
constexpr int kSpreadSteps = 1;

struct SpreadSeed {
  double rise = 0.0;    // underlying close at the exit decision minus close at the entry decision
  double width = 0.0;   // K2 - K1
  double call = 0.0;    // naked ATM call: run P&L, fees included
  double spread = 0.0;  // ATM call bought, +2 strikes call written
  double call_cost = 0.0;
  double spread_cost = 0.0;
};

// Enters at the headline's decision step, exits 3 bars before the first expiry.
Play m13_play(std::uint64_t seed, bool spread, double* cost) {
  Play p(kM13, seed);
  const std::uint32_t r = p.sc.news.front().release_step;
  p.step(r - p.st.step);
  const std::uint32_t expiry = p.expiries().front();
  std::vector<Leg> legs = {{p.strike_near(OptionKind::Call, expiry, p.close(r)), OrderSide::Buy}};
  if (spread) legs.push_back({p.strike_near(OptionKind::Call, expiry, p.close(r), kSpreadSteps), OrderSide::Sell});
  open_legs(p, legs, 10.0);
  p.step();
  *cost = option_flow(p, OrderSide::Buy) - option_flow(p, OrderSide::Sell);
  p.step(expiry - 3 - p.st.step);
  close_legs(p, legs, 10.0);
  p.finish();
  return p;
}

SpreadSeed m13_seed(std::uint64_t seed) {
  SpreadSeed s;
  const Play call = m13_play(seed, false, &s.call_cost);
  const Play spread = m13_play(seed, true, &s.spread_cost);
  const std::uint32_t r = call.sc.news.front().release_step;
  const std::uint32_t expiry = call.expiries().front();
  s.rise = call.close(expiry - 3) - call.close(r);
  s.width = kSpreadSteps * 2.5;
  s.call = call.net();
  s.spread = spread.net();
  return s;
}

}  // namespace

TEST_CASE("Learnability m13: the spread costs less and wins the modest move; the naked call wins the large one") {
  int small = 0;
  int small_spread = 0;
  int large = 0;
  int large_call = 0;
  int spread_better = 0;
  int both_lose = 0;
  int both_lose_spread_less = 0;
  double cost_ratio = 0.0;
  Outcomes calls;
  Outcomes spreads;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const SpreadSeed s = m13_seed(seed);
    cost_ratio += s.spread_cost / s.call_cost / kSeeds;
    calls.net.push_back(s.call);
    spreads.net.push_back(s.spread);
    spread_better += s.spread >= s.call ? 1 : 0;
    if (s.rise < s.width) {
      small += 1;
      small_spread += s.spread >= s.call ? 1 : 0;
    }
    if (s.rise > 2 * s.width) {
      large += 1;
      large_call += s.call > s.spread ? 1 : 0;
    }
    if (s.call < 0.0 && s.spread < 0.0) {
      both_lose += 1;
      both_lose_spread_less += s.spread > s.call ? 1 : 0;
    }
  }
  MESSAGE("m13: the spread costs " << 100.0 * cost_ratio << "% of the naked call");
  MESSAGE("m13: rise < width: spread >= call in " << small_spread << " of " << small << "; rise > 2 widths: call > spread in "
                                                  << large_call << " of " << large);
  MESSAGE("m13: spread >= call in " << spread_better << " of " << kSeeds << "; both lose in " << both_lose
                                    << ", the spread less in " << both_lose_spread_less);
  MESSAGE("m13: the call wins " << calls.positive() << ", the spread wins " << spreads.positive());
  CHECK(cost_ratio >= 0.35);
  CHECK(cost_ratio <= 0.65);
  CHECK(small >= 10);
  CHECK(share(small_spread, small) > 0.70);
  CHECK(large >= 5);
  CHECK(share(large_call, large) > 0.80);
  CHECK(share(spread_better, static_cast<int>(kSeeds)) <= 0.75);
  CHECK(share(spread_better, static_cast<int>(kSeeds)) >= 0.25);
  CHECK(share(both_lose_spread_less, both_lose) >= 0.90);
  CHECK(calls.losing() >= 0.15);
  CHECK(spreads.losing() >= 0.15);
}

TEST_CASE("Learnability m13: buy-and-hold is positive in 45-75% of seeds") {
  const Outcomes bh = buy_and_hold(kM13);
  MESSAGE("m13: buy-and-hold positive in " << bh.positive());
  CHECK(bh.positive() >= 0.45);
  CHECK(bh.positive() <= 0.75);
}

// ---- m14 income: a covered call against long only ----------------------------------------------

namespace {

constexpr const char* kM14 = "m14-covered-call";

// A long of the whole balance at the cap (3x), held to the end; with `covered`, a call about 5% out
// of the money of the soonest expiry is written for the same quantity at the first decision and
// again at each expiry (rolled at settlement).
Play m14_play(std::uint64_t seed, bool covered) {
  Play p(kM14, seed);
  const double q = std::floor(10'000.0 / p.close(p.st.step));
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(q)).accepted);
  for (const std::uint32_t expiry : p.expiries()) {
    if (covered && p.active()) {
      const OptionSeries* call = p.strike_near(OptionKind::Call, expiry, 1.05 * p.close(p.st.step));
      REQUIRE(p.option(call->id, OrderSide::Sell, lots(q)).accepted);
    }
    p.step(expiry - p.st.step);
  }
  p.finish();
  return p;
}

double stdev(const std::vector<double>& x) {
  double mu = 0.0;
  for (const double a : x) mu += a / static_cast<double>(x.size());
  double v = 0.0;
  for (const double a : x) v += (a - mu) * (a - mu) / static_cast<double>(x.size());
  return std::sqrt(v);
}

}  // namespace

TEST_CASE("Learnability m14: the covered call earns in a grind and gives up the rally; its cushion is small") {
  int rally = 0;
  int rally_long_better = 0;
  int calm = 0;
  int calm_covered_better = 0;
  Outcomes longs;
  Outcomes covered;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Play l = m14_play(seed, false);
    const Play c = m14_play(seed, true);
    longs.add(l);
    covered.add(c);
    if (l.sc.news.front().actual == 2) {
      rally += 1;
      rally_long_better += l.net() > c.net() ? 1 : 0;
    } else {
      calm += 1;
      calm_covered_better += c.net() > l.net() ? 1 : 0;
    }
  }
  const double worst_long = *std::min_element(longs.net.begin(), longs.net.end());
  const double worst_covered = *std::min_element(covered.net.begin(), covered.net.end());
  MESSAGE("m14: rally seeds " << rally << ", long only better in " << rally_long_better << "; other seeds " << calm
                              << ", the covered call better in " << calm_covered_better);
  MESSAGE("m14: worst seed long " << worst_long << ", covered " << worst_covered << "; stdev long " << stdev(longs.net)
                                  << ", covered " << stdev(covered.net));
  MESSAGE("m14: long wins " << longs.positive() << ", covered wins " << covered.positive());
  CHECK(rally >= 12);
  CHECK(share(rally_long_better, rally) > 0.80);
  CHECK(share(calm_covered_better, calm) > 0.65);
  CHECK(worst_covered <= 0.85 * worst_long);
  CHECK(stdev(covered.net) < stdev(longs.net));
  CHECK(covered.losing() >= 0.15);
  CHECK(longs.losing() >= 0.15);
}

TEST_CASE("Learnability m14: buy-and-hold is positive in 45-75% of seeds") {
  const Outcomes bh = buy_and_hold(kM14);
  MESSAGE("m14: buy-and-hold positive in " << bh.positive());
  CHECK(bh.positive() >= 0.45);
  CHECK(bh.positive() <= 0.75);
}

// ---- m15 insurance: a protective put against a stop and no hedge ---------------------------------

namespace {

constexpr const char* kM15 = "m15-protective-put";

int m15_actual(const Play& p) { return p.sc.news.front().actual; }

// A 1x long from the first decision to the end, with or without an equal ATM put of the second
// expiry; `floor_loss` receives the most the hedged book can lose: (entry - strike + premium) x q
// plus every cost paid.
Play m15_long(std::uint64_t seed, bool hedge, double* floor_loss) {
  Play p(kM15, seed);
  p.leverage(1);
  const double q = std::floor(10'000.0 * 0.45 / p.close(p.st.step));
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(q)).accepted);
  const OptionSeries* put = p.strike_near(OptionKind::Put, p.expiries().back(), p.close(p.st.step));
  if (hedge) REQUIRE(p.option(put->id, OrderSide::Buy, lots(q)).accepted);
  p.finish();
  if (floor_loss != nullptr) {
    double entry = 0.0;
    double premium = 0.0;
    for (const Event& e : p.st.events) {
      if (e.type == EventType::Fill) (e.series == 0 ? entry : premium) = num(e.price);
    }
    *floor_loss = (entry - num(put->strike) + premium) * q + fees(p) - num(p.st.funding_net);
  }
  return p;
}

}  // namespace

TEST_CASE("Learnability m15: a protective put is a floor that costs its premium when nothing happens") {
  int within_floor = 0;
  int calm = 0;
  int calm_hedged_below = 0;
  Outcomes puts;  // the put's own P&L: hedged minus unhedged
  Outcomes unhedged;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    double floor_loss = 0.0;
    const Play hedged = m15_long(seed, true, &floor_loss);
    const Play naked = m15_long(seed, false, nullptr);
    within_floor += -hedged.net() <= floor_loss + 0.01 ? 1 : 0;
    puts.net.push_back(hedged.net() - naked.net());
    unhedged.add(naked);
    if (m15_actual(hedged) == 0) {
      calm += 1;
      calm_hedged_below += hedged.net() < naked.net() ? 1 : 0;
    }
  }
  MESSAGE("m15: the hedged long stayed within its floor in " << within_floor << " of " << kSeeds);
  MESSAGE("m15: no incident in " << calm << " seeds; the hedged long did worse in " << calm_hedged_below);
  MESSAGE("m15: the put's own mean P&L " << puts.mean() << ", unhedged loses " << unhedged.losing());
  CHECK(within_floor == static_cast<int>(kSeeds));
  CHECK(calm >= 15);
  CHECK(share(calm_hedged_below, calm) > 0.80);
  CHECK(puts.mean() < 0.0);
  CHECK(unhedged.losing() >= 0.15);
}

TEST_CASE("Learnability m15: the strike is the deductible; a stop is free but gaps in a severe incident") {
  double ratio = 0.0;
  int severe = 0;
  int gapped = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play p(kM15, seed);
    const std::uint32_t t = p.st.step;
    const std::uint32_t expiry = p.expiries().back();
    const double atm = mid(p, *p.strike_near(OptionKind::Put, expiry, p.close(t)), t);
    const double otm = mid(p, *p.strike_near(OptionKind::Put, expiry, 0.95 * p.close(t)), t);
    ratio += otm / atm / kSeeds;
    const std::uint32_t incident = p.sc.news.front().release_step;
    // A 3x long with a 2xATR trailing stop, re-entered whenever stopped out before the incident.
    while (p.active() && p.st.step <= incident) {
      if (p.flat()) {
        const std::uint32_t at = p.st.step;
        p.order(OrderType::Market, OrderSide::Buy, lots(10'000.0 / p.close(at)));
        p.step();
        if (!p.flat()) p.trail(ticks(2.0 * p.atr_at(at)));
        continue;
      }
      p.step();
    }
    if (m15_actual(p) != -2) continue;
    severe += 1;
    for (const Event& e : p.st.events) {
      if (e.type != EventType::Fill || e.requested.raw == 0 || e.step != incident + 1) continue;
      gapped += (num(e.requested) - num(e.price)) / num(e.requested) > 0.02 ? 1 : 0;
    }
  }
  MESSAGE("m15: a 5% out-of-the-money put costs " << 100.0 * ratio << "% of the ATM put");
  MESSAGE("m15: a 2xATR stop filled more than 2% beyond its trigger in " << gapped << " of " << severe << " severe seeds");
  CHECK(ratio >= 0.30);
  CHECK(ratio <= 0.70);
  CHECK(severe >= 12);
  CHECK(share(gapped, severe) >= 0.40);
}

TEST_CASE("Learnability m15: buy-and-hold is positive in 40-70% of seeds") {
  const Outcomes bh = buy_and_hold(kM15);
  MESSAGE("m15: buy-and-hold positive in " << bh.positive());
  CHECK(bh.positive() >= 0.40);
  CHECK(bh.positive() <= 0.70);
}

// ---- m16 premium: an iron condor against a short strangle ----------------------------------------

namespace {

constexpr const char* kM16 = "m16-range-condor";
// Quantities (design D8 "As built"). Both structures are compared at kCompareQty, a size at which no
// margin call cuts the strangle's loss short. The strangle's margin calls are counted at kOversizedQty,
// where its per-leg initial margin takes about 95% of equity. The condor is checked for margin calls
// (under the mission's spread margin) at the largest quantity whose maximum loss, width minus the credit
// at the chain's mids, is at most kCondorRiskShare of equity.
constexpr double kCompareQty = 150.0;
constexpr double kOversizedQty = 440.0;
constexpr double kCondorRiskShare = 0.20;
constexpr double kWidth = 2 * 2.5;

struct PremiumSeed {
  double net = 0.0;
  double bound = 0.0;  // (width - credit) x units + costs
  int margin_calls = 0;
};

// The strikes 2 steps either side of the money at the first decision, first expiry; with wings, the
// strikes 4 steps away bought (the iron condor).
std::vector<Leg> premium_legs(const Play& p, bool wings) {
  const std::uint32_t expiry = p.expiries().front();
  const double f = p.close(p.st.step);
  std::vector<Leg> legs = {{p.strike_near(OptionKind::Put, expiry, f, -2), OrderSide::Sell},
                           {p.strike_near(OptionKind::Call, expiry, f, 2), OrderSide::Sell}};
  if (wings) {
    legs.push_back({p.strike_near(OptionKind::Put, expiry, f, -4), OrderSide::Buy});
    legs.push_back({p.strike_near(OptionKind::Call, expiry, f, 4), OrderSide::Buy});
  }
  return legs;
}

// The condor quantity whose maximum loss (width - credit at the mids) is kCondorRiskShare of equity.
double condor_qty_at_risk_share(std::uint64_t seed) {
  const Play p(kM16, seed);
  double credit = 0.0;
  for (const Leg& l : premium_legs(p, true)) credit += (l.side == OrderSide::Sell ? 1.0 : -1.0) * mid(p, *l.series, p.st.step);
  return std::floor(kCondorRiskShare * num(p.sc.mission.initial_balance) / (kWidth - credit) * 10.0) / 10.0;
}

// Sells premium at the first decision (see premium_legs) and holds it to settlement.
PremiumSeed sell_premium(std::uint64_t seed, bool wings, double q) {
  Play p(kM16, seed);
  open_legs(p, premium_legs(p, wings), q);
  p.step();
  const double credit = (option_flow(p, OrderSide::Sell) - option_flow(p, OrderSide::Buy)) / q;
  p.finish();
  return {p.net(), (kWidth - credit) * q + fees(p), p.margin_calls()};
}

bool breakout_before(const Play& p) {
  const RegimeSpan* b = p.first_span(RegimeLabel::Trend);
  return b != nullptr && b->start < p.expiries().front();
}

}  // namespace

TEST_CASE("Learnability m16: the condor's loss is capped; the strangle wins as often and loses far more") {
  int within_bound = 0;
  Outcomes condors;
  Outcomes strangles;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const PremiumSeed c = sell_premium(seed, true, kCompareQty);
    const PremiumSeed s = sell_premium(seed, false, kCompareQty);
    within_bound += c.margin_calls == 0 && -c.net <= c.bound + 0.01 ? 1 : 0;
    condors.net.push_back(c.net);
    strangles.net.push_back(s.net);
  }
  const double worst_condor = *std::min_element(condors.net.begin(), condors.net.end());
  const double worst_strangle = *std::min_element(strangles.net.begin(), strangles.net.end());
  MESSAGE("m16: condor within its bound in " << within_bound << " of " << kSeeds);
  MESSAGE("m16: wins: condor " << condors.positive() << ", strangle " << strangles.positive() << "; worst: condor "
                               << worst_condor << ", strangle " << worst_strangle);
  CHECK(within_bound == static_cast<int>(kSeeds));
  CHECK(strangles.positive() >= 0.50);
  CHECK(strangles.positive() <= 0.80);
  CHECK(worst_strangle <= 3.0 * worst_condor);
  CHECK(std::abs(strangles.positive() - condors.positive()) <= 0.15);
  CHECK(condors.losing() >= 0.15);
  CHECK(strangles.losing() >= 0.15);
}

TEST_CASE("Learnability m16: an oversized strangle is margin-called by an early breakout; the condor is not") {
  int early = 0;
  int early_calls = 0;
  int condor_calls = 0;
  double smallest = 1e18;
  double largest = 0.0;
  REQUIRE(Play(kM16, 1).sc.options.spread_margin);  // the band is about the mission's own margin rule
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const double q = condor_qty_at_risk_share(seed);
    smallest = std::min(smallest, q);
    largest = std::max(largest, q);
    condor_calls += sell_premium(seed, true, q).margin_calls > 0 ? 1 : 0;
    if (!breakout_before(Play(kM16, seed))) continue;
    early += 1;
    early_calls += sell_premium(seed, false, kOversizedQty).margin_calls > 0 ? 1 : 0;
  }
  MESSAGE("m16: breakout before the first expiry in " << early << " seeds; a " << kOversizedQty
                                                      << "-unit strangle margin-called in " << early_calls);
  MESSAGE("m16: the condor at " << kCondorRiskShare * 100 << "% of equity at its maximum loss (" << smallest << "-"
                                 << largest << " units) margin-called in " << condor_calls);
  CHECK(early >= 10);
  CHECK(share(early_calls, early) >= 0.25);
  CHECK(condor_calls == 0);
}

TEST_CASE("Learnability m16: buy-and-hold is positive in 35-65% of seeds") {
  const Outcomes bh = buy_and_hold(kM16);
  MESSAGE("m16: buy-and-hold positive in " << bh.positive());
  CHECK(bh.positive() >= 0.35);
  CHECK(bh.positive() <= 0.65);
}

// ---- m17 expiry: theta and gamma near expiry; close, roll or hold ---------------------------------

namespace {

constexpr const char* kM17 = "m17-before-expiry";
constexpr double kM17Qty = 10.0;

// Fees plus the half-spread crossed at every option fill (the fill against the mid it was quoted
// from: the previous close's IV at this bar's open).
double option_costs(const Play& p) {
  double spread = 0.0;
  for (const Event& e : p.st.events) {
    if (e.type != EventType::Fill || e.series == 0) continue;
    const Price mid_px = quote(p.sc, *find_series(p.sc, e.series), e.step - 1, p.sc.bars[e.step].open).mid;
    spread += std::abs(num(e.price) - num(mid_px)) * num(e.qty);
  }
  return fees(p) + spread;
}

// Expiries that leave 25 bars of life after the listing (the reference trades start 25 bars before).
std::vector<std::uint32_t> long_lived(const Play& p) {
  std::vector<std::uint32_t> out;
  for (const std::uint32_t e : p.expiries()) {
    if (e >= p.sc.mission.warmup_bars + 25) out.push_back(e);
  }
  return out;
}

struct ShortPutPath {
  double early_swing = 0.0;  // mean |one-bar change of the position's value|, 20-25 bars before expiry
  double late_swing = 0.0;   // the same over the last 5 bars (to the settlement value)
  double premium = 0.0;      // mid at entry
  double left = 0.0;         // time value still in the put 5 bars before expiry
};

// The value path of a short ATM put written 25 bars before expiry `e` and held to settlement.
ShortPutPath short_put_path(const Play& p, std::uint32_t e) {
  const std::uint32_t t = e - 25;
  const OptionSeries& s = *p.strike_near(OptionKind::Put, e, p.close(t));
  const auto value = [&](std::uint32_t i) { return i == e ? std::max(0.0, num(s.strike) - p.close(e)) : mid(p, s, i); };
  ShortPutPath out;
  for (std::uint32_t i = t + 1; i <= t + 5; ++i) out.early_swing += std::abs(value(i) - value(i - 1)) / 5.0;
  for (std::uint32_t i = e - 4; i <= e; ++i) out.late_swing += std::abs(value(i) - value(i - 1)) / 5.0;
  out.premium = value(t);
  out.left = value(e - 5) - std::max(0.0, num(s.strike) - p.close(e - 5));
  return out;
}

// Writes the ATM put 25 bars before `e`; holds it to settlement, or rolls it 5 bars before into the
// next expiry's ATM put (held to its settlement). Returns the option costs paid.
double hold_or_roll(std::uint64_t seed, std::uint32_t e, bool roll) {
  Play p(kM17, seed);
  p.step(e - 25 - p.st.step);
  const OptionSeries* put = p.strike_near(OptionKind::Put, e, p.close(p.st.step));
  REQUIRE(p.option(put->id, OrderSide::Sell, lots(kM17Qty)).accepted);
  p.step(e - 5 - p.st.step);
  if (roll) {
    p.option(put->id, OrderSide::Buy, lots(kM17Qty));
    const std::vector<std::uint32_t> all = p.expiries();
    const auto next = std::upper_bound(all.begin(), all.end(), e);
    if (next != all.end()) p.option(p.strike_near(OptionKind::Put, *next, p.close(p.st.step))->id, OrderSide::Sell, lots(kM17Qty));
  }
  p.finish();
  return option_costs(p);
}

}  // namespace

TEST_CASE("Learnability m17: an ATM option's theta per bar and gamma at least double from 20 to 3 bars before expiry") {
  int checked = 0;
  int kernel_ok = 0;
  int theta_ok = 0;
  int gamma_ok = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Play p(kM17, seed);
    Scenario same_iv = p.sc;  // the kernel property: the same implied-volatility level at both times
    for (const std::uint32_t e : p.expiries()) {
      const OptionSeries& s = *p.strike_near(OptionKind::Call, e, p.close(e - 20));
      const OptionQuote far = quote(p.sc, s, e - 20, s.strike);  // priced at the money: forward = strike
      const OptionQuote near = quote(p.sc, s, e - 3, s.strike);
      same_iv.options.iv_base[e - 3] = p.sc.options.iv_base[e - 20];
      const OptionQuote kernel = quote(same_iv, s, e - 3, s.strike);
      checked += 1;
      kernel_ok += std::abs(num(kernel.theta)) >= 2.0 * std::abs(num(far.theta)) && num(kernel.gamma) >= 2.0 * num(far.gamma) ? 1 : 0;
      theta_ok += std::abs(num(near.theta)) >= 2.0 * std::abs(num(far.theta)) ? 1 : 0;
      gamma_ok += num(near.gamma) >= 2.0 * num(far.gamma) ? 1 : 0;
    }
  }
  MESSAGE("m17: at one IV level theta and gamma doubled in " << kernel_ok << " of " << checked
                                                            << " expiries; on the generated paths theta in " << theta_ok
                                                            << " and gamma in " << gamma_ok);
  CHECK(checked == static_cast<int>(4 * kSeeds));
  CHECK(kernel_ok == checked);
  // On the path the IV level moves with the regime (design D8 "As built"): most expiries, not all.
  CHECK(share(theta_ok, checked) >= 0.90);
  CHECK(share(gamma_ok, checked) >= 0.80);
}

TEST_CASE("Learnability m17: near expiry the premium left is small against what one bar can swing") {
  double early_risk = 0.0;  // one-bar swing per unit of premium still to earn, 20-25 bars before
  double late_risk = 0.0;   // the same over the last 5 bars, against the time value left 5 bars before
  double early = 0.0;
  double late = 0.0;
  double left = 0.0;
  int n = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Play p(kM17, seed);
    for (const std::uint32_t e : long_lived(p)) {
      const ShortPutPath path = short_put_path(p, e);
      early += path.early_swing;
      late += path.late_swing;
      early_risk += path.early_swing / path.premium;
      late_risk += path.late_swing / std::max(path.left, 0.01);
      left += path.left / path.premium;
      n += 1;
    }
  }
  MESSAGE("m17: one-bar swing of a short ATM put: " << early / n << " 20-25 bars before, " << late / n
                                                     << " in the last 5 bars; per unit of premium left: " << early_risk / n
                                                     << " and " << late_risk / n << "; time value left 5 bars before: "
                                                     << 100.0 * left / n << "% of the entry premium");
  // As built (design D8): the swing itself barely grows (an ATM put ends in or out of the money), so
  // the band is the swing against the premium still to earn.
  CHECK(late_risk >= 2.0 * early_risk);
  CHECK(left / n <= 0.40);
}

TEST_CASE("Learnability m17: a roll is two more trades; a long OTM call held to settlement loses its time value") {
  double hold_costs = 0.0;
  double roll_costs = 0.0;
  int calls = 0;
  int worthless = 0;
  Outcomes holders;
  Outcomes call_buyers;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Play probe(kM17, seed);
    const std::uint32_t e = long_lived(probe).front();
    hold_costs += hold_or_roll(seed, e, false);
    roll_costs += hold_or_roll(seed, e, true);
    Play h(kM17, seed);
    h.step(e - 25 - h.st.step);
    REQUIRE(h.option(h.strike_near(OptionKind::Put, e, h.close(h.st.step))->id, OrderSide::Sell, lots(kM17Qty)).accepted);
    h.finish();
    holders.add(h);
    Play c(kM17, seed);
    c.step(e - 25 - c.st.step);
    const OptionSeries* call = c.strike_near(OptionKind::Call, e, c.close(c.st.step), 1);
    REQUIRE(call != nullptr);
    REQUIRE(c.option(call->id, OrderSide::Buy, lots(kM17Qty)).accepted);
    c.finish();
    call_buyers.add(c);
    calls += 1;
    worthless += c.close(e) <= num(call->strike) ? 1 : 0;
  }
  MESSAGE("m17: option costs: hold " << hold_costs / kSeeds << ", roll " << roll_costs / kSeeds << " ("
                                     << roll_costs / hold_costs << "x)");
  MESSAGE("m17: a long OTM call expired worthless in " << worthless << " of " << calls << "; short put loses "
                                                       << holders.losing() << ", call loses " << call_buyers.losing());
  CHECK(roll_costs >= 1.8 * hold_costs);
  CHECK(share(worthless, calls) > 0.60);
  CHECK(holders.losing() >= 0.15);
  CHECK(call_buyers.losing() >= 0.15);
}

TEST_CASE("Learnability m17: buy-and-hold is positive in 35-65% of seeds") {
  const Outcomes bh = buy_and_hold(kM17);
  MESSAGE("m17: buy-and-hold positive in " << bh.positive());
  CHECK(bh.positive() >= 0.35);
  CHECK(bh.positive() <= 0.65);
}
