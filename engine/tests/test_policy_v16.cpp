// Execution policy v16: capacity priority by price then sequence, percentage trailing stops and
// maintenance-margin tiers by notional (spec order-execution "Shared capacity", "Trailing stop";
// perpetual-futures "Contract model", "Liquidation price").

#include <doctest/doctest.h>

#include <string>

#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/checkpoint.hpp"
#include "tradelab/scoring.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

constexpr const char* kCapacity5 = "0.000005";  // 1,000,000 bar volume -> 5 contracts per bar

const Event* fill_of(const RunState& st, std::uint32_t order_id) {
  for (const Event& e : st.events) {
    if (e.type == EventType::Fill && e.order_id == order_id) return &e;
  }
  return nullptr;
}

const Event* update_of(const RunState& st, std::uint32_t order_id, Reason reason) {
  for (const Event& e : st.events) {
    if (e.type == EventType::OrderUpdate && e.order_id == order_id && e.reason == reason) return &e;
  }
  return nullptr;
}

// Base tier 0.5% up to 20x; from 50,000 notional 1% and at most 10x.
void add_tiers(Scenario& sc) {
  sc.contract.maintenance_margin = R("0.005");
  sc.contract.margin_tiers = {{M("50000"), R("0.01"), 10}};
}

bool has(const std::string& json, const std::string& needle) { return json.find(needle) != std::string::npos; }

}  // namespace

// ---- 4.3 capacity priority -----------------------------------------------------------------

TEST_CASE("Capacity: the better-priced limit is served first, whatever the placement order") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "97", "99"}, {"99", "99", "99", "99"}});
  sc.costs.capacity_fraction = R(kCapacity5);
  Driver d(sc);
  const auto far = d.order(OrderType::Limit, OrderSide::Buy, "5", "98");   // placed first
  const auto near = d.order(OrderType::Limit, OrderSide::Buy, "5", "99");  // reached first on the way down
  d.step();
  REQUIRE(fill_of(d.state(), near.order_id) != nullptr);
  CHECK(fill_of(d.state(), near.order_id)->qty == Q("5"));
  CHECK(fill_of(d.state(), far.order_id) == nullptr);  // no capacity left: still resting
  CHECK(find_order(d.state(), far.order_id)->active());
  CHECK(d.state().position.qty == Q("5"));
}

TEST_CASE("Capacity: at the same price the earlier sequence is served first") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "97", "99"}, {"99", "99", "99", "99"}});
  sc.costs.capacity_fraction = R(kCapacity5);
  Driver d(sc);
  const auto first = d.order(OrderType::Limit, OrderSide::Buy, "5", "99");
  const auto second = d.order(OrderType::Limit, OrderSide::Buy, "5", "99");
  d.step();
  CHECK(fill_of(d.state(), first.order_id) != nullptr);
  CHECK(fill_of(d.state(), second.order_id) == nullptr);
}

TEST_CASE("Capacity at a gap: the level the gap reached first is served first") {
  // Close 100, next open 90: on the way down 98 is reached before 95. Both would fill at the open.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"90", "91", "89", "90"}, {"90", "90", "90", "90"}});
  sc.costs.capacity_fraction = R(kCapacity5);
  Driver d(sc);
  const auto deep = d.order(OrderType::Limit, OrderSide::Buy, "5", "95");
  const auto shallow = d.order(OrderType::Limit, OrderSide::Buy, "5", "98");
  d.step();
  REQUIRE(fill_of(d.state(), shallow.order_id) != nullptr);
  CHECK(fill_of(d.state(), shallow.order_id)->price == P("90"));
  CHECK(fill_of(d.state(), deep.order_id) == nullptr);
}

TEST_CASE("Capacity: favorable exits nearer the open are served first") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "106", "100", "105"},
                               {"105", "105", "105", "105"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step();
  REQUIRE(d.state().position.qty == Q("10"));
  sc.costs.capacity_fraction = R(kCapacity5);  // the driver reads `sc` by reference
  const auto far = d.order(OrderType::TakeProfit, OrderSide::Sell, "5", "105", true);
  const auto near = d.order(OrderType::Limit, OrderSide::Sell, "5", "103", true);
  d.step();
  CHECK(fill_of(d.state(), near.order_id) != nullptr);
  CHECK(fill_of(d.state(), far.order_id) == nullptr);
  CHECK(d.state().position.qty == Q("5"));
}

// ---- 4.7 percentage trailing stop ------------------------------------------------------------

TEST_CASE("Percentage trailing stop follows the best close by its rate, effective the next bar") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "111", "100", "110"},  // high would tighten more; the close sets it
                               {"110", "110", "107", "108"},  // low 107 crosses 107.80
                               {"108", "108", "108", "108"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();
  const auto trail = d.trailing_rate(OrderSide::Sell, "1", "0.02");
  REQUIRE(trail.accepted);
  CHECK(find_order(d.state(), trail.order_id)->price == P("98"));  // 100 - 2%
  CHECK(find_order(d.state(), trail.order_id)->trail_rate == R("0.02"));
  d.step();
  CHECK(find_order(d.state(), trail.order_id)->price == P("107.8"));  // 110 - 2.2
  d.step();
  const Event* f = fill_of(d.state(), trail.order_id);
  REQUIRE(f != nullptr);
  CHECK(f->requested == P("107.8"));
  CHECK(f->price == P("107.8"));
  CHECK(d.state().position.flat());
}

TEST_CASE("Percentage trailing stop: the rate distance rounds outward onto the tick grid") {
  Scenario sc = make_scenario({{"100.37", "100.37", "100.37", "100.37"},
                               {"100.37", "100.37", "100.37", "100.37"},
                               {"100.37", "100.37", "100.37", "100.37"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Sell, "1");
  d.step();
  // 100.37 * 1.5% = 1.50555: a short's stop sits above, ceil(101.87555) = 101.88.
  const auto buy = d.trailing_rate(OrderSide::Buy, "1", "0.015");
  REQUIRE(buy.accepted);
  CHECK(find_order(d.state(), buy.order_id)->price == P("101.88"));
}

TEST_CASE("Percentage trailing stop: missing, out-of-range or doubled distances are refused") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();
  CHECK(d.trailing_rate(OrderSide::Sell, "1", "1").reason == Reason::InvalidTick);
  CHECK(d.trailing_rate(OrderSide::Sell, "1", "-0.01").reason == Reason::InvalidTick);
  Command both;
  both.type = CommandType::PlaceOrder;
  both.order_type = OrderType::TrailingStop;
  both.side = OrderSide::Sell;
  both.qty = Q("1");
  both.reduce_only = true;
  both.trail_distance = P("1");
  both.trail_rate = R("0.01");
  both.seq = 100;
  both.client_id = 100;
  both.decision_step = d.state().step;
  CHECK(apply_command(d.scenario(), d.state(), both).reason == Reason::InvalidTick);
}

TEST_CASE("Percentage trailing stop survives a checkpoint and changes the state digest") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  Driver a(sc);
  a.order(OrderType::Market, OrderSide::Buy, "1");
  a.step();
  Driver b(sc);
  b.order(OrderType::Market, OrderSide::Buy, "1");
  b.step();
  a.trailing_rate(OrderSide::Sell, "1", "0.02");
  b.trailing(OrderSide::Sell, "1", "2");  // the same trigger (98) by a fixed distance
  CHECK(a.state().orders.back().price == b.state().orders.back().price);
  CHECK(state_digest(a.state()) != state_digest(b.state()));
  const auto bytes = save_checkpoint(a.state(), sc.id);
  const auto back = load_checkpoint(bytes, sc.id);
  REQUIRE(back.ok());
  CHECK(back.value().orders.back().trail_rate == R("0.02"));
  CHECK(state_digest(back.value()) == state_digest(a.state()));
}

TEST_CASE("C ABI: tl_place_trailing_rate places a percentage trail and the state names its rate") {
  tl_run* run = tl_run_create("m07-trend-trailing", 3);
  REQUIRE(run != nullptr);
  CHECK(has(tl_place_order(run, 1, 0, 0, "1", nullptr, 0), "\"accepted\":true"));
  tl_advance(run, 1);
  CHECK(has(tl_place_trailing_rate(run, 2, 1, "1", 1, "0.03"), "\"accepted\":true"));
  CHECK(has(tl_state(run), "\"trailRate\":\"0.0300000000\""));
  CHECK(has(tl_place_trailing_rate(run, 3, 1, "1", 1, "1.5"), "invalid_tick"));
  CHECK(has(tl_place_trailing_rate(run, 4, 1, "1", 1, "abc"), "invalid_tick"));
  tl_run_destroy(run);
}

// ---- 5.6 maintenance-margin tiers ------------------------------------------------------------

TEST_CASE("Margin tier: the liquidation price uses the rate of the position's notional at entry") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  add_tiers(sc);
  Driver small(sc);
  small.leverage(10);
  small.order(OrderType::Market, OrderSide::Buy, "400");  // 40,000: base tier 0.5%
  small.step();
  Price liq;
  REQUIRE(detail_liq(small, &liq));
  CHECK(liq == P("90.45226131"));  // (40,000 - 4,000) / (400 * 0.995), rounded up
  Driver big(sc);
  big.leverage(10);
  big.order(OrderType::Market, OrderSide::Buy, "600");  // 60,000: tier 1%
  big.step();
  REQUIRE(detail_liq(big, &liq));
  CHECK(liq == P("90.90909091"));  // (60,000 - 6,000) / (600 * 0.99), rounded up
}

TEST_CASE("Margin tier: an order whose position would exceed its tier's leverage is refused") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  add_tiers(sc);
  Driver d(sc);
  d.leverage(20);
  CHECK(d.order(OrderType::Market, OrderSide::Buy, "300").accepted);  // 30,000 at 20x: base tier
  const auto big = d.order(OrderType::Market, OrderSide::Buy, "600");   // 60,000 at 20x: tier caps 10x
  CHECK_FALSE(big.accepted);
  CHECK(big.reason == Reason::LeverageAboveTier);
  d.leverage(10);
  CHECK(d.order(OrderType::Market, OrderSide::Buy, "600").accepted);
}

TEST_CASE("Margin tier: raising leverage above the tier of the open position is refused") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  add_tiers(sc);
  Driver d(sc);
  d.leverage(10);
  d.order(OrderType::Market, OrderSide::Buy, "600");
  d.step();
  CHECK(d.leverage(15).reason == Reason::LeverageAboveTier);
  CHECK(d.leverage(8).accepted);
}

TEST_CASE("Margin tier: resting orders that together cross into a higher tier are checked at fill") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "97", "99"}, {"99", "99", "99", "99"}});
  add_tiers(sc);
  Driver d(sc);
  d.leverage(20);
  const auto a = d.order(OrderType::Limit, OrderSide::Buy, "300", "99");  // each alone: 29,700 notional
  const auto b = d.order(OrderType::Limit, OrderSide::Buy, "300", "98");
  REQUIRE(a.accepted);
  REQUIRE(b.accepted);
  d.step();
  CHECK(fill_of(d.state(), a.order_id) != nullptr);
  CHECK(fill_of(d.state(), b.order_id) == nullptr);
  CHECK(update_of(d.state(), b.order_id, Reason::LeverageAboveTier) != nullptr);
  CHECK(d.state().position.qty == Q("300"));
}

TEST_CASE("Margin tier: validation refuses unordered tiers or a cap that leaves no initial margin") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  add_tiers(sc);
  CHECK(validate(sc) == ScenarioError::Ok);
  sc.contract.margin_tiers.push_back({M("40000"), R("0.02"), 5});  // floor below the previous one
  CHECK(validate(sc) == ScenarioError::InvalidContract);
  sc.contract.margin_tiers.back() = {M("100000"), R("0.005"), 5};  // rate below the previous one
  CHECK(validate(sc) == ScenarioError::InvalidContract);
  sc.contract.margin_tiers.back() = {M("100000"), R("0.02"), 20};  // leverage above the previous one
  CHECK(validate(sc) == ScenarioError::InvalidContract);
  sc.contract.margin_tiers.back() = {M("100000"), R("0.2"), 5};  // 5 x 20% = 1: liquidatable at entry
  CHECK(validate(sc) == ScenarioError::InvalidContract);
  sc.contract.margin_tiers.back() = {M("100000"), R("0.02"), 5};
  CHECK(validate(sc) == ScenarioError::Ok);
}

TEST_CASE("Margin tier: the debrief's safe leverage uses the tier of the trade") {
  // Long 600 at 100, 10x, stop 90.60. At 0.5% liquidation is 90.45 (stop in front); in the 1% tier it is
  // 90.91 (stop beyond), and 100 (1 - 1/L) / 0.99 < 90.60 needs L < 9.7: 9x would have kept it in front.
  const auto run = [](bool tiers) {
    Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "90", "92"},
                                 {"92", "92", "92", "92"}});
    if (tiers) add_tiers(sc);
    else sc.contract.maintenance_margin = R("0.005");
    Driver d(sc);
    d.leverage(10);
    d.plan(OrderSide::Buy, "90.6", "50");
    d.bracket(OrderType::Market, OrderSide::Buy, "600", nullptr, "90.6", nullptr);
    d.step(3);
    return score_run(sc, d.state());
  };
  const ScoreReport flat_rate = run(false);
  REQUIRE(flat_rate.trades.size() == 1);
  CHECK_FALSE(flat_rate.trades[0].stop_beyond_liq);
  const ScoreReport tiered = run(true);
  REQUIRE(tiered.trades.size() == 1);
  CHECK(tiered.trades[0].stop_beyond_liq);
  CHECK(tiered.trades[0].liq_price == P("90.90909091"));
  CHECK(tiered.trades[0].safe_leverage == 9);
}

TEST_CASE("C ABI: the contract lists every margin tier, the base one first") {
  tl_run* run = tl_run_create("m01-false-breakout", 1);
  REQUIRE(run != nullptr);
  const std::string s = tl_state(run);
  CHECK(has(s, "\"marginTiers\":[{\"notionalFloor\":\"0.00000000\",\"maintenanceMargin\":\"0.0050000000\",\"maxLeverage\":50}"));
  CHECK(has(s, "{\"notionalFloor\":\"50000.00000000\",\"maintenanceMargin\":\"0.0100000000\",\"maxLeverage\":20}"));
  CHECK(has(s, "{\"notionalFloor\":\"1000000.00000000\",\"maintenanceMargin\":\"0.0500000000\",\"maxLeverage\":5}]"));
  tl_run_destroy(run);
}
