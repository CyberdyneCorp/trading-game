// Strategy comparison (add-academy-and-strategies design D8, scoring v13, report-only): every row
// reproduces a scripted run of the same structure, the entry follows the player's first option fill,
// and computing it changes nothing in the player's run.

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "learnability.hpp"
#include "tradelab/c_api.h"

using namespace tradelab;
using namespace tradelab::learn;

namespace {

const ComparisonRow& row(const StrategyComparison& c, ComparisonStrategy name) {
  for (const ComparisonRow& r : c.rows) {
    if (r.name == name) return r;
  }
  FAIL("no such row");
  return c.rows.front();
}

double net_of(const ComparisonRow& r) { return num(r.net_pnl); }

// A finished run in which the player traded nothing (the comparison uses the mission's reference).
Play finished(const char* mission, std::uint64_t seed) {
  Play p(mission, seed);
  p.finish();
  return p;
}

StrategyComparison compare(const Play& p) {
  const auto c = strategy_comparison(p.sc, p.st);
  REQUIRE(c.has_value());
  return *c;
}

}  // namespace

TEST_CASE("Strategy comparison: only the strategy missions, only once the run has ended") {
  Play active("m16-range-condor", 3);
  CHECK_FALSE(strategy_comparison(active.sc, active.st).has_value());
  CHECK_FALSE(strategy_comparison(finished("m05-options-writer", 3).sc, finished("m05-options-writer", 3).st).has_value());
  for (const char* m : {"m13-call-spread", "m14-covered-call", "m15-protective-put", "m16-range-condor", "m17-before-expiry"}) {
    const StrategyComparison c = compare(finished(m, 3));
    CHECK_FALSE(c.from_player);
    CHECK(c.qty == lots(10.0));
    CHECK(c.rows.size() >= 2);
    for (const ComparisonRow& r : c.rows) CHECK(r.refused == Reason::None);
  }
}

TEST_CASE("Strategy comparison m16: the strangle and the condor reproduce scripted runs held to settlement") {
  for (const std::uint64_t seed : {3ULL, 11ULL}) {
    const StrategyComparison c = compare(finished("m16-range-condor", seed));
    for (const bool wings : {false, true}) {
      Play p("m16-range-condor", seed);
      const std::uint32_t expiry = p.expiries().front();
      const double f = p.close(p.st.step);
      std::vector<std::pair<const OptionSeries*, OrderSide>> legs;
      if (wings) legs.emplace_back(p.strike_near(OptionKind::Put, expiry, f, -4), OrderSide::Buy);
      if (wings) legs.emplace_back(p.strike_near(OptionKind::Call, expiry, f, 4), OrderSide::Buy);
      legs.emplace_back(p.strike_near(OptionKind::Put, expiry, f, -2), OrderSide::Sell);
      legs.emplace_back(p.strike_near(OptionKind::Call, expiry, f, 2), OrderSide::Sell);
      for (const auto& [s, side] : legs) REQUIRE(p.option(s->id, side, lots(10.0)).accepted);
      p.finish();
      const ComparisonRow& r = row(c, wings ? ComparisonStrategy::IronCondor : ComparisonStrategy::ShortStrangle);
      CHECK(net_of(r) == doctest::Approx(p.net()).epsilon(1e-12));
      CHECK(num(r.max_drawdown) == doctest::Approx(p.drawdown()).epsilon(1e-12));
      CHECK(static_cast<int>(r.margin_calls) == p.margin_calls());
      CHECK(r.exit_step == expiry);
    }
  }
}

TEST_CASE("Strategy comparison m13: the call and the spread enter after the headline and close 3 bars before expiry") {
  const StrategyComparison c = compare(finished("m13-call-spread", 5));
  for (const bool spread : {false, true}) {
    Play p("m13-call-spread", 5);
    const std::uint32_t r = p.sc.news.front().release_step;
    p.step(r - p.st.step);
    const std::uint32_t expiry = p.expiries().front();
    const OptionSeries* call = p.strike_near(OptionKind::Call, expiry, p.close(r));
    const OptionSeries* upper = p.strike_near(OptionKind::Call, expiry, p.close(r), 1);
    REQUIRE(p.option(call->id, OrderSide::Buy, lots(10.0)).accepted);
    if (spread) REQUIRE(p.option(upper->id, OrderSide::Sell, lots(10.0)).accepted);
    p.step(expiry - 3 - p.st.step);
    p.option(call->id, OrderSide::Sell, lots(10.0));
    if (spread) p.option(upper->id, OrderSide::Buy, lots(10.0));
    p.finish();
    const ComparisonRow& row_ = row(c, spread ? ComparisonStrategy::BullCallSpread : ComparisonStrategy::NakedCall);
    CHECK(c.entry_step == r);
    CHECK(net_of(row_) == doctest::Approx(p.net()).epsilon(1e-12));
    CHECK(row_.exit_step == expiry - 2);  // filled at the open after the decision 3 bars before
  }
}

TEST_CASE("Strategy comparison m17: a roll is the hold plus a buy-back and a new short at each expiry") {
  const StrategyComparison c = compare(finished("m17-before-expiry", 7));
  const double hold = net_of(row(c, ComparisonStrategy::HoldToSettlement));
  const double close = net_of(row(c, ComparisonStrategy::CloseBeforeExpiry));
  const double roll = net_of(row(c, ComparisonStrategy::RollBeforeExpiry));
  CHECK(hold != close);
  CHECK(roll != close);
  // Scripted roll: written at the first decision, rolled 5 bars before each expiry into the next.
  Play p("m17-before-expiry", 7);
  const std::vector<std::uint32_t> all = p.expiries();
  auto it = std::upper_bound(all.begin(), all.end(), p.st.step + 5);
  REQUIRE(it != all.end());
  const OptionSeries* put = p.strike_near(OptionKind::Put, *it, p.close(p.st.step));
  REQUIRE(p.option(put->id, OrderSide::Sell, lots(10.0)).accepted);
  for (auto next = std::next(it); next != all.end(); ++next) {
    p.step(put->expiry_step - 5 - p.st.step);
    p.option(put->id, OrderSide::Buy, lots(10.0));
    put = p.strike_near(OptionKind::Put, *next, p.close(p.st.step));
    REQUIRE(p.option(put->id, OrderSide::Sell, lots(10.0)).accepted);
  }
  p.finish();
  CHECK(roll == doctest::Approx(p.net()).epsilon(1e-12));
}

TEST_CASE("Strategy comparison m15: the long, the stop and the puts at the player's first option entry and quantity") {
  Play p("m15-protective-put", 9);
  p.step(4);
  const std::uint32_t at = p.st.step;
  const OptionSeries* put = p.strike_near(OptionKind::Put, p.expiries().back(), p.close(at));
  REQUIRE(p.option(put->id, OrderSide::Buy, lots(12.5)).accepted);
  p.finish();
  const StrategyComparison c = compare(p);
  CHECK(c.from_player);
  CHECK(c.entry_step == at);
  CHECK(c.qty == lots(12.5));
  REQUIRE(c.rows.size() == 4);
  for (const ComparisonRow& r : c.rows) CHECK(r.entry_step == at);
  // The long alone, scripted at the mission's cap.
  Play l("m15-protective-put", 9);
  l.step(4);
  REQUIRE(l.order(OrderType::Market, OrderSide::Buy, lots(12.5)).accepted);
  l.finish();
  CHECK(net_of(row(c, ComparisonStrategy::LongOnly)) == doctest::Approx(l.net()).epsilon(1e-12));
  CHECK(row(c, ComparisonStrategy::LongOnly).exit_step == l.st.step);
  // The ATM put on the second expiry: the long plus exactly the player's put.
  Play h("m15-protective-put", 9);
  h.step(4);
  REQUIRE(h.option(put->id, OrderSide::Buy, lots(12.5)).accepted);
  REQUIRE(h.order(OrderType::Market, OrderSide::Buy, lots(12.5)).accepted);
  h.finish();
  CHECK(net_of(row(c, ComparisonStrategy::AtmPut)) == doctest::Approx(h.net()).epsilon(1e-12));
}

TEST_CASE("Strategy comparison: an oversized player quantity is refused, not resized; the run is unchanged") {
  Play p("m16-range-condor", 3);
  const std::uint32_t expiry = p.expiries().front();
  const OptionSeries* call = p.strike_near(OptionKind::Call, expiry, p.close(p.st.step), 4);
  REQUIRE(p.option(call->id, OrderSide::Buy, lots(2000.0)).accepted);
  p.finish();
  const Digest before = state_digest(p.st);
  const std::size_t events = p.st.events.size();
  const StrategyComparison c = compare(p);
  CHECK(c.qty == lots(2000.0));
  CHECK(row(c, ComparisonStrategy::ShortStrangle).refused == Reason::InsufficientMargin);
  CHECK(row(c, ComparisonStrategy::ShortStrangle).net_pnl.raw == 0);
  CHECK(state_digest(p.st) == before);
  CHECK(p.st.events.size() == events);
}

TEST_CASE("Strategy comparison in tl_debrief: null for other missions, rows for m14, digest unchanged") {
  tl_run* run = tl_run_create("m14-covered-call", 3);
  REQUIRE(run != nullptr);
  CHECK(std::string(tl_debrief(run)) == "{\"error\":\"run_active\"}");
  tl_advance(run, 100000);
  const std::string before = tl_state(run);
  const std::string d = tl_debrief(run);
  CHECK(d.find("\"strategyComparison\":{\"basis\":\"reference\"") != std::string::npos);
  CHECK(d.find("\"name\":\"long_only\"") != std::string::npos);
  CHECK(d.find("\"name\":\"covered_call\"") != std::string::npos);
  CHECK(std::string(tl_state(run)) == before);
  tl_run_destroy(run);
  tl_run* other = tl_run_create("m01-false-breakout", 3);
  tl_advance(other, 100000);
  CHECK(std::string(tl_debrief(other)).find("\"strategyComparison\":null") != std::string::npos);
  tl_run_destroy(other);
}
