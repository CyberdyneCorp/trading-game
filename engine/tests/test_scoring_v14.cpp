// Scoring v14 (add-practice-and-advanced-strategies design D13, report-only): the bias findings with
// their evidence, risk_limit_exceeded, the position episodes and the strategy comparison of m18-m22.
// Each rule fires on a scripted run and stays silent on a near miss; altering bars after the step it
// judges leaves a finding unchanged; and none of it moves a score.

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "learnability.hpp"
#include "tradelab/c_api.h"
#include "tradelab/whatif.hpp"

using namespace tradelab;
using namespace tradelab::learn;

namespace {

std::vector<Finding> of_code(const Play& p, FindingCode code) {
  const ScoreReport r = score_run(p.sc, p.st);
  std::vector<Finding> out;
  for (const Finding& f : r.findings) {
    if (f.code == code) out.push_back(f);
  }
  return out;
}

void plan(Play& p, OrderSide side, double invalidation, double risk) {
  Command c;
  c.type = CommandType::RecordPlan;
  c.plan.side = side;
  c.plan.has_lose_scenario = true;
  c.plan.invalidation = ticks(invalidation);
  c.plan.confidence_pct = 60;
  c.plan.planned_risk = Money::from_raw(static_cast<std::int64_t>(std::llround(risk * 100.0)) * (Money::kScale / 100));
  REQUIRE(p.send(c).accepted);
}

// Steps until the position is flat (a stop filled) or `limit` bars passed.
void until_flat(Play& p, std::uint32_t limit) {
  for (std::uint32_t i = 0; i < limit && p.active() && !p.flat(); ++i) p.step();
}

// A planned long whose 1 x ATR stop fills, then (at `delay` decisions after the stop-out) a long of `mult`
// times the quantity, with a plan of `mult` x the risk when `planned`.
Play revenge_run(std::uint64_t seed, std::uint32_t delay, double mult, bool planned) {
  Play p("m23-second-chance", seed);
  p.step(14);
  const double c = p.close(p.st.step);
  const double stop = c - p.atr_at(p.st.step);
  const double q = std::floor(100.0 / (c - stop) * 10.0) / 10.0;
  plan(p, OrderSide::Buy, stop, 100.0);
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(q), Price{}, ticks(stop)).accepted);
  p.step();
  until_flat(p, 80);
  REQUIRE(p.flat());
  p.step(delay);
  const double c2 = p.close(p.st.step);
  if (planned) plan(p, OrderSide::Buy, c2 - p.atr_at(p.st.step), 100.0 * mult);
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(q * mult), Price{}, ticks(c2 - p.atr_at(p.st.step))).accepted);
  p.finish();
  return p;
}

}  // namespace

TEST_CASE("Scoring v14 revenge_trade: an unplanned or doubled entry within 3 bars of a loss; silent on a planned same-size one") {
  std::optional<std::uint64_t> seed;
  for (std::uint64_t s = 1; s <= 30 && !seed; ++s) {
    const Play p = revenge_run(s, 0, 2.0, false);
    const auto trades = build_trades(p.sc, p.st);
    if (!trades.empty() && trades.front().net.raw < 0) seed = s;
  }
  REQUIRE(seed);
  const Play doubled = revenge_run(*seed, 0, 2.0, false);
  const std::vector<Finding> f = of_code(doubled, FindingCode::RevengeTrade);
  REQUIRE(f.size() == 1);
  REQUIRE(f.front().evidence);
  const FindingEvidence& ev = *f.front().evidence;
  CHECK(ev.trade == 1);
  CHECK(ev.entry_step == ev.exit_step);
  CHECK(ev.ratio_bp == 20000);  // an unplanned entry is compared by quantity: twice the losing trade's
  CHECK_FALSE(ev.planned);
  CHECK(ev.loss.raw > 0);
  CHECK(f.front().tone == FindingTone::Warn);
  CHECK(of_code(revenge_run(*seed, 0, 1.0, true), FindingCode::RevengeTrade).empty());       // planned, same risk
  CHECK(of_code(revenge_run(*seed, 4, 2.0, false), FindingCode::RevengeTrade).empty());      // 4 bars later
  CHECK(of_code(revenge_run(*seed, 3, 2.0, false), FindingCode::RevengeTrade).size() == 1);  // the 3rd bar still counts
  const std::vector<Finding> planned_double = of_code(revenge_run(*seed, 1, 2.0, true), FindingCode::RevengeTrade);
  REQUIRE(planned_double.size() == 1);  // planned, but at twice the planned risk
  CHECK(planned_double.front().evidence->ratio_bp == 20000);
  CHECK(planned_double.front().evidence->planned);
}

namespace {

// The first decision after `from` where 4 closes rose and the close is more than 2 ATR above SMA(20).
std::optional<std::uint32_t> chase_step(const Play& p, std::uint32_t from) {
  const Series ma = sma(p.sc, p.last(), 20);
  for (std::uint32_t i = from; i + 2 <= p.last(); ++i) {
    bool rising = true;
    for (std::uint32_t k = 0; k < 4; ++k) rising = rising && p.close(i - k) > p.close(i - k - 1);
    if (rising && ma[i] && p.close(i) - num(ma[i]) > 2.0 * p.atr_at(i)) return i;
  }
  return std::nullopt;
}

Play chase_run(const Scenario* altered, std::uint64_t seed, double stop_atr) {
  Play p("m24-fast-market", seed);
  if (altered != nullptr) p.sc = *altered;
  const std::optional<std::uint32_t> at = chase_step(p, p.st.step);
  REQUIRE(at);
  p.step(*at - p.st.step);
  const double c = p.close(p.st.step);
  const Price stop = stop_atr > 0.0 ? ticks(c - stop_atr * p.atr_at(p.st.step)) : Price{};
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(10), Price{}, stop).accepted);
  p.finish();
  return p;
}

}  // namespace

TEST_CASE("Scoring v14 fomo_entry: a chase without a stop or with a far one; silent with a stop within 2 ATR") {
  const Play no_stop = chase_run(nullptr, 3, 0.0);
  const std::vector<Finding> f = of_code(no_stop, FindingCode::FomoEntry);
  REQUIRE(f.size() == 1);
  const FindingEvidence& ev = *f.front().evidence;
  CHECK(ev.closes >= 4);
  CHECK(ev.extension_atr > 200);
  CHECK(ev.stop.raw == 0);
  CHECK(of_code(chase_run(nullptr, 3, 3.0), FindingCode::FomoEntry).size() == 1);  // a stop 3 ATR away
  CHECK(of_code(chase_run(nullptr, 3, 1.5), FindingCode::FomoEntry).empty());      // within 2 ATR

  // No look-ahead: bars after the entry decision moved (a crash) leave the finding as it was.
  Scenario altered = no_stop.sc;
  for (std::uint32_t i = ev.entry_step + 2; i < altered.bars.size(); ++i) {
    Bar& b = altered.bars[i];
    for (Price* px : {&b.open, &b.high, &b.low, &b.close}) *px = ticks(num(*px) * 0.8);
  }
  const std::vector<Finding> g = of_code(chase_run(&altered, 3, 0.0), FindingCode::FomoEntry);
  REQUIRE(g.size() == 1);
  CHECK(g.front().step == f.front().step);
  CHECK(g.front().evidence->extension_atr == ev.extension_atr);
  CHECK(g.front().evidence->closes == ev.closes);
}

namespace {

// A planned long (3 ATR stop, `first_risk` planned) and, once the close is an ATR lower, an add of `mult`
// times the quantity with the same stop (or none).
std::optional<Play> average_run(std::uint64_t seed, double mult, bool keep_stop, double first_risk) {
  Play p("m25-the-dip", seed);
  p.step(20);
  const double c = p.close(p.st.step);
  const double a = p.atr_at(p.st.step);
  const double stop = c - 3.0 * a;
  const double q = std::floor(100.0 / (c - stop) * 10.0) / 10.0;
  plan(p, OrderSide::Buy, stop, first_risk);
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(q), Price{}, ticks(stop)).accepted);
  p.step();
  while (p.active() && !p.flat() && p.close(p.st.step) > c - a) p.step();
  if (!p.active() || p.flat()) return std::nullopt;
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(q * mult), Price{}, keep_stop ? ticks(stop) : Price{}).accepted);
  p.finish();
  return p;
}

}  // namespace

TEST_CASE("Scoring v14 averaging_down: an add while losing beyond the first plan's risk or without a stop; silent within it") {
  std::optional<std::uint64_t> seed;
  for (std::uint64_t s = 1; s <= 40 && !seed; ++s) {
    if (average_run(s, 2.0, true, 100.0)) seed = s;
  }
  REQUIRE(seed);
  const Play doubled = *average_run(*seed, 2.0, true, 100.0);
  const std::vector<Finding> f = of_code(doubled, FindingCode::AveragingDown);
  REQUIRE(f.size() == 1);
  const FindingEvidence& ev = *f.front().evidence;
  CHECK(ev.avg_after < ev.avg_before);
  CHECK(ev.loss.raw > 0);
  CHECK(ev.risk_after > ev.risk_before);
  CHECK(ev.risk_after > ev.risk_plan);
  CHECK(ev.risk_plan == Money::from_raw(100 * Money::kScale));
  CHECK(of_code(*average_run(*seed, 0.1, false, 100.0), FindingCode::AveragingDown).size() == 1);  // no stop on the add
  // Planned at entry with room for the add: total risk to the same stop within the first plan.
  CHECK(of_code(*average_run(*seed, 0.1, true, 400.0), FindingCode::AveragingDown).empty());
}

TEST_CASE("Scoring v14 risk_limit_exceeded: counted at each close from the engine's own risk block; silent within the limit") {
  for (const double perp : {25.0, 0.0}) {
    Play p("m18-gamma-scalp", 3);
    const std::uint32_t e = p.expiries().front();
    REQUIRE(p.option(p.strike_near(OptionKind::Call, e, p.close(p.st.step))->id, OrderSide::Buy, lots(4)).accepted);
    if (perp > 0.0) REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(perp)).accepted);
    std::uint32_t over = 0;
    std::uint32_t first = 0;
    while (p.active()) {
      p.step();
      WhatIfRequest req;
      req.holdings = true;
      const WhatIfResult r = run_option_whatif(p.sc, p.st, req);
      const bool exceeded = r.risk && std::any_of(r.risk->limits.begin(), r.risk->limits.end(), [](const RiskLimitCheck& c) { return c.exceeded; });
      if (exceeded && over++ == 0) first = p.st.step;
    }
    const std::vector<Finding> f = of_code(p, FindingCode::RiskLimitExceeded);
    CAPTURE(perp);
    if (perp == 0.0) {  // 4 calls: at most 4 units of delta, far inside 15% of equity
      CHECK(over == 0);
      CHECK(f.empty());
      continue;
    }
    REQUIRE(f.size() == 1);
    CHECK(f.front().step == first);
    CHECK(f.front().count == over);
    CHECK(f.front().evidence->limit == RiskLimitKind::Delta);
    CHECK(f.front().evidence->limit_bp == 1500);
  }
  Play none("m13-call-spread", 3);  // no declared limit, never the finding
  REQUIRE(none.order(OrderType::Market, OrderSide::Buy, lots(90)).accepted);
  none.finish();
  CHECK(of_code(none, FindingCode::RiskLimitExceeded).empty());
}

TEST_CASE("Scoring v14 episodes: scale-ins, a partial exit, a stop moved to breakeven and a trailing stop, with MFE and MAE in R") {
  Play p("m22-scale-manage", 5);
  p.step(14);
  const double c = p.close(p.st.step);
  const double a = p.atr_at(p.st.step);
  plan(p, OrderSide::Buy, c - 2 * a, 100.0);
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(20), Price{}, ticks(c - 2 * a)).accepted);
  p.step();
  REQUIRE(p.order(OrderType::Market, OrderSide::Buy, lots(10)).accepted);  // a scale-in
  p.step();
  REQUIRE(p.order(OrderType::Market, OrderSide::Sell, lots(10)).accepted);  // a partial exit
  p.step();
  std::uint32_t stop_id = 0;
  for (const Order& o : p.st.orders) {
    if (o.active() && o.reduce_only && o.type == OrderType::StopMarket) stop_id = o.id;
  }
  REQUIRE(stop_id != 0);
  Command move;
  move.type = CommandType::MoveOrder;
  move.order_id = stop_id;
  move.price = ticks(p.st.position.entry.raw / static_cast<double>(Price::kScale) + 0.01);
  const bool moved = p.send(move).accepted;
  p.trail(ticks(3 * a));
  p.finish();
  const std::vector<Episode> eps = position_episodes(p.sc, p.st, build_trades(p.sc, p.st));
  REQUIRE(!eps.empty());
  const Episode& ep = eps.front();
  CHECK(ep.is_long);
  CHECK(ep.entries.size() == 2);
  CHECK(ep.exits.size() >= 2);
  CHECK(ep.exits.front().qty == lots(10));
  CHECK(ep.trailing);
  if (moved) {
    REQUIRE(ep.stop_moves.size() == 1);
    CHECK(ep.stop_moves.front().kind == StopMoveKind::ToBreakeven);
  }
  REQUIRE(ep.mfe_r_bp);
  REQUIRE(ep.mae_r_bp);
  CHECK(*ep.mfe_r_bp >= 0);
  CHECK(*ep.mae_r_bp >= 0);
  CHECK(ep.planned_risk == Money::from_raw(100 * Money::kScale));
}

TEST_CASE("Scoring v14 comparison m18-m22: the rows exist, and two reproduce scripted runs") {
  for (const char* m : {"m18-gamma-scalp", "m19-funding-carry", "m20-event-vol", "m21-calendar", "m22-scale-manage"}) {
    Play p(m, 3);
    p.finish();
    const auto c = strategy_comparison(p.sc, p.st);
    REQUIRE(c);
    CHECK(c->rows.size() >= 3);
    for (const ComparisonRow& r : c->rows) CHECK(r.refused == Reason::None);
  }
  // m19 hedged carry at 2x: the synthetic long on the last expiry and the short of qty x multiplier.
  Play ref("m19-funding-carry", 3);
  ref.finish();
  const auto c = strategy_comparison(ref.sc, ref.st);
  Play p("m19-funding-carry", 3);
  p.leverage(2);
  const std::uint32_t e = p.expiries().back();
  REQUIRE(p.option(p.strike_near(OptionKind::Call, e, p.close(p.st.step))->id, OrderSide::Buy, lots(10)).accepted);
  REQUIRE(p.option(p.strike_near(OptionKind::Put, e, p.close(p.st.step))->id, OrderSide::Sell, lots(10)).accepted);
  REQUIRE(p.order(OrderType::Market, OrderSide::Sell, lots(10)).accepted);
  p.finish();
  const auto row = std::find_if(c->rows.begin(), c->rows.end(), [](const ComparisonRow& r) { return r.name == ComparisonStrategy::HedgedCarry2x; });
  REQUIRE(row != c->rows.end());
  CHECK(row->net_pnl.raw == account_view(p.sc, p.st).equity.raw - p.sc.mission.initial_balance.raw);
}

TEST_CASE("Scoring v14 changes no score: the version is 14 and the findings come after the lesson is picked") {
  tl_run* run = tl_run_create("m23-second-chance", 3);
  tl_advance(run, 1000);
  const std::string d = tl_debrief(run);
  CHECK(d.find("\"score\":{\"version\":14") != std::string::npos);
  CHECK(d.find("\"episodes\":[]") != std::string::npos);
  tl_run_destroy(run);
}
