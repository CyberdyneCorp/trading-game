#include <doctest/doctest.h>

#include "scenario_builder.hpp"
#include "tradelab/prng.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {
Scenario flat_then_up() {
  return make_scenario({{"100", "100", "100", "100"},
                        {"100", "101", "99", "100"},
                        {"100", "111", "100", "110"},
                        {"110", "111", "109", "110"}});
}
}  // namespace

TEST_CASE("SIM03: order after close t fills at the open of t+1, never earlier") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"105", "106", "104", "105"}, {"105", "105", "105", "105"}});
  Driver d(sc);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "1").accepted);
  CHECK(d.state().position.flat());  // accepted is not filled
  d.step();
  const Event* fill = last_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->step == 1);
  CHECK(fill->price == P("105"));
}

TEST_CASE("FUT01: long and short are mirror images before costs") {
  const Scenario sc = flat_then_up();
  Driver longd(sc);
  Driver shortd(sc);
  longd.order(OrderType::Market, OrderSide::Buy, "2");
  shortd.order(OrderType::Market, OrderSide::Sell, "2");
  longd.step(2);  // filled at the bar-1 open (100); now at the bar-2 close (110)
  shortd.step(2);
  longd.close();
  shortd.close();
  longd.step();  // closed at the open of bar 3 = 110
  shortd.step();
  CHECK(longd.state().realized_pnl == M("20"));
  CHECK(shortd.state().realized_pnl == M("-20"));
}

TEST_CASE("SIM04: gap through a stop fills from the open, not the trigger") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"90", "91", "89", "90"},
                               {"90", "90", "90", "90"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();
  REQUIRE(d.order(OrderType::StopMarket, OrderSide::Sell, "1", "95", true).accepted);
  d.step();
  const Event* fill = last_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->price == P("90"));
  CHECK(fill->requested == P("95"));
  CHECK((fill->flags & kFlagGapFill) != 0);
  CHECK(d.state().realized_pnl == M("-10"));
}

TEST_CASE("SIM05: stop and target in the same bar -> stop wins, flagged ambiguous") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "106", "94", "100"},
                               {"100", "100", "100", "100"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();
  const auto stop = d.order(OrderType::StopMarket, OrderSide::Sell, "1", "95", true);
  const auto tp = d.order(OrderType::TakeProfit, OrderSide::Sell, "1", "105", true);
  d.step();
  const Event* fill = last_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->order_id == stop.order_id);
  CHECK((fill->flags & kFlagAmbiguousIntrabar) != 0);
  CHECK(d.state().position.flat());
  for (const Order& o : d.state().orders) {
    if (o.id == tp.order_id) CHECK(o.status == OrderStatus::Cancelled);
  }
}

TEST_CASE("SIM07: a limit never fills worse than its price; touching is not enough") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "98", "99"},     // touches 98 only
                               {"99", "99", "97.5", "98"},     // trades through 98
                               {"98", "98", "98", "98"}});
  Driver d(sc);
  d.order(OrderType::Limit, OrderSide::Buy, "1", "98");
  d.step();
  CHECK(d.state().position.flat());
  d.step();
  const Event* fill = last_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->price <= P("98"));
}

TEST_CASE("FUT02: liquidation forfeits the isolated margin and never more") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "80", "85"},
                               {"85", "85", "85", "85"}});
  Driver d(sc);
  d.leverage(10);
  d.order(OrderType::Market, OrderSide::Buy, "10");  // notional 1000, margin 100
  d.step();
  CHECK(d.state().position.margin == M("100"));
  const AccountView v = d.view();
  REQUIRE(v.has_liquidation);
  CHECK(v.liquidation_price > P("90"));
  CHECK(v.liquidation_price < P("92"));
  d.step();
  const Event* liq = last_of(d.state(), EventType::Liquidation);
  REQUIRE(liq != nullptr);
  CHECK(liq->amount == M("100"));
  CHECK(d.state().wallet == M("9900"));
  CHECK(d.state().position.flat());
}

TEST_CASE("FUT04: a stop beyond the liquidation price is warned and liquidation fires first") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "87", "88"},
                               {"88", "88", "88", "88"}});
  Driver d(sc);
  d.leverage(10);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step();
  const auto stop = d.order(OrderType::StopMarket, OrderSide::Sell, "10", "88", true);
  REQUIRE(stop.accepted);
  CHECK(stop.warning == Reason::StopBeyondLiquidation);
  d.step();
  CHECK(last_of(d.state(), EventType::Liquidation) != nullptr);
  CHECK(last_of(d.state(), EventType::Fill)->step == 1);  // the stop never filled
}

TEST_CASE("Stop in front of liquidation fills before it") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "80", "85"},
                               {"85", "85", "85", "85"}});
  Driver d(sc);
  d.leverage(10);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step();
  d.order(OrderType::StopMarket, OrderSide::Sell, "10", "95", true);
  d.step();
  CHECK(last_of(d.state(), EventType::Liquidation) == nullptr);
  CHECK(d.state().realized_pnl == M("-50"));
}

TEST_CASE("FUT03: positive funding moves exactly notional * rate from longs to shorts") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}});
  sc.contract.funding_interval = 2;
  sc.funding_rate[2] = R("0.0001");
  Driver longd(sc);
  Driver shortd(sc);
  longd.order(OrderType::Market, OrderSide::Buy, "100");  // notional 10,000
  shortd.order(OrderType::Market, OrderSide::Sell, "100");
  longd.step(2);
  shortd.step(2);
  CHECK(longd.state().funding_net == M("-1"));
  CHECK(shortd.state().funding_net == M("1"));
  CHECK(longd.state().wallet == M("9999"));
}

TEST_CASE("Margin, leverage cap, reduce-only and short permission are enforced at acceptance") {
  Scenario sc = flat_then_up();
  sc.mission.leverage_cap = 5;
  Driver d(sc);
  CHECK(d.leverage(10).reason == Reason::LeverageAboveCap);
  CHECK(d.order(OrderType::Market, OrderSide::Buy, "200").reason == Reason::InsufficientMargin);  // 20k at 1x
  CHECK(d.order(OrderType::StopMarket, OrderSide::Sell, "1", "90", true).reason == Reason::ReduceOnlyWouldIncrease);
  CHECK(d.order(OrderType::Limit, OrderSide::Buy, "1", "99.999").reason == Reason::InvalidTick);
  CHECK(d.order(OrderType::Limit, OrderSide::Buy, "0.05", "99").reason == Reason::InvalidQuantity);
  sc.mission.allow_short = false;
  Driver d2(sc);
  CHECK(d2.order(OrderType::Market, OrderSide::Sell, "1").reason == Reason::ShortNotAllowed);
}

TEST_CASE("Duplicate client command id is idempotent") {
  const Scenario sc = flat_then_up();
  RunState st = start_run(sc, 1);
  Command c;
  c.seq = 1;
  c.client_id = 77;
  c.qty = Q("1");
  const auto first = apply_command(sc, st, c);
  c.seq = 2;
  const auto second = apply_command(sc, st, c);
  CHECK(first.order_id == second.order_id);
  CHECK(st.orders.size() == 1);
}

TEST_CASE("Commands from a past decision step are rejected") {
  const Scenario sc = flat_then_up();
  RunState st = start_run(sc, 1);
  advance(sc, st);
  Command c;
  c.seq = 1;
  c.client_id = 1;
  c.qty = Q("1");
  c.decision_step = 0;
  CHECK(apply_command(sc, st, c).reason == Reason::WrongDecisionStep);
}

TEST_CASE("Losing all money ends the run as bankrupt") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "1", "2"},
                               {"2", "2", "2", "2"},
                               {"2", "2", "2", "2"}});
  sc.mission.initial_balance = M("100");
  Driver d(sc);
  d.leverage(2);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "2").accepted);  // all-in: notional 200, margin 100
  d.step(2);
  CHECK(d.state().status == RunStatus::Bankrupt);
  CHECK(d.state().wallet.raw >= 0);
  CHECK(d.order(OrderType::Market, OrderSide::Buy, "0.1").reason == Reason::RunBankrupt);
}

TEST_CASE("Run finishes after the last bar and expires resting orders") {
  const Scenario sc = flat_then_up();
  Driver d(sc);
  d.order(OrderType::Limit, OrderSide::Buy, "1", "50");
  d.step(3);
  CHECK(d.state().status == RunStatus::Finished);
  CHECK(d.state().orders[0].status == OrderStatus::Expired);
}

TEST_CASE("SIM01: mutating future bars never changes what was emitted up to t") {
  Scenario a = make_scenario({{"100", "100", "100", "100"},
                              {"100", "102", "98", "101"},
                              {"101", "103", "100", "102"},
                              {"102", "104", "101", "103"},
                              {"103", "104", "102", "103"}});
  Scenario b = a;
  b.bars[3] = {P("50"), P("200"), P("40"), P("60"), Q("5"), false};
  b.bars[4] = {P("60"), P("61"), P("59"), P("60"), Q("5"), false};
  Driver da(a);
  Driver db(b);
  for (Driver* d : {&da, &db}) {
    d->leverage(3);
    d->order(OrderType::Market, OrderSide::Buy, "5");
    d->step();
    d->order(OrderType::StopMarket, OrderSide::Sell, "5", "97", true);
    d->step();  // now at t = 2
  }
  CHECK(to_hex(state_digest(da.state())) == to_hex(state_digest(db.state())));
  CHECK(to_hex(events_digest(da.state())) == to_hex(events_digest(db.state())));
}

TEST_CASE("SIM08: wallet reconciles exactly with realized P&L, fees and funding (randomized)") {
  for (std::uint64_t seed = 1; seed <= 40; ++seed) {
    Pcg32 rng(seed, Stream::Sandbox);
    Scenario sc = make_scenario({{"100", "100", "100", "100"}});
    sc.bars.clear();
    std::int64_t px = 100'00;  // cents
    for (int i = 0; i < 60; ++i) {
      const std::int64_t o = px;
      px = std::max<std::int64_t>(500, px + static_cast<std::int64_t>(rng.next_bounded(801)) - 400);
      const std::int64_t hi = std::max(o, px) + rng.next_bounded(200);
      const std::int64_t lo = std::max<std::int64_t>(100, std::min(o, px) - rng.next_bounded(200));
      auto cents = [](std::int64_t c) { return Price::from_raw(c * 1'000'000); };
      sc.bars.push_back({cents(o), cents(hi), cents(lo), cents(px), Q("50"), false});
    }
    sc.funding_rate.assign(sc.bars.size(), R("0.0003"));
    sc.contract.funding_interval = 4;
    sc.contract.taker_fee = R("0.0005");
    sc.contract.maker_fee = R("0.0002");
    sc.costs.half_spread = R("0.0002");
    sc.costs.slippage = R("0.0003");
    sc.costs.capacity_fraction = R("0.1");
    REQUIRE(validate(sc) == ScenarioError::Ok);
    Driver d(sc, seed);
    d.leverage(static_cast<int>(1 + rng.next_bounded(10)));
    while (d.state().status == RunStatus::Active) {
      const auto side = rng.next_bounded(2) == 0 ? OrderSide::Buy : OrderSide::Sell;
      switch (rng.next_bounded(5)) {
        case 0: d.order(OrderType::Market, side, "3"); break;
        case 1: d.order(OrderType::Limit, side, "2", "100"); break;
        case 2: d.order(OrderType::StopMarket, side, "2", "101", !d.state().position.flat()); break;
        case 3: d.close(); break;
        default: break;
      }
      d.step();
      const RunState& st = d.state();
      const std::int64_t expected = sc.mission.initial_balance.raw + st.realized_pnl.raw - st.fees_paid.raw + st.funding_net.raw;
      REQUIRE(st.wallet.raw == expected);
      REQUIRE(st.wallet.raw >= 0);
      REQUIRE(st.position.margin.raw >= 0);
    }
  }
}

TEST_CASE("SIM02 (native): same scenario, seed and commands -> identical digests") {
  const Scenario sc = flat_then_up();
  auto run = [&sc] {
    Driver d(sc, 9);
    d.leverage(4);
    d.order(OrderType::Market, OrderSide::Sell, "7");
    d.step();
    d.order(OrderType::StopMarket, OrderSide::Buy, "7", "105", true);
    d.step(2);
    return to_hex(state_digest(d.state())) + to_hex(events_digest(d.state()));
  };
  CHECK(run() == run());
}

TEST_CASE("Regression: the order that closes a position ends filled, and orphans are cancelled after its fill") {
  // Before: apply_fill cancelled every active reduce-only order as soon as the position went flat,
  // including the stop being filled, so the log read "cancelled: position_closed" then "filled".
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "94", "95"},
                               {"95", "95", "95", "95"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();
  const auto stop = d.order(OrderType::StopMarket, OrderSide::Sell, "1", "96", true);
  const auto tp = d.order(OrderType::TakeProfit, OrderSide::Sell, "1", "110", true);
  d.step();
  REQUIRE(d.state().position.flat());
  std::uint64_t fill_seq = 0;
  std::uint64_t tp_cancel_seq = 0;
  for (const Event& e : d.state().events) {
    if (e.type == EventType::Fill && e.order_id == stop.order_id) fill_seq = e.seq;
    if (e.type != EventType::OrderUpdate) continue;
    if (e.order_id == stop.order_id) CHECK(e.status != OrderStatus::Cancelled);
    if (e.order_id == tp.order_id && e.status == OrderStatus::Cancelled) tp_cancel_seq = e.seq;
  }
  REQUIRE(fill_seq != 0);
  CHECK(tp_cancel_seq > fill_seq);
}
