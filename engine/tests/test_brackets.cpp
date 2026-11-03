#include <doctest/doctest.h>

#include "scenario_builder.hpp"

using namespace tradelab;
using namespace tradelab::test;

TEST_CASE("Bracket on a market entry creates reduce-only OCO children for the filled quantity") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "101", "99.5", "100"},
                               {"100", "100.5", "99.5", "100"},
                               {"100", "100", "100", "100"}});
  Driver d(sc);
  const auto entry = d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  REQUIRE(entry.accepted);
  CHECK(children_of(d.state(), entry.order_id).empty());  // nothing before the fill
  d.step();
  const auto kids = children_of(d.state(), entry.order_id);
  REQUIRE(kids.size() == 2);
  for (const Order* k : kids) {
    CHECK(k->reduce_only);
    CHECK(k->side == OrderSide::Sell);
    CHECK(k->qty == Q("10"));
    CHECK(k->oco_group == entry.order_id);
  }
}

TEST_CASE("SIM06: partial entry fills protect only the filled quantity; exits reduce siblings") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "94", "96"},
                               {"96", "96", "96", "96"}});
  sc.costs.capacity_fraction = R("0.000006");  // 1,000,000 volume -> 6 contracts per bar
  Driver d(sc);
  const auto entry = d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step();  // only 6 of 10 fill (market IOC: rest cancelled)
  CHECK(d.state().position.qty == Q("6"));
  for (const Order* k : children_of(d.state(), entry.order_id)) CHECK(k->qty == Q("6"));
  d.step(2);  // bar 3 hits the stop (low 94): the stop fills 6 and its TP sibling is reduced to 0
  CHECK(d.state().position.flat());
  for (const Order* k : children_of(d.state(), entry.order_id)) CHECK_FALSE(k->active());
  // Nothing ever sold more than the position (no accidental short).
  CHECK(d.state().position.qty.raw >= 0);
}

TEST_CASE("OCO: a partial exit reduces the sibling by the same quantity") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "112", "100", "111"},
                               {"111", "111", "111", "111"}});
  Driver d(sc);
  const auto entry = d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step();
  sc.costs.capacity_fraction = R("0.000004");  // the driver reads `sc` by reference: 4 contracts/bar from now
  d.step();  // TP fills 4 of 10 intrabar
  const auto kids = children_of(d.state(), entry.order_id);
  REQUIRE(kids.size() == 2);
  const Order* stop = kids[0]->type == OrderType::StopMarket ? kids[0] : kids[1];
  const Order* tp = kids[0]->type == OrderType::TakeProfit ? kids[0] : kids[1];
  CHECK(tp->filled == Q("4"));
  CHECK(stop->remaining() == Q("6"));
  CHECK(d.state().position.qty == Q("6"));
}

TEST_CASE("Bracket after an intrabar entry: adverse stop allowed, same-bar take-profit not credited") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "111", "97", "100"},  // limit 98 fills intrabar; high 111 would reach TP 110
                               {"100", "100", "100", "100"}});
  Driver d(sc);
  d.step();
  const auto entry = d.bracket(OrderType::Limit, OrderSide::Buy, "5", "98", "96", "110");
  REQUIRE(entry.accepted);
  d.step();
  CHECK(d.state().position.qty == Q("5"));  // entered intrabar
  for (const Order* k : children_of(d.state(), entry.order_id)) {
    if (k->type == OrderType::TakeProfit) CHECK(k->active());  // high 111 >= 110 but not credited
  }
}

TEST_CASE("Invalid brackets are rejected") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  Driver d(sc);
  CHECK(d.bracket(OrderType::Market, OrderSide::Buy, "1", nullptr, "105", nullptr).reason == Reason::InvalidBracket);
  CHECK(d.bracket(OrderType::Market, OrderSide::Sell, "1", nullptr, nullptr, "105").reason == Reason::InvalidBracket);
  CHECK(d.bracket(OrderType::Market, OrderSide::Buy, "1", nullptr, "95.001", nullptr).reason == Reason::InvalidTick);
  CHECK(d.bracket(OrderType::Limit, OrderSide::Buy, "1", "90", "89", "95").accepted);
}

TEST_CASE("Trailing stop ratchets with closes, takes effect next bar, never loosens") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "110", "100", "105"},   // high 110 must NOT move the trail; close 105 does
                               {"105", "106", "102", "103"},   // trail 102 set at close of bar 2 -> low 102 triggers
                               {"103", "103", "103", "103"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();
  const auto tr = d.trailing(OrderSide::Sell, "1", "3");
  REQUIRE(tr.accepted);
  CHECK(find_order(d.state(), tr.order_id)->price == P("97"));  // 100 - 3
  d.step();
  CHECK(find_order(d.state(), tr.order_id)->price == P("102"));  // from close 105, not high 110
  d.step();
  const Event* fill = last_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->order_id == tr.order_id);
  CHECK(fill->price == P("102"));
  CHECK(d.state().position.flat());
}

TEST_CASE("Trailing stop for a short follows lower closes") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "90", "92"},
                               {"92", "93", "91", "92.5"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Sell, "1");
  d.step();
  const auto tr = d.trailing(OrderSide::Buy, "1", "2");
  CHECK(find_order(d.state(), tr.order_id)->price == P("102"));
  d.step();
  CHECK(find_order(d.state(), tr.order_id)->price == P("94"));
  d.step();
  CHECK(find_order(d.state(), tr.order_id)->price == P("94"));  // close 92.5 + 2 = 94.5 would loosen
}
