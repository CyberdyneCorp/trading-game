// Execution policy v18 (design D6, D7, D8 of add-practice-and-advanced-strategies): time in force
// (gtc, bars:N, ioc and fok at the next open) and player-built OCO links.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/checkpoint.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

const Event* update_of(const RunState& st, std::uint32_t order_id, Reason reason) {
  for (const Event& e : st.events) {
    if (e.type == EventType::OrderUpdate && e.order_id == order_id && e.reason == reason) return &e;
  }
  return nullptr;
}

std::vector<const Event*> fills_of(const RunState& st, std::uint32_t order_id) {
  std::vector<const Event*> out;
  for (const Event& e : st.events) {
    if (e.type == EventType::Fill && e.order_id == order_id) out.push_back(&e);
  }
  return out;
}

// Flat bars at 100, then the bars a test needs.
Scenario market(std::initializer_list<BarSpec> tail) {
  std::vector<BarSpec> all = {{"100", "100", "100", "100"}};
  all.insert(all.end(), tail.begin(), tail.end());
  Scenario sc = make_scenario({{"100", "100", "100", "100"}});
  sc.bars.clear();
  for (const BarSpec& b : all) sc.bars.push_back({P(b.o), P(b.h), P(b.l), P(b.c), Q("1000000"), false});
  sc.funding_rate.assign(sc.bars.size(), Rate{});
  return sc;
}

void set_volume(Scenario& sc, std::size_t bar, const char* volume) { sc.bars[bar].volume = Q(volume); }

}  // namespace

// ---- D6 time in force --------------------------------------------------------------------

TEST_CASE("TIF: an order without a time in force is gtc and works as before") {
  Scenario sc = market({{"100", "101", "99", "100"}, {"100", "100", "94", "95"}});
  Driver d(sc);
  const CommandResult r = d.order(OrderType::Limit, OrderSide::Buy, "1", "95");
  REQUIRE(r.accepted);
  const Order* o = find_order(d.state(), r.order_id);
  CHECK(o->tif == TimeInForce::Gtc);
  CHECK(o->expire_step == 0);
  d.step(2);
  CHECK(d.state().position.qty == Q("1"));
}

TEST_CASE("TIF bars: eligible on the next N bars, the remainder expires at the close of the last one") {
  // Placed at step 0 with bars:2: eligible on bars 1 and 2; bar 3 would have filled it.
  Scenario sc = market({{"100", "101", "98", "100"}, {"100", "101", "97", "100"}, {"100", "100", "90", "92"}});
  Driver d(sc);
  const CommandResult r = d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "95", TimeInForce::Bars, 2);
  REQUIRE(r.accepted);
  CHECK(find_order(d.state(), r.order_id)->expire_step == 2);
  d.step(1);
  CHECK(find_order(d.state(), r.order_id)->active());
  d.step(1);
  const Order* o = find_order(d.state(), r.order_id);
  CHECK(o->status == OrderStatus::Expired);
  const Event* e = update_of(d.state(), r.order_id, Reason::TimeInForce);
  REQUIRE(e != nullptr);
  CHECK(e->step == 2);
  d.step(1);
  CHECK(d.state().position.flat());
}

TEST_CASE("TIF bars: a fill inside the window, and a bracket entry's children are gtc") {
  Scenario sc = market({{"100", "101", "94", "96"}, {"96", "97", "95", "96"}, {"96", "97", "95", "96"}, {"96", "97", "95", "96"}});
  Driver d(sc);
  Command c;
  c.type = CommandType::PlaceOrder;
  c.order_type = OrderType::Limit;
  c.side = OrderSide::Buy;
  c.qty = Q("1");
  c.price = P("95");
  c.attach_stop = P("90");
  c.attach_tp = P("110");
  c.tif = TimeInForce::Bars;
  c.tif_bars = 1;
  const CommandResult b = d.apply(c);
  REQUIRE(b.accepted);
  d.step(1);
  CHECK(fills_of(d.state(), b.order_id).size() == 1);
  const std::vector<const Order*> kids = children_of(d.state(), b.order_id);
  REQUIRE(kids.size() == 2);
  for (const Order* k : kids) {
    CHECK(k->tif == TimeInForce::Gtc);
    CHECK(k->expire_step == 0);
  }
  d.step(2);
  for (const Order* k : children_of(d.state(), b.order_id)) CHECK(k->active());  // protection never expires silently
}

TEST_CASE("TIF: refusals with invalid_time_in_force") {
  Scenario sc = market({{"100", "101", "99", "100"}, {"100", "101", "99", "100"}});  // last bar 2, step 0
  Driver d(sc);
  CHECK(d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "95", TimeInForce::Bars, 0).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "95", TimeInForce::Bars, 3).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "95", TimeInForce::Bars, 2).accepted);
  CHECK(d.order_tif(OrderType::Market, OrderSide::Buy, "1", nullptr, TimeInForce::Bars, 1).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::Market, OrderSide::Buy, "1", nullptr, TimeInForce::Ioc).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::StopMarket, OrderSide::Buy, "1", "105", TimeInForce::Ioc).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::StopMarket, OrderSide::Buy, "1", "105", TimeInForce::Fok).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "95", TimeInForce::Gtc, 2).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "95", TimeInForce::Ioc, 1).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::StopMarket, OrderSide::Buy, "1", "105", TimeInForce::Bars, 1).accepted);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "2").accepted);
  d.step(1);
  CHECK(d.order_tif(OrderType::TakeProfit, OrderSide::Sell, "1", "110", TimeInForce::Ioc, 0, true).reason == Reason::InvalidTimeInForce);
  CHECK(d.order_tif(OrderType::TakeProfit, OrderSide::Sell, "1", "110", TimeInForce::Bars, 1, true).accepted);
  Command t;
  t.type = CommandType::PlaceOrder;
  t.order_type = OrderType::TrailingStop;
  t.side = OrderSide::Sell;
  t.qty = Q("1");
  t.trail_distance = P("2");
  t.reduce_only = true;
  t.tif = TimeInForce::Fok;
  CHECK(d.apply(t).reason == Reason::InvalidTimeInForce);
  t.tif = TimeInForce::Bars;
  t.tif_bars = 1;
  CHECK(d.apply(t).accepted);
}

TEST_CASE("TIF ioc: fills at the open when the open reaches the limit") {
  Scenario sc = market({{"99", "101", "98", "100"}});
  Driver d(sc);
  const CommandResult r = d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "99.5", TimeInForce::Ioc);
  REQUIRE(r.accepted);
  CHECK(find_order(d.state(), r.order_id)->expire_step == 1);
  d.step(1);
  const std::vector<const Event*> f = fills_of(d.state(), r.order_id);
  REQUIRE(f.size() == 1);
  CHECK(f[0]->price == P("99"));
  CHECK(find_order(d.state(), r.order_id)->status == OrderStatus::Filled);
}

TEST_CASE("TIF ioc: a limit the open does not reach is not filled intrabar") {
  // The low touches 95 (a gtc limit at 96 would fill intrabar); the ioc one is cancelled after the open.
  Scenario sc = market({{"100", "101", "95", "100"}});
  Driver d(sc);
  const CommandResult r = d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "96", TimeInForce::Ioc);
  const CommandResult g = d.order(OrderType::Limit, OrderSide::Buy, "1", "96");
  REQUIRE(r.accepted);
  d.step(1);
  CHECK(fills_of(d.state(), r.order_id).empty());
  const Order* o = find_order(d.state(), r.order_id);
  CHECK(o->status == OrderStatus::Cancelled);
  const Event* e = update_of(d.state(), r.order_id, Reason::TimeInForce);
  REQUIRE(e != nullptr);
  CHECK(e->step == 1);
  CHECK(fills_of(d.state(), g.order_id).size() == 1);  // the gtc twin filled intrabar
  CHECK(d.state().position.qty == Q("1"));
}

TEST_CASE("TIF ioc: keeps what the open's capacity allowed, cancels the remainder") {
  Scenario sc = market({{"99", "101", "98", "100"}});
  set_volume(sc, 1, "3");  // capacity 3 at fraction 1
  Driver d(sc);
  const CommandResult r = d.order_tif(OrderType::Limit, OrderSide::Buy, "5", "99.5", TimeInForce::Ioc);
  d.step(1);
  const std::vector<const Event*> f = fills_of(d.state(), r.order_id);
  REQUIRE(f.size() == 1);
  CHECK(f[0]->qty == Q("3"));
  const Order* o = find_order(d.state(), r.order_id);
  CHECK(o->status == OrderStatus::Cancelled);
  CHECK(update_of(d.state(), r.order_id, Reason::TimeInForce) != nullptr);
}

TEST_CASE("TIF fok: too little capacity left after the orders served before it fills nothing") {
  // Gap down from 102 to 100: the limit at 101 is reached first and takes 6 of the 10 units.
  Scenario sc = market({{"102", "102", "102", "102"}, {"100", "101", "99", "100"}});
  set_volume(sc, 2, "10");
  Driver d(sc);
  d.step(1);
  const CommandResult a = d.order(OrderType::Limit, OrderSide::Buy, "6", "101");
  const CommandResult b = d.order_tif(OrderType::Limit, OrderSide::Buy, "5", "100.5", TimeInForce::Fok);
  REQUIRE(b.accepted);
  d.step(1);
  CHECK(fills_of(d.state(), a.order_id).size() == 1);
  CHECK(fills_of(d.state(), b.order_id).empty());
  CHECK(find_order(d.state(), b.order_id)->status == OrderStatus::Cancelled);
  CHECK(update_of(d.state(), b.order_id, Reason::TimeInForce) != nullptr);
  CHECK(d.state().position.qty == Q("6"));
}

TEST_CASE("TIF fok: the whole quantity at its turn") {
  Scenario sc = market({{"102", "102", "102", "102"}, {"100", "101", "99", "100"}});
  set_volume(sc, 2, "10");
  Driver d(sc);
  d.step(1);
  const CommandResult b = d.order_tif(OrderType::Limit, OrderSide::Buy, "4", "100.5", TimeInForce::Fok);
  d.step(1);
  const std::vector<const Event*> f = fills_of(d.state(), b.order_id);
  REQUIRE(f.size() == 1);
  CHECK(f[0]->qty == Q("4"));
  CHECK(find_order(d.state(), b.order_id)->status == OrderStatus::Filled);
}

TEST_CASE("TIF: an ioc order is cancelled on a bar without trading") {
  Scenario sc = market({{"99", "101", "98", "100"}, {"99", "101", "98", "100"}});
  sc.bars[1] = Bar{sc.bars[0].close, sc.bars[0].close, sc.bars[0].close, sc.bars[0].close, Qty{}, false, true};
  Driver d(sc);
  const CommandResult r = d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "105", TimeInForce::Ioc);
  d.step(2);
  CHECK(fills_of(d.state(), r.order_id).empty());
  const Event* e = update_of(d.state(), r.order_id, Reason::TimeInForce);
  REQUIRE(e != nullptr);
  CHECK(e->step == 1);
}

TEST_CASE("TIF options: an ioc limit fills at the open or is cancelled; bars expire") {
  Scenario sc = market({{"100", "101", "99", "100"}, {"100", "101", "99", "100"}, {"100", "101", "99", "100"},
                        {"100", "101", "99", "100"}, {"100", "101", "99", "100"}});
  add_options(sc, 5, {"100"});
  Driver d(sc);
  // A limit far above the ask is marketable at the open: filled at the ask.
  const CommandResult hi = d.option_tif(1, OrderSide::Buy, "1", "50", TimeInForce::Ioc);
  // A limit far below it is not: cancelled with time_in_force right after the option open phase.
  const CommandResult lo = d.option_tif(1, OrderSide::Buy, "1", "0.01", TimeInForce::Fok);
  const CommandResult bars = d.option_tif(2, OrderSide::Buy, "1", "0.01", TimeInForce::Bars, 2);
  REQUIRE(hi.accepted);
  REQUIRE(lo.accepted);
  REQUIRE(bars.accepted);
  CHECK(d.option_tif(1, OrderSide::Buy, "1", nullptr, TimeInForce::Ioc).reason == Reason::InvalidTimeInForce);
  CHECK(d.option_tif(1, OrderSide::Buy, "1", nullptr, TimeInForce::Bars, 2).reason == Reason::InvalidTimeInForce);
  d.step(1);
  CHECK(fills_of(d.state(), hi.order_id).size() == 1);
  CHECK(fills_of(d.state(), lo.order_id).empty());
  const Event* e = update_of(d.state(), lo.order_id, Reason::TimeInForce);
  REQUIRE(e != nullptr);
  CHECK(e->step == 1);
  CHECK(find_order(d.state(), bars.order_id)->active());
  d.step(1);
  CHECK(find_order(d.state(), bars.order_id)->status == OrderStatus::Expired);
  CHECK(update_of(d.state(), bars.order_id, Reason::TimeInForce)->step == 2);
}

// ---- D7 player-built OCO links -----------------------------------------------------------

TEST_CASE("OCO: a buy-stop and a sell-stop on one wide bar fill exactly one") {
  Scenario sc = market({{"100", "110", "90", "100"}});
  Driver d(sc);
  const CommandResult up = d.order(OrderType::StopMarket, OrderSide::Buy, "1", "105");
  const CommandResult dn = d.order(OrderType::StopMarket, OrderSide::Sell, "1", "95");
  const CommandResult l = d.link(up.order_id, dn.order_id);
  REQUIRE(l.accepted);
  CHECK(l.order_id == up.order_id);
  CHECK(find_order(d.state(), up.order_id)->link_group == up.order_id);
  CHECK(find_order(d.state(), dn.order_id)->link_group == up.order_id);
  d.step(1);
  const std::size_t filled = fills_of(d.state(), up.order_id).size() + fills_of(d.state(), dn.order_id).size();
  CHECK(filled == 1);
  // Same distance from the open: the earlier sequence executes first; the other is cancelled.
  CHECK(fills_of(d.state(), up.order_id).size() == 1);
  CHECK(find_order(d.state(), dn.order_id)->status == OrderStatus::Cancelled);
  CHECK(update_of(d.state(), dn.order_id, Reason::OcoSiblingFilled) != nullptr);
}

TEST_CASE("OCO: without the link both stops fill on that bar") {
  Scenario sc = market({{"100", "110", "90", "100"}});
  Driver d(sc);
  const CommandResult up = d.order(OrderType::StopMarket, OrderSide::Buy, "1", "105");
  const CommandResult dn = d.order(OrderType::StopMarket, OrderSide::Sell, "1", "95");
  d.step(1);
  CHECK(fills_of(d.state(), up.order_id).size() == 1);
  CHECK(fills_of(d.state(), dn.order_id).size() == 1);
}

TEST_CASE("OCO: a perpetual and an option order: the perpetual open phase runs first") {
  Scenario sc = market({{"99", "101", "98", "100"}, {"100", "101", "99", "100"}});
  add_options(sc, 2, {"100"});
  Driver d(sc);
  const CommandResult perp = d.order(OrderType::Limit, OrderSide::Buy, "1", "99.5");
  const CommandResult opt = d.option(1, OrderSide::Buy, "1", "50");
  REQUIRE(d.link(opt.order_id, perp.order_id).accepted);
  d.step(1);
  CHECK(fills_of(d.state(), perp.order_id).size() == 1);
  CHECK(fills_of(d.state(), opt.order_id).empty());
  CHECK(update_of(d.state(), opt.order_id, Reason::OcoSiblingFilled) != nullptr);
  CHECK(d.state().option_positions.empty());
}

TEST_CASE("OCO: two reduce-only exits of the position reduce each other on a partial exit") {
  Scenario sc = market({{"100", "100", "100", "100"}, {"100", "111", "99", "110"}, {"110", "111", "109", "110"}});
  Driver d(sc);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step(1);
  const CommandResult tp = d.order(OrderType::TakeProfit, OrderSide::Sell, "4", "110", true);
  const CommandResult stop = d.order(OrderType::StopMarket, OrderSide::Sell, "10", "90", true);
  REQUIRE(d.link(tp.order_id, stop.order_id).accepted);
  d.step(1);
  CHECK(fills_of(d.state(), tp.order_id).size() == 1);
  CHECK(d.state().position.qty == Q("6"));
  const Order* s = find_order(d.state(), stop.order_id);
  CHECK(s->active());
  CHECK(s->remaining() == Q("6"));
  CHECK(s->link_group == tp.order_id);
}

TEST_CASE("OCO: a linked bracket parent keeps its children's group") {
  Scenario sc = market({{"100", "101", "94", "96"}, {"96", "97", "95", "96"}});
  Driver d(sc);
  const CommandResult entry = d.bracket(OrderType::Limit, OrderSide::Buy, "1", "95", "90", "110");
  const CommandResult other = d.order(OrderType::Limit, OrderSide::Sell, "1", "130");
  REQUIRE(d.link(entry.order_id, other.order_id).accepted);
  d.step(1);
  CHECK(fills_of(d.state(), entry.order_id).size() == 1);
  CHECK(find_order(d.state(), other.order_id)->status == OrderStatus::Cancelled);
  const std::vector<const Order*> kids = children_of(d.state(), entry.order_id);
  REQUIRE(kids.size() == 2);
  for (const Order* k : kids) {
    CHECK(k->oco_group == entry.order_id);
    CHECK(k->link_group == 0);
    CHECK(k->active());
  }
}

TEST_CASE("OCO: refusals with invalid_oco change nothing") {
  Scenario sc = market({{"100", "101", "94", "96"}, {"96", "97", "95", "96"}, {"96", "97", "95", "96"}});
  Driver d(sc);
  const CommandResult entry = d.bracket(OrderType::Limit, OrderSide::Buy, "1", "95", "90", "110");
  const CommandResult a = d.order(OrderType::Limit, OrderSide::Buy, "1", "80");
  const CommandResult b = d.order(OrderType::Limit, OrderSide::Sell, "1", "130");
  const CommandResult c = d.order(OrderType::Limit, OrderSide::Sell, "1", "140");
  d.step(1);  // the entry fills and attaches its children
  const std::uint32_t child = children_of(d.state(), entry.order_id).front()->id;
  REQUIRE(d.link(a.order_id, b.order_id).accepted);
  const auto links = [&d] {
    std::vector<std::uint32_t> out;
    for (const Order& o : d.state().orders) out.push_back(o.link_group);
    return out;
  };
  const std::vector<std::uint32_t> before = links();
  const std::size_t events = d.state().events.size();
  CHECK(d.link(c.order_id, c.order_id).reason == Reason::InvalidOco);       // the same order twice
  CHECK(d.link(c.order_id, 999).reason == Reason::InvalidOco);              // unknown
  CHECK(d.link(c.order_id, entry.order_id).reason == Reason::InvalidOco);   // inactive (filled)
  CHECK(d.link(c.order_id, child).reason == Reason::InvalidOco);            // a bracket child
  CHECK(d.link(c.order_id, b.order_id).reason == Reason::InvalidOco);       // already linked
  CHECK(links() == before);
  CHECK(d.state().events.size() == events);
}

TEST_CASE("OCO: a cancel or an expiry dissolves the link; a move keeps it") {
  Scenario sc = market({{"100", "101", "99", "100"}, {"100", "101", "99", "100"}, {"100", "101", "99", "100"},
                        {"100", "101", "99", "100"}});
  Driver d(sc);
  const CommandResult a = d.order(OrderType::Limit, OrderSide::Buy, "1", "80");
  const CommandResult b = d.order(OrderType::Limit, OrderSide::Sell, "1", "130");
  REQUIRE(d.link(a.order_id, b.order_id).accepted);
  REQUIRE(d.cancel(a.order_id).accepted);
  CHECK(find_order(d.state(), b.order_id)->link_group == 0);
  CHECK(find_order(d.state(), b.order_id)->active());

  const CommandResult x = d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "80", TimeInForce::Bars, 1);
  const CommandResult y = d.order(OrderType::Limit, OrderSide::Sell, "1", "140");
  REQUIRE(d.link(x.order_id, y.order_id).accepted);
  d.step(1);
  CHECK(find_order(d.state(), x.order_id)->status == OrderStatus::Expired);
  CHECK(find_order(d.state(), y.order_id)->link_group == 0);
  CHECK(find_order(d.state(), y.order_id)->active());

  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "2").accepted);
  d.step(1);
  const CommandResult tp = d.order(OrderType::TakeProfit, OrderSide::Sell, "2", "120", true);
  const CommandResult st = d.order(OrderType::StopMarket, OrderSide::Sell, "2", "90", true);
  REQUIRE(d.link(tp.order_id, st.order_id).accepted);
  REQUIRE(d.move(st.order_id, "92").accepted);
  CHECK(find_order(d.state(), st.order_id)->link_group == tp.order_id);
}

TEST_CASE("OCO: a step with only a link is an act; a link emits no event") {
  Scenario sc = market({{"100", "101", "99", "100"}, {"100", "101", "99", "100"}, {"100", "101", "99", "100"}});
  Driver d(sc);
  const CommandResult a = d.order(OrderType::Limit, OrderSide::Buy, "1", "80");
  const CommandResult b = d.order(OrderType::Limit, OrderSide::Sell, "1", "130");
  d.step(1);
  const std::size_t events = d.state().events.size();
  CHECK(decision_at(d.state()) == DecisionKind::Wait);
  REQUIRE(d.link(a.order_id, b.order_id).accepted);
  CHECK(d.state().events.size() == events);
  CHECK(decision_at(d.state()) == DecisionKind::Act);
  d.step(1);
  CHECK(d.state().decisions.back().kind == DecisionKind::Act);
}

// ---- D8 checkpoint format 6 --------------------------------------------------------------

TEST_CASE("Checkpoint 6: time in force and links round trip; format 5 is refused") {
  Scenario sc = market({{"100", "101", "99", "100"}, {"100", "101", "99", "100"}, {"100", "101", "99", "100"}});
  Driver d(sc);
  const CommandResult a = d.order_tif(OrderType::Limit, OrderSide::Buy, "1", "80", TimeInForce::Bars, 2);
  const CommandResult b = d.order(OrderType::Limit, OrderSide::Sell, "1", "130");
  REQUIRE(d.link(a.order_id, b.order_id).accepted);
  const std::vector<std::uint8_t> bytes = save_checkpoint(d.state(), sc.id);
  const Result<RunState> back = load_checkpoint(bytes, sc.id);
  REQUIRE(back.ok());
  CHECK(state_digest(back.value()) == state_digest(d.state()));
  const Order* o = find_order(back.value(), a.order_id);
  CHECK(o->tif == TimeInForce::Bars);
  CHECK(o->tif_bars == 2);
  CHECK(o->expire_step == 2);
  CHECK(o->link_group == a.order_id);
  CHECK(back.value().last_link_step == 0);
  CHECK(decision_at(back.value()) == DecisionKind::Act);
  auto format5 = bytes;
  format5[4] = 5;
  CHECK(load_checkpoint(format5, sc.id).error() == ErrorCode::NonCanonicalEncoding);
}

// ---- C ABI ---------------------------------------------------------------------------------

TEST_CASE("C ABI: tl_place_order_tif, tl_place_option_order_tif and tl_link_oco") {
  tl_run* run = tl_run_create("m01-false-breakout", 3);
  REQUIRE(run != nullptr);
  const std::string state = tl_state(run);
  const std::string bad = tl_place_order_tif(run, 1, 1, 0, "0.1", "1", 0, nullptr, nullptr, nullptr, nullptr, 9, 0);
  CHECK(bad.find("\"reason\":\"invalid_time_in_force\"") != std::string::npos);
  const std::string ioc_market = tl_place_order_tif(run, 2, 0, 0, "0.1", nullptr, 0, nullptr, nullptr, nullptr, nullptr, 2, 0);
  CHECK(ioc_market.find("\"reason\":\"invalid_time_in_force\"") != std::string::npos);
  const std::string a = tl_place_order_tif(run, 3, 1, 0, "0.1", "1", 0, nullptr, nullptr, nullptr, nullptr, 1, 3);
  CHECK(a.find("\"accepted\":true") != std::string::npos);
  const std::string b = tl_place_order_ex(run, 4, 1, 0, "0.1", "2", 0, nullptr, nullptr, nullptr);
  CHECK(b.find("\"accepted\":true") != std::string::npos);
  const std::string link = tl_link_oco(run, 5, 1, 2);
  CHECK(link.find("\"accepted\":true") != std::string::npos);
  CHECK(std::string(tl_link_oco(run, 6, 1, 2)).find("invalid_oco") != std::string::npos);
  const std::string after = tl_state(run);
  CHECK(after.find("\"tif\":\"bars\",\"tifBars\":3,\"expireStep\":") != std::string::npos);
  CHECK(after.find("\"linkGroup\":1") != std::string::npos);
  tl_run_destroy(run);
}
