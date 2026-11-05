// Learnability bands of the batch-B missions m09-m12 (design D8.3): scripted reference players
// trade through the engine over 60 seeds. Each band says what a disciplined player can learn there
// and that nothing is guaranteed. Signals are read at a bar's close and act from the next open.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "learnability.hpp"

using namespace tradelab;
using namespace tradelab::learn;

// ---- m09 the day before: long volatility around an announced release --------------------------

namespace {

constexpr const char* kM09 = "m09-event-straddle";

struct Straddle {
  double net = 0.0;      // run P&L: both legs, fees included
  double premium = 0.0;  // paid for both legs
  int surprise = 0;
};

// Buys the ATM straddle of the first expiry at decision step `buy`, sells both legs at `sell`.
Straddle trade_straddle(std::uint64_t seed, std::int32_t buy_offset, std::int32_t sell_offset) {
  Play p(kM09, seed);
  const NewsItem& n = p.sc.news.front();
  const auto buy = static_cast<std::uint32_t>(static_cast<std::int32_t>(n.release_step) + buy_offset);
  const auto sell = static_cast<std::uint32_t>(static_cast<std::int32_t>(n.release_step) + sell_offset);
  p.step(buy - p.st.step);
  const std::uint32_t expiry = p.expiries().front();
  const OptionSeries* call = p.strike_near(OptionKind::Call, expiry, p.close(buy));
  const OptionSeries* put = p.strike_near(OptionKind::Put, expiry, p.close(buy));
  REQUIRE(call != nullptr);
  REQUIRE(put != nullptr);
  REQUIRE(p.option(call->id, OrderSide::Buy, lots(10.0)).accepted);
  REQUIRE(p.option(put->id, OrderSide::Buy, lots(10.0)).accepted);
  p.step(sell - p.st.step);
  p.option(call->id, OrderSide::Sell, lots(10.0));
  p.option(put->id, OrderSide::Sell, lots(10.0));
  p.finish();
  return {p.net(), p.premium_paid(), n.actual - n.expectation};
}

}  // namespace

TEST_CASE("Learnability m09: a straddle bought before the release pays only when the surprise beats the implied move") {
  Outcomes before;
  Outcomes after;
  std::map<int, Hits> by_surprise;  // |surprise| -> wins
  double premium = 0.0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Straddle s = trade_straddle(seed, -5, 3);
    before.net.push_back(s.net);
    before.drawdown.push_back(0.0);
    premium += s.premium;
    by_surprise[std::abs(s.surprise)].add(1, s.net);
    const Straddle late = trade_straddle(seed, 1, 3);  // the same exit, entered after the gap
    after.net.push_back(late.net);
    after.drawdown.push_back(0.0);
  }
  const double mean_pct = before.mean() / (premium / kSeeds);
  MESSAGE("m09 straddle 5 bars before, out 3 after: wins " << before.positive() << ", mean " << before.mean() << " ("
                                                          << 100.0 * mean_pct << "% of the premium)");
  for (const auto& [size, h] : by_surprise) MESSAGE("m09 |surprise| " << size << ": wins " << h.rate() << " of " << h.n);
  MESSAGE("m09 straddle bought 1 bar after the release: loses " << after.losing());
  CHECK(before.positive() >= 0.25);
  CHECK(before.positive() <= 0.55);
  CHECK(mean_pct <= 0.05);
  CHECK(by_surprise[2].rate() > 0.70);
  CHECK(by_surprise[0].rate() < 0.20);
  CHECK(after.losing() > 0.60);
  CHECK(before.losing() >= 0.15);
}

TEST_CASE("Learnability m09: implied volatility collapses once the decision is out") {
  double fall = 0.0;
  int up = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play p(kM09, seed);
    const std::uint32_t r = p.sc.news.front().release_step;
    const OptionSeries* atm = p.strike_near(OptionKind::Call, p.expiries().front(), p.close(r));
    const double before = p.iv_at(*atm, r);
    const double after = p.iv_at(*atm, r + 3);
    fall += 1.0 - after / before;
    up += after > before ? 1 : 0;
  }
  MESSAGE("m09: ATM IV falls on average " << 100.0 * fall / kSeeds << "% from the release bar to 3 bars after (rose in " << up << ")");
  CHECK(fall / kSeeds >= 0.25);
}

TEST_CASE("Learnability m09: the decision is drawn; the announcement shows only the consensus") {
  std::map<int, int> actual;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Scenario sc;
    REQUIRE(build_mission(kM09, seed, sc));
    REQUIRE(sc.news.size() == 1);
    const NewsItem& n = sc.news.front();
    CHECK(n.announce_lead == 20);
    CHECK(n.expectation == 0);
    actual[n.actual] += 1;
    // The first expiry lands after the release, with about 10 bars to spare.
    const std::uint32_t first_expiry = sc.options.series.front().expiry_step;
    CHECK(first_expiry > n.release_step + 5);
    CHECK(first_expiry < n.release_step + 16);
  }
  CHECK(actual.size() >= 4);
}

// ---- m10 cover: protective puts, collars, writers and carry ----------------------------------

namespace {

constexpr const char* kM10 = "m10-hedged-book";

struct M10Seed {
  int actual = 0;
  double unhedged = 0.0;  // 1x long held to the end
  double hedged = 0.0;    // the same long plus an equal ATM put of the second expiry
};

int m10_actual(const Play& p) { return p.sc.news.front().actual; }

// 1x long from the first decision to the end, with or without an equal ATM put of the last expiry.
Play hold_long(std::uint64_t seed, bool hedge, double* floor_loss) {
  Play p(kM10, seed);
  p.leverage(1);
  const double q = std::floor(10'000.0 * 0.45 / p.close(p.st.step));  // the put's premium stays affordable
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(q)).accepted);
  const OptionSeries* put = p.strike_near(OptionKind::Put, p.expiries().back(), p.close(p.st.step));
  if (hedge) REQUIRE(p.option(put->id, OrderSide::Buy, lots(q)).accepted);
  p.finish();
  if (floor_loss != nullptr) {
    double entry = 0.0;
    double premium = 0.0;
    for (const Event& e : p.st.events) {
      if (e.type != EventType::Fill) continue;
      (e.series == 0 ? entry : premium) = num(e.price);
    }
    // Most the hedged book can lose: entry above the strike, the premium, and every cost paid.
    const double costs = num(p.st.fees_paid) - num(p.st.funding_net);
    *floor_loss = (entry - num(put->strike) + premium) * q + costs;
  }
  return p;
}

}  // namespace

TEST_CASE("Learnability m10: a protective put is a floor, and an unneeded one is a cost") {
  int within_floor = 0;
  int calm = 0;
  int calm_hedged_below = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    double floor_loss = 0.0;
    const Play hedged = hold_long(seed, true, &floor_loss);
    const Play naked = hold_long(seed, false, nullptr);
    within_floor += -hedged.net() <= floor_loss + 0.01 ? 1 : 0;
    if (m10_actual(hedged) == 0) {
      calm += 1;
      calm_hedged_below += hedged.net() < naked.net() ? 1 : 0;
    }
  }
  MESSAGE("m10: the hedged book stayed within its floor in " << within_floor << " of " << kSeeds);
  MESSAGE("m10: with no shock, the hedged book did worse in " << calm_hedged_below << " of " << calm);
  CHECK(within_floor == static_cast<int>(kSeeds));
  CHECK(calm >= 6);
  CHECK(share(calm_hedged_below, calm) > 0.80);
}

TEST_CASE("Learnability m10: a severe rumor gaps a leveraged long's stop") {
  int severe = 0;
  int gapped = 0;
  Outcomes naked;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play p(kM10, seed);
    const std::uint32_t rumor = p.sc.news.front().release_step;
    // A 5x long with a 2xATR trailing stop, re-entered whenever stopped out before the rumor.
    while (p.active() && p.st.step <= rumor) {
      if (p.flat()) {
        const std::uint32_t t = p.st.step;
        p.order(OrderType::Market, OrderSide::Buy, lots(10'000.0 / p.close(t)));
        p.step();
        if (!p.flat()) p.trail(ticks(2.0 * p.atr_at(t)));
        continue;
      }
      p.step();
    }
    p.finish();
    naked.add(p);
    if (m10_actual(p) != -2) continue;
    severe += 1;
    for (const Event& e : p.st.events) {
      if (e.type != EventType::Fill || e.requested.raw == 0 || e.step != rumor + 1) continue;
      gapped += (num(e.requested) - num(e.price)) / num(e.requested) > 0.02 ? 1 : 0;
    }
  }
  MESSAGE("m10: a 5x long's 2xATR stop filled more than 2% beyond its trigger in " << gapped << " of " << severe << " severe seeds");
  CHECK(severe >= 15);
  CHECK(share(gapped, severe) >= 0.40);
  CHECK(naked.losing() >= 0.15);
}

TEST_CASE("Learnability m10: a small collar never margin-calls; a large put writer does when the rumor is severe") {
  int collar_calls = 0;
  int severe = 0;
  int writer_calls = 0;
  Outcomes writer;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play c(kM10, seed);
    c.leverage(1);
    const double price = c.close(c.st.step);
    const double q = std::floor(0.4 * 10'000.0 / price);
    const std::uint32_t expiry = c.expiries().back();
    REQUIRE(c.order(OrderType::Market, OrderSide::Buy, lots(q)).accepted);
    REQUIRE(c.option(c.strike_near(OptionKind::Put, expiry, price, -1)->id, OrderSide::Buy, lots(q)).accepted);
    REQUIRE(c.option(c.strike_near(OptionKind::Call, expiry, price, 1)->id, OrderSide::Sell, lots(q)).accepted);
    c.finish();
    collar_calls += c.margin_calls() > 0 ? 1 : 0;

    Play w(kM10, seed);
    REQUIRE(w.option(w.strike_near(OptionKind::Put, expiry, price)->id, OrderSide::Sell, lots(300.0)).accepted);
    w.finish();
    writer.add(w);
    if (m10_actual(w) != -2) continue;
    severe += 1;
    writer_calls += w.margin_calls() > 0 ? 1 : 0;
  }
  MESSAGE("m10: collars margin-called in " << collar_calls << " seeds; a 300-put writer in " << writer_calls << " of " << severe
                                           << " severe seeds");
  CHECK(collar_calls == 0);
  CHECK(share(writer_calls, severe) >= 0.30);
  CHECK(writer.losing() >= 0.15);
}

TEST_CASE("Learnability m10: a long held through the crowded uptrend pays its carry") {
  double paid = 0.0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play p(kM10, seed);
    p.leverage(1);
    const double q = std::floor(5'000.0 / p.close(p.st.step));
    p.order(OrderType::Market, OrderSide::Buy, lots(q));
    const RegimeSpan* crowded = p.first_span(RegimeLabel::Crowded);
    p.step(crowded->start + crowded->bars - p.st.step);
    const double notional = q * p.open(p.sc.mission.warmup_bars + 1);
    paid += -num(p.st.funding_net) / notional;
  }
  MESSAGE("m10: a 1x long paid on average " << 100.0 * paid / kSeeds << "% of notional in funding through the uptrend");
  CHECK(paid / kSeeds >= 0.008);
}

// ---- m11 calendar: expectation against surprise ------------------------------------------------

namespace {

constexpr const char* kM11 = "m11-news-calendar";

// A 5x long entered two bars before a release with a 1xATR stop: did the stop fill beyond 1%?
bool stop_jumped(std::uint64_t seed, std::size_t item, bool* held) {
  Play p(kM11, seed);
  const std::uint32_t r = p.sc.news[item].release_step;
  p.step(r - 2 - p.st.step);
  const std::uint32_t t = p.st.step;
  p.order(OrderType::Market, OrderSide::Buy, lots(45'000.0 / p.close(t)), {}, ticks(p.close(t) - p.atr_at(t)));
  p.step(2);
  *held = !p.flat();
  p.step(3);
  for (const Event& e : p.st.events) {
    if (e.type == EventType::Fill && e.requested.raw != 0 && e.side == OrderSide::Sell && e.step == r + 1) {
      return (num(e.requested) - num(e.price)) / num(e.requested) > 0.01;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("Learnability m11: the consensus is priced; the surprise gaps; following afterwards is no edge") {
  Hits follow;
  Hits consensus;
  double quiet_gap = 0.0;
  int quiet = 0;
  double loud_gap = 0.0;
  int loud = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play p(kM11, seed);
    for (const NewsItem& n : p.sc.news) {
      const std::uint32_t r = n.release_step;
      const int surprise = n.actual - n.expectation;
      const double gap = std::abs(std::log(p.open(r + 1) / p.close(r)));
      (surprise == 0 ? quiet_gap : loud_gap) += gap;
      (surprise == 0 ? quiet : loud) += 1;
      if (n.announce_lead == 0) continue;
      follow.add(surprise != 0 ? sign(surprise) : n.tone, std::log(p.close(r + 10) / p.open(r + 1)));
      if (n.expectation != 0) consensus.add(n.expectation, std::log(p.close(r + kHorizon) / p.open(r - 1)));
    }
  }
  MESSAGE("m11: following a scheduled headline from the next open is right " << follow.rate() << " of " << follow.n);
  MESSAGE("m11: |gap| with surprise 0: " << 100.0 * quiet_gap / quiet << "% (" << quiet << "), with a surprise: "
                                        << 100.0 * loud_gap / loud << "% (" << loud << ")");
  MESSAGE("m11: positioned with the consensus before the release, right " << consensus.rate() << " of " << consensus.n);
  CHECK(follow.rate() < 0.65);
  CHECK(quiet_gap / quiet < 0.005);
  CHECK(loud_gap / loud > 0.02);
  CHECK(consensus.rate() < 0.60);
}

TEST_CASE("Learnability m11: a stop held through a release can fill far beyond its trigger") {
  int releases = 0;
  int jumped = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Scenario sc;
    REQUIRE(build_mission(kM11, seed, sc));
    for (std::size_t i = 0; i < sc.news.size(); ++i) {
      if (sc.news[i].actual == sc.news[i].expectation) continue;
      bool held = false;
      const bool beyond = stop_jumped(seed, i, &held);
      if (!held) continue;
      releases += 1;
      jumped += beyond ? 1 : 0;
    }
  }
  MESSAGE("m11: a 5x long's 1xATR stop filled more than 1% beyond its trigger in " << jumped << " of " << releases
                                                                                  << " releases with a surprise");
  CHECK(releases > 60);
  CHECK(share(jumped, releases) >= 0.30);
}

TEST_CASE("Learnability m11: four releases a run, three announced 15-25 bars ahead") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Scenario sc;
    REQUIRE(build_mission(kM11, seed, sc));
    REQUIRE(sc.news.size() == 4);
    int announced = 0;
    for (const NewsItem& n : sc.news) {
      if (n.announce_lead == 0) continue;
      announced += 1;
      CHECK(n.announce_lead >= 15);
      CHECK(n.announce_lead <= 25);
      CHECK(n.release_step >= sc.mission.warmup_bars);
    }
    CHECK(announced == 3);
  }
}

// ---- m12 final exam ---------------------------------------------------------------------------

namespace {

constexpr const char* kM12 = "m12-capstone";

// Fade RSI extremes at 1x and exit at market kHorizon bars later.
void always_fade_rsi(Play& p) {
  const Series r = rsi(p.sc, p.last(), 14);
  while (p.active() && p.st.step + kHorizon + 1 < p.last()) {
    const double v = num(r[p.st.step]);
    const int side = (v < 30.0) - (v > 70.0);
    if (side == 0) {
      p.step();
      continue;
    }
    p.order(OrderType::Market, side > 0 ? OrderSide::Buy : OrderSide::Sell, lots(p.equity() / p.close(p.st.step)));
    p.step(kHorizon);
    p.close_position();
    p.step();
  }
  p.finish();
}

// Hold the MACD histogram's side at 1x, reversing when it flips.
void always_follow_macd(Play& p) {
  const Macd m = macd(p.sc, p.last(), 12, 26, 9);
  int held = 0;
  while (p.active() && p.st.step + 1 < p.last()) {
    const int side = sign(num(m.histogram[p.st.step]));
    if (side != 0 && side != held) {
      if (held != 0) p.close_position();
      p.order(OrderType::Market, side > 0 ? OrderSide::Buy : OrderSide::Sell, lots(0.9 * p.equity() / p.close(p.st.step)));
      held = side;
    }
    p.step();
  }
  p.finish();
}

// Write an ATM straddle of each expiry as soon as it is the nearest one; let it settle.
void sell_straddles(Play& p) {
  for (const std::uint32_t expiry : p.expiries()) {
    if (!p.active() || p.st.step >= expiry) continue;
    const double price = p.close(p.st.step);
    const double q = std::floor(0.3 * p.equity() / price);
    p.option(p.strike_near(OptionKind::Call, expiry, price)->id, OrderSide::Sell, lots(q));
    p.option(p.strike_near(OptionKind::Put, expiry, price)->id, OrderSide::Sell, lots(q));
    p.step(expiry - p.st.step);
  }
  p.finish();
}

// Long at 1x while the published funding pays longs (shorts crowded), flat otherwise.
void carry_long(Play& p) {
  while (p.active() && p.st.step + 1 < p.last()) {
    const bool paid = num(account_view(p.sc, p.st).next_funding_rate) < -0.0004;
    if (paid && p.flat()) p.order(OrderType::Market, OrderSide::Buy, lots(0.9 * p.equity() / p.close(p.st.step)));
    if (!paid && !p.flat()) p.close_position();
    p.step();
  }
  p.finish();
}

// The reference rule set: read the regime with ADX first (fade the bands below 20, follow MACD with
// the DI lines above 25), risk 1% with a 2xATR stop, and stay flat from 3 bars before an announced
// release until the bar after it.
void reference_rules(Play& p) {
  const std::uint32_t last = p.last();
  const Adx a = adx(p.sc, last, 14);
  const Bollinger bb = bollinger(p.sc, last, 20, 2);
  const Macd m = macd(p.sc, last, 12, 26, 9);
  const Series r = rsi(p.sc, last, 14);
  auto near_release = [&](std::uint32_t t) {
    for (const NewsItem& n : p.sc.news) {
      const bool announced = n.announce_lead > 0 && t + n.announce_lead >= n.release_step;
      if (announced && t + 3 >= n.release_step && t <= n.release_step + 1) return true;
    }
    return false;
  };
  while (p.active() && p.st.step + 1 < last) {
    const std::uint32_t t = p.st.step;
    if (near_release(t)) {
      if (!p.flat()) p.close_position();
      p.step();
      continue;
    }
    if (!p.flat()) {
      p.step();
      continue;
    }
    const double adx_v = num(a.adx[t]);
    const double c = p.close(t);
    int side = 0;
    double target = num(bb.middle[t]);  // a fade takes profit back at the middle band
    if (adx_v < 20.0 && num(r[t]) < 35.0 && bb.lower[t] && c < num(bb.lower[t])) side = 1;
    if (adx_v < 20.0 && num(r[t]) > 65.0 && bb.upper[t] && c > num(bb.upper[t])) side = -1;
    const int di = sign(num(a.plus_di[t]) - num(a.minus_di[t]));
    if (adx_v > 25.0 && sign(num(m.histogram[t])) == di) {
      side = di;
      target = c + side * 4.0 * p.atr_at(t);  // a follow aims for 2R
    }
    if (side != 0) {
      const double stop = c - side * 2.0 * p.atr_at(t);
      p.order(OrderType::Market, side > 0 ? OrderSide::Buy : OrderSide::Sell, lots(0.01 * p.equity() / (2.0 * p.atr_at(t))), {},
              ticks(stop), ticks(target));
    }
    p.step();
  }
  p.finish();
}

}  // namespace

TEST_CASE("Learnability m12: no fixed strategy is the answer; the reference rules beat buy-and-hold, never guaranteed") {
  std::map<std::string, Outcomes> fixed;
  Outcomes reference;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    Play a(kM12, seed);
    always_fade_rsi(a);
    fixed["always fade RSI"].add(a);
    Play b(kM12, seed);
    b.leverage(2);
    always_follow_macd(b);
    fixed["always follow MACD"].add(b);
    Play c(kM12, seed);
    sell_straddles(c);
    fixed["sell a straddle every expiry"].add(c);
    Play d(kM12, seed);
    d.leverage(2);
    carry_long(d);
    fixed["carry long while shorts pay"].add(d);
    Play e(kM12, seed);
    reference_rules(e);
    reference.add(e);
  }
  fixed["buy and hold"] = buy_and_hold(kM12);
  for (const auto& [name, o] : fixed) {
    MESSAGE("m12 " << name << ": positive " << o.positive() << ", mean " << o.mean() << ", return/DD " << o.return_to_drawdown());
    CHECK(o.positive() <= 0.70);
  }
  MESSAGE("m12 reference rules: positive " << reference.positive() << ", loses " << reference.losing() << ", return/DD "
                                           << reference.return_to_drawdown());
  CHECK(reference.return_to_drawdown() > fixed["buy and hold"].return_to_drawdown());
  CHECK(reference.losing() >= 0.20);
}
