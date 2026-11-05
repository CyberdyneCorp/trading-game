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

TEST_CASE("Regression: closing a position larger than one bar's capacity keeps working until flat") {
  // Before: "close position" was IOC like any market order, so the rest of the position stayed
  // open after a capacity-limited bar with no order working on it.
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step();
  REQUIRE(d.state().position.qty == Q("10"));
  sc.costs.capacity_fraction = R("0.000004");  // 4 contracts per bar from now on
  const auto close = d.close();
  REQUIRE(close.accepted);
  d.step();
  CHECK(d.state().position.qty == Q("6"));
  REQUIRE(find_order(d.state(), close.order_id) != nullptr);
  CHECK(find_order(d.state(), close.order_id)->status == OrderStatus::PartiallyFilled);  // still working
  d.step(2);
  CHECK(d.state().position.flat());
  CHECK(find_order(d.state(), close.order_id)->status == OrderStatus::Filled);
}

TEST_CASE("A capacity-limited market entry is still IOC (only reduce-only market orders keep working)") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  sc.costs.capacity_fraction = R("0.000004");
  Driver d(sc);
  const auto entry = d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step();
  CHECK(d.state().position.qty == Q("4"));
  CHECK(find_order(d.state(), entry.order_id)->status == OrderStatus::Cancelled);
}

TEST_CASE("Regression: a bracket stop already breached by the entry's open fills at the open, not at its trigger") {
  // Before: the stop leg attached at the open was only checked intrabar and filled at its trigger
  // (97), a price the bar never traded after the gap down (high 92): a free 7 per contract.
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"90", "92", "85", "88"},
                               {"88", "88", "88", "88"}});
  Driver d(sc);
  d.step();
  const auto entry = d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "97", "110");
  REQUIRE(entry.accepted);
  d.step();
  CHECK(d.state().position.flat());
  std::vector<const Event*> fills;
  for (const Event& e : d.state().events) {
    if (e.type == EventType::Fill) fills.push_back(&e);
  }
  REQUIRE(fills.size() == 2);
  CHECK(fills[0]->price == P("90"));  // entry at the open
  CHECK(fills[1]->price == P("90"));  // stop at the open (gap), not at 97
  CHECK((fills[1]->flags & kFlagGapFill) != 0);
}

TEST_CASE("Regression: moving a bracket stop keeps it in its OCO group (the target is cancelled when it fills)") {
  // Before: the client replaced a dragged bracket STOP with a plain stop; the TP kept saying OCO but no
  // longer had a sibling, and stayed working after the stop closed the position.
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "96", "97"},
                               {"97", "97", "97", "97"}});
  Driver d(sc);
  const auto entry = d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step();
  auto kids = children_of(d.state(), entry.order_id);
  REQUIRE(kids.size() == 2);
  const Order* stop = kids[0]->type == OrderType::StopMarket ? kids[0] : kids[1];
  const std::uint32_t stop_id = stop->id;
  const std::size_t orders_before = d.state().orders.size();
  const auto moved = d.move(stop_id, "97");
  REQUIRE(moved.accepted);
  CHECK(moved.order_id == stop_id);
  CHECK(d.state().orders.size() == orders_before);  // moved in place, nothing new
  stop = children_of(d.state(), entry.order_id)[0]->type == OrderType::StopMarket ? children_of(d.state(), entry.order_id)[0]
                                                                                  : children_of(d.state(), entry.order_id)[1];
  CHECK(stop->price == P("97"));
  CHECK(stop->oco_group == entry.order_id);
  const Event& ev = d.state().events.back();
  CHECK(ev.type == EventType::OrderUpdate);
  CHECK(ev.reason == Reason::OrderMoved);
  CHECK(ev.requested == P("97"));
  d.step(2);  // bar 3 trades down to 96: the moved stop fills at 97, the TP is cancelled with it
  CHECK(d.state().position.flat());
  for (const Order* k : children_of(d.state(), entry.order_id)) CHECK_FALSE(k->active());
}

TEST_CASE("Regression: an exit larger than the position it closes is cancelled on the same bar (policy v14)") {
  // Before: a partial close left the bracket TP at the old size; when it closed the rest, its
  // remainder stayed working (PartiallyFilled, no position) and was only cancelled on the next bar.
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "111", "100", "110"},
                               {"110", "110", "110", "110"}});
  Driver d(sc);
  const auto entry = d.bracket(OrderType::Market, OrderSide::Buy, "10", nullptr, "95", "110");
  d.step();
  d.order(OrderType::Market, OrderSide::Sell, "1");
  d.step();  // 9 left; both exits still say 10
  REQUIRE(d.state().position.qty == Q("9"));
  const auto kids = children_of(d.state(), entry.order_id);
  REQUIRE(kids.size() == 2);
  const std::uint32_t tp_id = kids[0]->type == OrderType::TakeProfit ? kids[0]->id : kids[1]->id;
  d.step();  // bar 3 reaches the TP: it sells 9 and closes the position
  CHECK(d.state().position.flat());
  const Order* tp = find_order(d.state(), tp_id);
  REQUIRE(tp != nullptr);
  CHECK(tp->filled == Q("9"));
  CHECK(tp->status == OrderStatus::Cancelled);
  for (const Order* k : children_of(d.state(), entry.order_id)) CHECK_FALSE(k->active());
  // Its remainder is cancelled right after its fill, on the same bar, as "position closed".
  const Event& last = d.state().events.back();
  CHECK(last.type == EventType::OrderUpdate);
  CHECK(last.order_id == tp_id);
  CHECK(last.reason == Reason::PositionClosed);
  CHECK(last.step == d.state().step);
}

TEST_CASE("Regression: a stop cut short by capacity executes its remainder at the next opens (policy v15)") {
  // Before: the remainder rested at the trigger again, so the long stayed open while the next bars
  // opened above the stop, and it later filled at the trigger price instead of at an open.
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "100", "100", "100"},
                               {"100", "100", "94", "99"},
                               {"99", "100", "98", "99"},
                               {"99", "100", "98", "99"},
                               {"99", "99", "99", "99"}});
  Driver d(sc);
  d.order(OrderType::Market, OrderSide::Buy, "10");
  d.step();
  sc.costs.capacity_fraction = R("0.000004");  // 4 contracts per bar from now on
  const auto stop = d.order(OrderType::StopMarket, OrderSide::Sell, "10", "95", true);
  REQUIRE(stop.accepted);
  d.step();  // bar 2 trades through 95: 4 fill near the trigger
  REQUIRE(d.state().position.qty == Q("6"));
  CHECK(find_order(d.state(), stop.order_id)->status == OrderStatus::PartiallyFilled);
  d.step();  // bar 3 never reaches 95, yet the triggered stop is a market order: 4 fill at its open
  CHECK(d.state().position.qty == Q("2"));
  const Event* fill = last_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->step == d.state().step);
  CHECK(fill->order_id == stop.order_id);
  CHECK(fill->price == P("99"));  // the open (no costs here), not the trigger (95)
  CHECK(fill->requested == P("95"));  // the debrief still shows the trigger it came from
  d.step();
  CHECK(d.state().position.flat());
  CHECK(find_order(d.state(), stop.order_id)->status == OrderStatus::Filled);
}

TEST_CASE("A capacity-limited entry stop is IOC once triggered, like a market order") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"},
                               {"100", "106", "100", "105"},
                               {"105", "106", "104", "105"},
                               {"105", "105", "105", "105"}});
  sc.costs.capacity_fraction = R("0.000004");
  Driver d(sc);
  const auto entry = d.order(OrderType::StopMarket, OrderSide::Buy, "10", "102");
  REQUIRE(entry.accepted);
  d.step();
  CHECK(d.state().position.qty == Q("4"));
  CHECK(find_order(d.state(), entry.order_id)->status == OrderStatus::Cancelled);
  CHECK(last_of(d.state(), EventType::OrderUpdate)->reason == Reason::InsufficientCapacity);
  d.step();
  CHECK(d.state().position.qty == Q("4"));
}

TEST_CASE("MoveOrder accepts only working perpetual exits on the tick") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  Driver d(sc);
  const auto limit = d.order(OrderType::Limit, OrderSide::Buy, "1", "90");
  REQUIRE(limit.accepted);
  CHECK(d.move(limit.order_id, "91").reason == Reason::UnsupportedOrderType);  // an entry is not an exit
  CHECK(d.move(999, "91").reason == Reason::UnknownOrder);
  d.order(OrderType::Market, OrderSide::Buy, "1");
  d.step();
  const auto stop = d.order(OrderType::StopMarket, OrderSide::Sell, "1", "95", true);
  REQUIRE(stop.accepted);
  CHECK(d.move(stop.order_id, "95.005").reason == Reason::InvalidTick);
  REQUIRE(d.cancel(stop.order_id).accepted);
  CHECK(d.move(stop.order_id, "96").reason == Reason::UnknownOrder);  // no longer working
}
