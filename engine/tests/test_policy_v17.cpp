// Execution policy v17: mark-price bars (perpetual-futures "Mark price", "Liquidation", "Funding"),
// the option settlement window in market time (options "Expiry and settlement") and continuity bars
// with the scenario gap policy (indicators "Gaps in data").

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "pack_fixtures.hpp"
#include "scenario_builder.hpp"
#include "tradelab/c_api.h"
#include "tradelab/candle_patterns.hpp"
#include "tradelab/indicators.hpp"
#include "tradelab/options.hpp"

using namespace tradelab;
using namespace tradelab::test;

namespace {

const Event* first_of(const RunState& st, EventType type) {
  for (const Event& e : st.events) {
    if (e.type == type) return &e;
  }
  return nullptr;
}

const Event* update_of(const RunState& st, std::uint32_t order_id, Reason reason) {
  for (const Event& e : st.events) {
    if (e.type == EventType::OrderUpdate && e.order_id == order_id && e.reason == reason) return &e;
  }
  return nullptr;
}

Bar flat(const char* px) { return Bar{P(px), P(px), P(px), P(px), Q("1000000"), false}; }

Bar bar_of(const char* o, const char* h, const char* l, const char* c) { return Bar{P(o), P(h), P(l), P(c), Q("1000000"), false}; }

// A continuity bar at index i: flat at the previous close, no volume.
void make_continuity(Scenario& sc, std::size_t i) {
  const Price c = sc.bars[i - 1].close;
  sc.bars[i] = Bar{c, c, c, c, Qty{}, false, true};
}

}  // namespace

// ---- 5.7 mark price ------------------------------------------------------------------------

TEST_CASE("Mark price: a wick on the last price only does not liquidate") {
  // 10x long at 100: liquidation near 90.9. The traded low reaches 90, the mark low stays at 95.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "101", "90", "99"},
                               {"99", "99", "99", "99"}});
  sc.mark = sc.bars;
  sc.mark[2] = bar_of("100", "101", "95", "99");
  REQUIRE(validate(sc) == ScenarioError::Ok);
  CHECK_FALSE(mark_from_last_price(sc));
  Driver d(sc);
  REQUIRE(d.leverage(10).accepted);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step(2);
  CHECK(first_of(d.state(), EventType::Liquidation) == nullptr);
  CHECK(d.state().position.qty == Q("10"));

  // Without the mark series the same bars liquidate (markFromLastPrice).
  Scenario last = sc;
  last.mark.clear();
  CHECK(mark_from_last_price(last));
  Driver e(last);
  REQUIRE(e.leverage(10).accepted);
  REQUIRE(e.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  e.step(2);
  CHECK(first_of(e.state(), EventType::Liquidation) != nullptr);
}

TEST_CASE("Mark price: liquidation on the mark low, fills on the traded price, equity at the mark close") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "101", "98", "99"},
                               {"99", "99", "99", "99"}});
  sc.mark = sc.bars;
  sc.mark[1] = bar_of("100", "102", "100", "102");
  sc.mark[2] = bar_of("100", "101", "85", "99");
  REQUIRE(validate(sc) == ScenarioError::Ok);
  Driver d(sc);
  REQUIRE(d.leverage(10).accepted);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step();
  const Event* fill = first_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->price == P("100"));            // traded open, no costs in the test scenario
  CHECK(d.view().mark == P("102"));          // marked at the mark close
  CHECK(d.view().unrealized == M("20"));
  d.step();
  const Event* liq = first_of(d.state(), EventType::Liquidation);
  REQUIRE(liq != nullptr);  // the traded low (98) never reached ~90.9; the mark low (85) did
  CHECK(d.state().position.flat());
}

TEST_CASE("Mark price: funding is charged on the mark close") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  sc.contract.funding_interval = 1;
  sc.funding_rate.assign(sc.bars.size(), R("0.001"));
  sc.mark = sc.bars;
  sc.mark[1] = bar_of("100", "120", "100", "120");
  Driver d(sc);
  REQUIRE(d.order(OrderType::Market, OrderSide::Buy, "10").accepted);
  d.step();
  const Event* f = first_of(d.state(), EventType::Funding);
  REQUIRE(f != nullptr);
  CHECK(f->price == P("120"));
  CHECK(f->amount == M("-1.2"));  // 10 x 120 x 0.1%
}

TEST_CASE("Mark price: a mark series must match the bars") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  sc.mark = {flat("100"), flat("100")};
  CHECK(validate(sc) == ScenarioError::InvalidMark);
  sc.mark = {flat("100"), flat("100"), bar_of("100", "99", "100", "100")};
  CHECK(validate(sc) == ScenarioError::InvalidMark);
}

// ---- 6.6 settlement window ------------------------------------------------------------------

TEST_CASE("Settlement: only the sub-bars inside the window count") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"90", "103", "90", "103"}});
  add_options(sc, 2, {"100"});
  SubBars sub{2, 1, {}};
  for (int i = 0; i < 30; ++i) sub.closes.push_back(P("90"));  // before the window
  for (int i = 0; i < 15; ++i) sub.closes.push_back(P("100"));
  for (int i = 0; i < 15; ++i) sub.closes.push_back(P("103"));
  sc.sub_bars = {sub};
  REQUIRE(validate(sc) == ScenarioError::Ok);
  bool approx = true;
  CHECK(settlement_price(sc, 2, &approx) == P("101.5"));
  CHECK_FALSE(approx);
  // 5-minute sub-bars that do not tile a 32-minute window fall back to the bars, flagged approximate.
  sc.options.settlement_minutes = 32;
  sc.sub_bars = {SubBars{2, 5, {P("100"), P("101"), P("102"), P("103"), P("103"), P("103"), P("103")}}};
  REQUIRE(validate(sc) == ScenarioError::Ok);
  CHECK(settlement_price(sc, 2, &approx) == P("103"));
  CHECK(approx);
}

TEST_CASE("Settlement: spec example, 30 one-minute closes averaging 101.5") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "103", "100", "103"}});
  add_options(sc, 2, {"100"});
  SubBars sub{2, 1, {}};
  for (int i = 0; i < 15; ++i) sub.closes.push_back(P("100"));
  for (int i = 0; i < 15; ++i) sub.closes.push_back(P("103"));  // mean 101.5, last = the bar close
  sc.sub_bars = {sub};
  REQUIRE(validate(sc) == ScenarioError::Ok);
  bool approx = true;
  CHECK(settlement_price(sc, 2, &approx) == P("101.5"));
  CHECK_FALSE(approx);
  // Coarse bars only: the expiry close, flagged approximate.
  sc.sub_bars.clear();
  CHECK(settlement_price(sc, 2, &approx) == P("103"));
  CHECK(approx);
}

TEST_CASE("Settlement: a window of two hourly bars averages their closes; sub-bars must end at the bar close") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "104", "100", "104"}, {"104", "106", "104", "106"}});
  add_options(sc, 2, {"100"});
  sc.options.settlement_minutes = 120;
  bool approx = true;
  CHECK(settlement_price(sc, 2, &approx) == P("105"));
  CHECK_FALSE(approx);  // the bars tile the window exactly
  sc.options.settlement_minutes = 90;
  CHECK(settlement_price(sc, 2, &approx) == P("105"));
  CHECK(approx);
  sc.sub_bars = {SubBars{2, 1, {P("100"), P("105")}}};
  CHECK(validate(sc) == ScenarioError::InvalidSubBars);
}

TEST_CASE("Settlement window: orders are refused with series_settling from the window start") {
  // Hourly bars, expiry at bar 6, a 150-minute window: it starts at minute 270 (half-way through bar 4),
  // so an order placed at step 3 (executes at the open of bar 4, minute 240) is still accepted and one
  // placed at step 4 (bar 5 opens at minute 300) is refused.
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}, {"100", "100", "100", "100"}});
  add_options(sc, 6, {"100"});
  sc.options.settlement_minutes = 150;
  REQUIRE(validate(sc) == ScenarioError::Ok);
  CHECK_FALSE(in_settlement_window(sc, sc.options.series[0], 4));
  CHECK(in_settlement_window(sc, sc.options.series[0], 5));
  Driver d(sc);
  d.step(3);
  const auto resting = d.option(1, OrderSide::Buy, "1", "0.01");  // a limit far below the ask: rests
  REQUIRE(resting.accepted);
  d.step();
  CHECK(find_order(d.state(), resting.order_id)->active());
  const auto late = d.option(1, OrderSide::Buy, "1");
  CHECK_FALSE(late.accepted);
  CHECK(late.reason == Reason::SeriesSettling);
  d.step();  // bar 5 opens inside the window: the resting limit is cancelled, not executed
  CHECK_FALSE(find_order(d.state(), resting.order_id)->active());
  CHECK(update_of(d.state(), resting.order_id, Reason::SeriesSettling) != nullptr);
}

TEST_CASE("Settlement window: the default 30 minutes on hourly bars refuses nothing before expiry") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"100", "100", "100", "100"}});
  add_options(sc, 3, {"100"});
  Driver d(sc);
  d.step(2);  // the last decision step before expiry
  CHECK(d.option(1, OrderSide::Buy, "1").accepted);
}

// ---- 7.6 continuity bars ---------------------------------------------------------------------

TEST_CASE("Continuity bar: no fill in that bar; the market order executes at the next trading open") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "100", "100", "100"}, {"100", "100", "100", "100"},
                               {"102", "103", "101", "102"}, {"102", "102", "102", "102"}});
  make_continuity(sc, 2);
  REQUIRE(validate(sc) == ScenarioError::Ok);
  Driver d(sc);
  d.step();  // decision at bar 1; bar 2 is a continuity bar
  const auto mkt = d.order(OrderType::Market, OrderSide::Buy, "1");
  const auto stop = d.order(OrderType::StopMarket, OrderSide::Buy, "1", "100");  // reachable at the flat price
  REQUIRE(mkt.accepted);
  REQUIRE(stop.accepted);
  d.step();
  CHECK(first_of(d.state(), EventType::Fill) == nullptr);
  CHECK(find_order(d.state(), mkt.order_id)->active());
  CHECK(find_order(d.state(), stop.order_id)->active());
  d.step();
  const Event* fill = first_of(d.state(), EventType::Fill);
  REQUIRE(fill != nullptr);
  CHECK(fill->step == 3);
  CHECK(fill->price == P("102"));
}

TEST_CASE("Continuity bar: validation requires a flat bar at the previous close with no volume") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"100", "101", "100", "101"}, {"101", "101", "101", "101"}});
  make_continuity(sc, 2);
  CHECK(validate(sc) == ScenarioError::Ok);
  sc.bars[2].volume = Q("1");
  CHECK(validate(sc) == ScenarioError::InvalidContinuity);
  sc.bars[2] = Bar{P("100"), P("100"), P("100"), P("100"), Qty{}, false, true};  // not the previous close
  CHECK(validate(sc) == ScenarioError::InvalidContinuity);
  sc.bars[0].continuity = true;  // bar 0 has no previous close
  CHECK(validate(sc) == ScenarioError::InvalidContinuity);
}

TEST_CASE("Gap policy: skip leaves continuity bars out of indicators, carry forward feeds them flat") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}, {"102", "102", "102", "102"}, {"104", "104", "104", "104"},
                               {"104", "104", "104", "104"}, {"110", "110", "110", "110"}});
  make_continuity(sc, 3);
  REQUIRE(validate(sc) == ScenarioError::Ok);

  sc.mission.gap_policy = GapPolicy::Skip;
  const Series skip = sma(sc, 4, 3);
  CHECK_FALSE(skip[3].has_value());  // no value at the continuity bar
  CHECK(skip[2] == P("102"));        // (100 + 102 + 104) / 3
  CHECK(skip[4] == P("105.33333333"));  // (102 + 104 + 110) / 3, the continuity bar left out
  CHECK_FALSE(vwap(sc, 4, 24)[3].has_value());
  CHECK_FALSE(rsi(sc, 4, 2)[3].has_value());

  sc.mission.gap_policy = GapPolicy::CarryForward;
  const Series carry = sma(sc, 4, 3);
  CHECK(carry[3] == P("103.33333333"));  // (102 + 104 + 104) / 3
  CHECK(carry[4] == P("106"));           // (104 + 104 + 110) / 3
  CHECK(vwap(sc, 4, 24)[3].has_value());

  // Without continuity bars the policy changes nothing.
  Scenario plain = sc;
  plain.bars[3].continuity = false;
  plain.bars[3].volume = Q("1000000");
  plain.mission.gap_policy = GapPolicy::Skip;
  CHECK(sma(plain, 4, 3) == carry);
}

TEST_CASE("Gap policy: skip is closed-bar only and patterns never complete at a continuity bar") {
  Scenario sc = make_scenario({{"100", "100", "100", "100"}});
  for (int i = 1; i < 40; ++i) sc.bars.push_back(bar_of("100", "101", "99", i % 2 == 0 ? "100.5" : "99.5"));
  sc.bars.push_back(sc.bars.back());
  sc.funding_rate.assign(sc.bars.size(), Rate{});
  for (std::size_t i = 1; i < sc.bars.size(); ++i) sc.bars[i].open = sc.bars[i - 1].close;
  for (Bar& b : sc.bars) {
    b.high = std::max({b.high, b.open, b.close});
    b.low = std::min({b.low, b.open, b.close});
  }
  make_continuity(sc, 30);
  REQUIRE(validate(sc) == ScenarioError::Ok);
  for (const GapPolicy policy : {GapPolicy::Skip, GapPolicy::CarryForward}) {
    sc.mission.gap_policy = policy;
    for (const PatternHit& h : detect_patterns(sc, 40)) CHECK(h.step != 30);
    // Changing a later bar changes nothing at step 31.
    Scenario future = sc;
    future.bars[35].close = P("120");
    future.bars[35].high = P("120");
    CHECK(sma(future, 31, 5)[31] == sma(sc, 31, 5)[31]);
    CHECK(macd(future, 31, 3, 6, 2).histogram[31] == macd(sc, 31, 3, 6, 2).histogram[31]);
  }
}

TEST_CASE("CSV import: gaps filled with continuity bars round-trip through the pack") {
  const std::string first = make_csv(100);
  std::string second = make_csv(100, 1'700'000'000 + 102 * 3600);  // two missing hours
  second = second.substr(second.find('\n') + 1);
  const std::string csv = first + second;
  const SecretSeed device = seed_of(7);
  const PublicKey pub = ed25519_keypair(device).public_key;
  const std::vector<TrustedKey> keys = {{key_id("device", pub), pub}};

  const PackBuild joined = build_csv_pack(csv, hourly_meta(), {}, device);
  REQUIRE_MESSAGE(joined.ok(), joined.reason);
  const PackOpen j = open_pack(joined.bytes, keys, PackIntent::Install);
  REQUIRE(j.ok());
  CHECK(j.scenario.bars.size() == 200);
  CHECK(j.info.gap_policy.empty());

  ImportRules rules;
  rules.fill_gaps = GapPolicy::CarryForward;
  const PackBuild filled = build_csv_pack(csv, hourly_meta(), rules, device);
  REQUIRE_MESSAGE(filled.ok(), filled.reason);
  const PackOpen f = open_pack(filled.bytes, keys, PackIntent::Install);
  REQUIRE_MESSAGE(f.ok(), (f.reason + " " + f.detail));
  REQUIRE(f.scenario.bars.size() == 202);
  CHECK(f.info.gap_policy == "carryForward");
  CHECK(f.scenario.mission.gap_policy == GapPolicy::CarryForward);
  CHECK(f.scenario.bars[100].continuity);
  CHECK(f.scenario.bars[101].continuity);
  CHECK_FALSE(f.scenario.bars[102].continuity);
  CHECK(f.scenario.bars[100].close == f.scenario.bars[99].close);
  CHECK(f.scenario.bars[100].volume == Qty{});

  rules.fill_gaps = GapPolicy::Skip;
  const PackOpen s = open_pack(build_csv_pack(csv, hourly_meta(), rules, device).bytes, keys, PackIntent::Install);
  REQUIRE(s.ok());
  CHECK(s.info.gap_policy == "skip");
  CHECK(s.scenario.mission.gap_policy == GapPolicy::Skip);

  // A file without gaps builds exactly as before, whatever the rule.
  CHECK(build_csv_pack(first, hourly_meta(), rules, device).bytes == build_csv_pack(first, hourly_meta(), {}, device).bytes);
}

TEST_CASE("C ABI: built-in missions mark from the last price; bars carry the continuity flag; quotes say settling") {
  tl_run* run = tl_run_create("m04-options-buyer", 1);
  REQUIRE(run != nullptr);
  const std::string s = tl_state(run);
  CHECK(s.find("\"markFromLastPrice\":true") != std::string::npos);
  CHECK(s.find("\"gapPolicy\":\"skip\"") != std::string::npos);
  CHECK(s.find("\"settlementMinutes\":30") != std::string::npos);
  CHECK(s.find("\"settling\":false") != std::string::npos);
  CHECK(s.find("\"settling\":true") == std::string::npos);
  CHECK(s.find(",true,false]") != std::string::npos);  // [o, h, l, c, v, synthetic, continuity]
  tl_run_destroy(run);
}

// Regression: the debrief reveal said a filled gap "was kept as a jump"; it now carries the gap policy.
TEST_CASE("C ABI: a pack debrief reveal says whether its gaps were filled with continuity bars") {
  const std::string seed_hex(64, 'b');
  const std::string key = tl_device_key(seed_hex.c_str());
  const std::string pub = key.substr(key.find("publicKey\":\"") + 12, 64);
  const std::string id = key.substr(key.find("\"id\":\"") + 6, 23);
  const std::string keys = "[{\"id\":\"" + id + "\",\"key\":\"" + pub + "\"}]";
  const std::string second = make_csv(100, 1'700'000'000 + 102 * 3600);  // two missing hours
  const std::string csv = make_csv(100) + second.substr(second.find('\n') + 1);
  const std::string meta =
      R"({"asset":"BTC","pair":"BTC/USDT","venue":"test","volumeUnit":"base","intervalSeconds":3600,"timestampUnit":"s","leverageCap":3)";
  const auto reveal_of = [&](const std::string& fill) {
    const std::string built = tl_pack_build_csv(csv.c_str(), (meta + fill + "}").c_str(), seed_hex.c_str());
    REQUIRE_MESSAGE(built.find("\"ok\":true") != std::string::npos, built);
    const std::vector<std::uint8_t> pack(tl_blob_data(), tl_blob_data() + tl_blob_size());
    tl_run* run = tl_run_create_pack(pack.data(), static_cast<std::uint32_t>(pack.size()), keys.c_str(), 5);
    REQUIRE(run != nullptr);
    tl_advance(run, 1000);
    const std::string d = tl_debrief(run);
    tl_run_destroy(run);
    return d.substr(d.find("\"reveal\""));
  };
  CHECK(reveal_of(R"(,"gapFill":"skip")").find(R"("gaps":1,"gapPolicy":"skip")") != std::string::npos);
  CHECK(reveal_of(R"(,"gapFill":"carryForward")").find(R"("gaps":1,"gapPolicy":"carryForward")") != std::string::npos);
  CHECK(reveal_of("").find(R"("gaps":1,"gapPolicy":"")") != std::string::npos);
}
