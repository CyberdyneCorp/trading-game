// Coach review (add-practice-and-advanced-strategies design D17): each moment code on a scripted run
// and a near miss, no look-ahead (bars after a step replaced leave every moment up to it unchanged),
// the selection caps, and purity through the C ABI (refused while active, nothing changed).

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/generator.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;
using namespace tradelab::test;
using enum CoachMomentCode;

namespace {

constexpr std::uint32_t kWarmup = 25;

Price px(double v) { return Price::from_raw(std::llround(v * 100.0) * (Price::kScale / 100)); }

// Bars opening at the previous close, wicks 0.1 beyond the body: `kWarmup` flat bars at 100, then `closes`
// (the first is the close of the first decision, kWarmup).
Scenario market(const std::vector<double>& closes) {
  Scenario sc = make_scenario({{"100", "100.1", "99.9", "100"}}, kWarmup);
  std::vector<double> all(kWarmup, 100.0);
  all.insert(all.end(), closes.begin(), closes.end());
  sc.bars.clear();
  double prev = 100.0;
  for (const double c : all) {
    sc.bars.push_back({px(prev), px(std::max(prev, c) + 0.1), px(std::min(prev, c) - 0.1), px(c), Q("1000000"), false});
    prev = c;
  }
  sc.funding_rate.assign(sc.bars.size(), Rate{});
  return sc;
}

std::vector<double> flat(std::size_t n, double v = 100.0) { return std::vector<double>(n, v); }

std::vector<double> concat(std::vector<double> a, const std::vector<double>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// Runs to the end, then reviews.
std::vector<CoachMoment> finish(Driver& d) {
  for (int i = 0; i < 1000 && d.state().status == RunStatus::Active; ++i) d.step();
  REQUIRE(d.state().status != RunStatus::Active);
  return coach_review(d.scenario(), d.state());
}

const CoachMoment* find(const std::vector<CoachMoment>& ms, CoachMomentCode code) {
  auto it = std::find_if(ms.begin(), ms.end(), [code](const CoachMoment& m) { return m.code == code; });
  return it == ms.end() ? nullptr : &*it;
}

std::uint32_t child_stop(const RunState& st) {
  for (const Order& o : st.orders) {
    if (o.parent_id != 0 && o.type == OrderType::StopMarket && o.active()) return o.id;
  }
  return 0;
}

}  // namespace

TEST_CASE("Coach entry_without_stop: a position opened with no stop working after the decision; silent with a bracket stop") {
  const Scenario sc = market(flat(20));
  Driver d(sc);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step();
  const std::vector<CoachMoment> ms = finish(d);
  const CoachMoment* m = find(ms, EntryWithoutStop);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup + 1);  // judged at the close of the bar it filled on
  CHECK(m->tone == FindingTone::Warn);
  CHECK(*m->evidence.qty == Q("10"));
  CHECK(*m->evidence.close == P("100"));

  Driver ok(sc);
  REQUIRE(ok.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "98", nullptr).accepted);
  CHECK(find(finish(ok), EntryWithoutStop) == nullptr);

  Driver later(sc);  // the stop placed at the close of the fill bar counts
  REQUIRE(later.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  later.step();
  REQUIRE(later.order(OrderType::StopMarket, OrderSide::Sell, "10", "98", true).accepted);
  CHECK(find(finish(later), EntryWithoutStop) == nullptr);
}

TEST_CASE("Coach stop_moved_away: a stop lowered under a long; silent when raised") {
  const Scenario sc = market(flat(20));
  const auto run = [&sc](const char* to) {
    Driver d(sc);
    REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "98", nullptr).accepted);
    d.step();
    REQUIRE(d.move(child_stop(d.state()), to).accepted);
    return finish(d);
  };
  const std::vector<CoachMoment> away = run("97");
  const CoachMoment* m = find(away, StopMovedAway);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup + 1);
  CHECK(*m->evidence.from == P("98"));
  CHECK(*m->evidence.to == P("97"));
  CHECK(find(run("98.5"), StopMovedAway) == nullptr);
}

TEST_CASE("Coach added_against_structure: an add below SMA(20) and the 10-bar low; silent on an add in a rise") {
  // A long at 101, then closes falling 0.5 a bar: by the 4th the close is under the average and every low.
  const Scenario falling = market(concat({101.0, 100.5, 100.0, 99.5, 99.0}, flat(15, 99.0)));
  Driver d(falling);
  REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", nullptr).accepted);
  d.step(4);  // close 99.0 at kWarmup + 4
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  const std::vector<CoachMoment> ms = finish(d);
  const CoachMoment* m = find(ms, AddedAgainstStructure);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup + 4);
  CHECK(*m->evidence.close == P("99"));
  CHECK(*m->evidence.level == P("99.4"));  // the previous bar's low: 99.5 - 0.1
  CHECK(*m->evidence.sma > P("99"));

  const Scenario rising = market(concat({101.0, 101.5, 102.0, 102.5, 103.0}, flat(15, 103.0)));
  Driver r(rising);
  REQUIRE(r.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", nullptr).accepted);
  r.step(4);
  REQUIRE(r.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  CHECK(find(finish(r), AddedAgainstStructure) == nullptr);
}

TEST_CASE("Coach risk_above_budget: a risk to the stop above 2% of the initial capital; silent under it") {
  const Scenario sc = market(flat(20));
  const auto run = [&sc](const char* qty) {
    Driver d(sc);
    REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, qty, nullptr, "95", nullptr).accepted);
    return finish(d);
  };
  const std::vector<CoachMoment> big = run("50");
  const CoachMoment* m = find(big, RiskAboveBudget);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup);
  CHECK(*m->evidence.risk == M("250"));  // 50 x (100 - 95), at the close of the decision
  CHECK(*m->evidence.budget == M("200"));
  CHECK(find(run("30"), RiskAboveBudget) == nullptr);  // 150
}

TEST_CASE("Coach held_into_release_unhedged and stayed_flat_before_release: the decision before an announced release") {
  Scenario sc = market(flat(20));
  sc.news.push_back({kWarmup + 10, NewsKind::RateDecision, 0, 100, 0, 0, 6});
  Driver held(sc);
  REQUIRE(held.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  const std::vector<CoachMoment> hm = finish(held);
  const CoachMoment* m = find(hm, HeldIntoReleaseUnhedged);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup + 9);
  CHECK(*m->evidence.release_step == kWarmup + 10);
  CHECK(*m->evidence.qty == Q("10"));
  CHECK(find(hm, StayedFlatBeforeRelease) == nullptr);

  Driver stopped(sc);  // a working stop counts as protection
  REQUIRE(stopped.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "98", nullptr).accepted);
  CHECK(find(finish(stopped), HeldIntoReleaseUnhedged) == nullptr);

  Driver flat_d(sc);  // traded, then flat before the release
  REQUIRE(flat_d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "98", nullptr).accepted);
  flat_d.step(3);
  REQUIRE(flat_d.close().accepted);
  const std::vector<CoachMoment> fm = finish(flat_d);
  const CoachMoment* g = find(fm, StayedFlatBeforeRelease);
  REQUIRE(g != nullptr);
  CHECK(g->step == kWarmup + 9);
  CHECK(g->tone == FindingTone::Good);

  Driver idle(sc);  // never traded: no credit for doing nothing
  CHECK(find(finish(idle), StayedFlatBeforeRelease) == nullptr);

  Scenario unannounced = sc;
  unannounced.news.front().announce_lead = 0;
  Driver blind(unannounced);
  REQUIRE(blind.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  CHECK(find(finish(blind), HeldIntoReleaseUnhedged) == nullptr);  // a surprise was not visible
}

TEST_CASE("Coach revenge_trade: an unplanned entry right after a losing close; silent 4 bars later") {
  // Long at 100 with a stop at 99; the 3rd bar falls through it.
  const Scenario sc = market(concat({100.0, 100.0, 98.5}, flat(17, 98.5)));
  const auto run = [&sc](int wait) {
    Driver d(sc);
    REQUIRE(d.plan(OrderSide::Buy, "99", "10").accepted);
    REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "99", nullptr).accepted);
    d.step(2);  // the close of the bar the stop filled on
    REQUIRE(d.state().position.flat());
    d.step(wait);
    REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "20").accepted);
    return finish(d);
  };
  const std::vector<CoachMoment> ms = run(0);
  const CoachMoment* m = find(ms, RevengeTrade);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup + 2);
  CHECK(*m->evidence.exit_step == kWarmup + 2);
  CHECK(*m->evidence.size_ratio_bp == 20000);
  CHECK_FALSE(*m->evidence.planned);
  CHECK(find(run(3), RevengeTrade) != nullptr);
  CHECK(find(run(4), RevengeTrade) == nullptr);
  const CoachMoment* cut = find(ms, CutAtInvalidation);  // the stop at the plan's invalidation was honored
  REQUIRE(cut != nullptr);
  CHECK(cut->step == kWarmup + 2);
  CHECK(*cut->evidence.planned_risk == M("10"));
}

TEST_CASE("Coach fomo_entry: a chase after 4 rising closes far above SMA(20); silent on a flat entry") {
  const Scenario sc = market(concat({101.0, 102.0, 103.0, 104.0, 105.0}, flat(15, 105.0)));
  Driver d(sc);
  d.step(4);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  const std::vector<CoachMoment> ms = finish(d);
  const CoachMoment* m = find(ms, FomoEntry);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup + 4);
  CHECK(*m->evidence.closes == 5);
  CHECK(*m->evidence.extension_atr > 200);
  CHECK(*m->evidence.stop == Price{});
  const Scenario quiet = market(flat(20));
  Driver calm(quiet);
  REQUIRE(calm.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  CHECK(find(finish(calm), FomoEntry) == nullptr);
}

TEST_CASE("Coach averaging_down: an add while losing that lifts the risk above the first plan; silent within it") {
  const Scenario sc = market(concat({100.0, 99.0}, flat(18, 99.0)));
  const auto run = [&sc](const char* add) {
    Driver d(sc);
    REQUIRE(d.plan(OrderSide::Buy, "95", "60").accepted);
    REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", nullptr).accepted);
    d.step(2);
    REQUIRE(d.order(OrderType::Market, OrderSide::Buy, add).accepted);
    return finish(d);
  };
  const std::vector<CoachMoment> ms = run("20");
  const CoachMoment* m = find(ms, AveragingDown);
  REQUIRE(m != nullptr);
  CHECK(m->step == kWarmup + 2);
  CHECK(*m->evidence.avg_before == P("100"));
  CHECK(*m->evidence.avg_after == P("99.33333333"));  // (10 x 100 + 20 x 99) / 30
  CHECK(*m->evidence.plan_risk == M("60"));
  CHECK(*m->evidence.risk_before == M("50"));            // 10 x (100 - 95)
  CHECK(*m->evidence.risk_after == M("129.9999999"));    // 30 x (99.33333333 - 95)
  CHECK(find(run("0.1"), AveragingDown) == nullptr);  // 10.1 units: about 50.4, within the first plan's 60
}

TEST_CASE("Coach risk_limit_exceeded: the first close of each breach of a declared limit") {
  Scenario sc = market(flat(20));
  sc.mission.risk_limits.delta_bp = 1000;  // |delta notional| up to 10% of equity
  Driver d(sc);
  REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "20", nullptr, "98", nullptr).accepted);
  d.step(3);
  REQUIRE(d.close().accepted);
  d.step(2);
  REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "20", nullptr, "98", nullptr).accepted);
  const std::vector<CoachMoment> ms = finish(d);
  std::vector<std::uint32_t> steps;
  for (const CoachMoment& m : ms) {
    if (m.code == RiskLimitExceeded) steps.push_back(m.step);
  }
  CHECK(steps == std::vector<std::uint32_t>{kWarmup + 1, kWarmup + 6});
  const CoachMoment* m = find(ms, RiskLimitExceeded);
  CHECK(*m->evidence.limit == RiskLimitKind::Delta);
  CHECK(*m->evidence.limit_bp == 1000);
  CHECK(*m->evidence.value_bp == 2000);  // 2,000 of notional on 10,000 of equity
  Driver small(sc);
  REQUIRE(small.bracket(OrderType::Market, OrderSide::Buy, "5", nullptr, "98", nullptr).accepted);
  CHECK(find(finish(small), RiskLimitExceeded) == nullptr);
}

TEST_CASE("Coach stop_to_breakeven_after_1r and partial_at_target: the managed winner's good moments") {
  // Long 10 at 100, stop 99 (1R = 10); the close reaches 102 (2R).
  const Scenario sc = market(concat({100.0, 100.5, 102.0, 102.0}, flat(16, 102.0)));
  Driver d(sc);
  REQUIRE(d.plan(OrderSide::Buy, "99", "10").accepted);
  REQUIRE(d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "99", nullptr).accepted);
  d.step(2);
  REQUIRE(d.move(child_stop(d.state()), "100.1").accepted);
  REQUIRE(d.order(OrderType::Market, OrderSide::Sell, "5", nullptr, true).accepted);
  const std::vector<CoachMoment> ms = finish(d);
  const CoachMoment* be = find(ms, StopToBreakevenAfter1r);
  REQUIRE(be != nullptr);
  CHECK(be->step == kWarmup + 2);
  CHECK(*be->evidence.profit_r_bp == 20000);
  CHECK(be->tone == FindingTone::Good);
  const CoachMoment* part = find(ms, PartialAtTarget);
  REQUIRE(part != nullptr);
  CHECK(part->step == kWarmup + 3);  // the bar the exit filled on
  CHECK(*part->evidence.qty == Q("5"));
  CHECK(*part->evidence.price == P("102"));
  CHECK(*part->evidence.profit_r_bp == 20000);  // (102 - 100) x 10 / 10

  Driver early(sc);  // a breakeven stop at 0.5R (close 100.5) is not credited
  REQUIRE(early.plan(OrderSide::Buy, "99", "10").accepted);
  REQUIRE(early.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "99", nullptr).accepted);
  early.step();
  REQUIRE(early.move(child_stop(early.state()), "100.1").accepted);
  CHECK(find(finish(early), StopToBreakevenAfter1r) == nullptr);
}

namespace {

// A fixed script by step (no decision reads a price), so it can be played on altered markets.
void scripted(Driver& d) {
  const std::uint32_t w = d.scenario().mission.warmup_bars;
  const std::function<void(std::uint32_t)> act = [&d, w](std::uint32_t s) {
    if (s == w + 2) (void)d.order(OrderType::Market, OrderSide::Buy, "10");
    if (s == w + 8) (void)d.order(OrderType::Market, OrderSide::Buy, "20");
    if (s == w + 9) (void)d.order(OrderType::StopMarket, OrderSide::Sell, "30", "50", true);
    if (s == w + 30) (void)d.close();
    if (s == w + 31) (void)d.order(OrderType::Market, OrderSide::Buy, "40");
  };
  for (int i = 0; i < 1000 && d.state().status == RunStatus::Active; ++i) {
    act(d.state().step);
    d.step();
  }
}

}  // namespace

TEST_CASE("Coach review: no look-ahead, bars after a step replaced leave every moment up to it unchanged") {
  for (const char* id : {"m25-the-dip", "m23-second-chance", "m11-news-calendar"}) {
    CAPTURE(id);
    Scenario sc;
    REQUIRE(build_mission(id, 2026, sc));
    Driver d(sc);
    scripted(d);
    const std::vector<CoachMoment> base = coach_review(d.scenario(), d.state());
    REQUIRE(!base.empty());
    for (const std::uint32_t cut : {sc.mission.warmup_bars + 5, sc.mission.warmup_bars + 20, sc.mission.warmup_bars + 40}) {
      Scenario alt = sc;
      for (std::size_t i = cut + 1; i < alt.bars.size(); ++i) {  // a different future: prices 7% lower
        Bar& b = alt.bars[i];
        for (Price* p : {&b.open, &b.high, &b.low, &b.close}) p->raw = p->raw / 100 * 93;
      }
      Driver a(alt);
      scripted(a);
      const std::vector<CoachMoment> other = coach_review(a.scenario(), a.state());
      const auto upto = [cut](const std::vector<CoachMoment>& ms) {
        std::vector<std::pair<std::uint32_t, CoachMomentCode>> out;
        for (const CoachMoment& m : ms) {
          if (m.step <= cut) out.emplace_back(m.step, m.code);
        }
        return out;
      };
      CHECK(upto(base) == upto(other));
    }
  }
}

TEST_CASE("Coach review: at most 9 warnings and 3 good moments, in step order") {
  // A long cycled every 4 bars without a stop: one entry_without_stop per position.
  const Scenario sc = market(flat(80));
  Driver d(sc);
  for (int k = 0; k < 18; ++k) {
    REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
    d.step(2);
    REQUIRE(d.close().accepted);
    d.step(2);
  }
  const std::vector<CoachMoment> ms = finish(d);
  CHECK(std::count_if(ms.begin(), ms.end(), [](const CoachMoment& m) { return m.tone == FindingTone::Warn; }) == 9);
  CHECK(std::is_sorted(ms.begin(), ms.end(), [](const CoachMoment& a, const CoachMoment& b) { return a.step < b.step; }));
  CHECK(ms.front().step == kWarmup + 1);  // equal impact: the earliest are kept
}

TEST_CASE("Coach review through the C ABI: refused while active, the same answer twice, nothing changed") {
  tl_run* run = tl_run_create("m23-second-chance", 3);
  REQUIRE(run != nullptr);
  CHECK(std::string(tl_coach_review(run)) == R"({"error":"run_active"})");
  const std::string before_active = tl_state(run);
  CHECK(std::string(tl_state(run)) == before_active);
  REQUIRE(std::string(tl_place_order(run, 1, 0, 0, "10", nullptr, 0)).find("\"accepted\":true") != std::string::npos);
  tl_advance(run, 100000);
  const std::string state = tl_state(run);
  const std::string first = tl_coach_review(run);
  const std::string second = tl_coach_review(run);
  CHECK(first == second);
  CHECK(first.find("\"code\":\"entry_without_stop\"") != std::string::npos);
  CHECK(first.find("\"lesson\":\"risk-sizing\"") != std::string::npos);
  CHECK(std::string(tl_state(run)) == state);
  tl_run_destroy(run);
}

