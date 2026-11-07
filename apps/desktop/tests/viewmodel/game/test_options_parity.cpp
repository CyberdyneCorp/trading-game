// Web parity of the strategy builder, its combos and the what-if lab (add-academy-and-strategies D3,
// D4): every case of tests/fixtures/web/{strategies,combos,whatifLab}.json, written by
// scripts/gen_web_fixtures.mjs from the web's own modules, replayed through the C++ ports. The C++
// result is written as the web's JSON and compared field by field (numbers with a 1e-9 relative
// tolerance, everything else exactly).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>

#include "model/game_state.hpp"
#include "model/json.hpp"
#include "model/storage/json_writer.hpp"
#include "viewmodel/game/combos.hpp"
#include "viewmodel/game/strategies.hpp"
#include "viewmodel/game/whatif_lab.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;
using json::Value;
using storage::JsonWriter;

namespace {

using Port = std::function<std::string(const Value& args)>;  // the C++ result as JSON text

Value fixture(const std::string& name) {
  std::ifstream in(std::string(TRADELAB_WEB_FIXTURES_DIR) + "/" + name + ".json", std::ios::binary);
  REQUIRE_MESSAGE(in.good(), "missing fixture " << name << ".json: run scripts/gen_web_fixtures.mjs");
  std::ostringstream text;
  text << in.rdbuf();
  auto v = json::parse(text.str());
  REQUIRE(v.has_value());
  return *v;
}

bool same(const Value& got, const Value& want) {
  if (want.kind() == Value::Kind::Number) {
    if (got.kind() != Value::Kind::Number) return false;
    const double g = got.as_double();
    const double w = want.as_double();
    return std::abs(g - w) <= 1e-9 * std::max({1.0, std::abs(g), std::abs(w)});
  }
  if (got.kind() != want.kind()) return false;
  switch (want.kind()) {
    case Value::Kind::Null: return true;
    case Value::Kind::Bool: return got.as_bool() == want.as_bool();
    case Value::Kind::String: return got.as_string() == want.as_string();
    case Value::Kind::Array: {
      if (got.size() != want.size()) return false;
      for (std::size_t i = 0; i < want.size(); ++i) {
        if (!same(got[i], want[i])) return false;
      }
      return true;
    }
    case Value::Kind::Object: break;
    case Value::Kind::Number: return false;
  }
  if (got.fields().size() != want.fields().size()) return false;
  return std::all_of(want.fields().begin(), want.fields().end(),
                     [&](const auto& kv) { return got.has(kv.first) && same(got[kv.first], kv.second); });
}

void replay(const std::string& name, const std::map<std::string, Port>& ports) {
  const Value f = fixture(name);
  int n = 0;
  for (const Value& c : f["cases"].items()) {
    const std::string fn = c["fn"].as_string();
    const auto port = ports.find(fn);
    REQUIRE_MESSAGE(port != ports.end(), name << ": no C++ port mapped for " << fn);
    const std::string got = port->second(c["args"]);
    const auto parsed = json::parse(got);
    REQUIRE_MESSAGE(parsed.has_value(), "unreadable C++ output " << got);
    INFO(name << " " << fn << " " << storage::dump(c["args"]));
    CHECK_MESSAGE(same(*parsed, c["out"]), "got  " << got << "\nwant " << storage::dump(c["out"]));
    ++n;
  }
  CHECK(n > 0);
}

std::string num(double v) { return js_number(v); }
std::string text(const std::string& s) {
  std::string out;
  storage::append_json_string(out, s);
  return out;
}
std::string boolean(bool b) { return b ? "true" : "false"; }
template <typename T, typename F>
std::string array(const std::vector<T>& items, F each) {
  std::string out = "[";
  for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + each(items[i]);
  return out + "]";
}
std::string opt_text(const std::optional<std::string>& s) { return s ? text(*s) : "null"; }
std::string opt_num(const std::optional<double>& v) { return v ? num(*v) : "null"; }
std::string opt_int(const std::optional<int>& v) { return v ? std::to_string(*v) : "null"; }

// ---- inputs ----

GameState game(const Value& g) { return game_state_from_json(g); }

std::vector<OptionQuote> chain(const Value& c) {
  return game_state_from_json(*json::parse("{\"options\":{\"chain\":" + storage::dump(c) + "}}")).options.chain;
}

std::vector<double> doubles(const Value& v) {
  std::vector<double> out;
  for (const auto& x : v.items()) out.push_back(x.as_double());
  return out;
}

BuilderLeg builder_leg(const Value& l) {
  BuilderLeg out;
  out.kind = leg_kind_from(l["kind"].as_string()).value_or(LegKind::Call);
  out.buy = l["side"].as_string() != "sell";
  if (!l["strike"].is_null()) out.strike = l["strike"].as_double();
  out.qty = l["qty"].as_double();
  return out;
}

std::vector<BuilderLeg> builder_legs(const Value& v) {
  std::vector<BuilderLeg> out;
  for (const auto& l : v.items()) out.push_back(builder_leg(l));
  return out;
}

OrderLeg order_leg(const Value& o) {
  return {static_cast<int>(o["series"].as_int()), o["side"].as_string() != "sell", o["qty"].as_string()};
}

std::vector<OrderLeg> order_legs_of(const Value& v) {
  std::vector<OrderLeg> out;
  for (const auto& o : v.items()) out.push_back(order_leg(o));
  return out;
}

AnalysisLeg analysis_leg(const Value& l) {
  AnalysisLeg out;
  out.series = static_cast<int>(l["series"].as_int());
  out.buy = l["side"].as_string() != "sell";
  out.qty = l["qty"].as_string();
  if (l.has("entry")) out.entry = l["entry"].as_string();
  return out;
}

std::vector<AnalysisLeg> analysis_legs(const Value& v) {
  std::vector<AnalysisLeg> out;
  for (const auto& l : v.items()) out.push_back(analysis_leg(l));
  return out;
}

EngineEvent event(const Value& e) {
  EngineEvent out;
  out.seq = static_cast<int>(e["seq"].as_int());
  out.step = static_cast<int>(e["step"].as_int());
  out.type = e["type"].as_string();
  out.order_id = static_cast<int>(e["orderId"].as_int());
  out.status = e["status"].as_string();
  out.reason = e["reason"].as_string();
  out.side = e["side"].as_string();
  out.qty = e["qty"].as_string();
  out.price = e["price"].as_string();
  return out;
}

std::vector<EngineEvent> events(const Value& v) {
  std::vector<EngineEvent> out;
  for (const auto& e : v.items()) out.push_back(event(e));
  return out;
}

ComboRecord record(const Value& r) {
  return {r["id"].as_string(), r["strategy"].as_string(), static_cast<int>(r["step"].as_int()), order_legs_of(r["legs"]),
          r["withdrawn"].as_bool()};
}

std::optional<LegState> leg_state_from(const std::string& s) {
  for (const LegState x : {LegState::Working, LegState::Filled, LegState::Cancelled}) {
    if (leg_state_code(x) == s) return x;
  }
  return std::nullopt;
}

LegStatus leg_status(const Value& l) {
  LegStatus out{order_leg(l["leg"]), std::nullopt, LegState::Working, l["reason"].as_string(), std::nullopt, std::nullopt};
  if (!l["orderId"].is_null()) out.order_id = static_cast<int>(l["orderId"].as_int());
  out.state = leg_state_from(l["state"].as_string()).value_or(LegState::Working);
  if (!l["price"].is_null()) out.price = l["price"].as_string();
  if (!l["fillStep"].is_null()) out.fill_step = static_cast<int>(l["fillStep"].as_int());
  return out;
}

ComboStatus status(const Value& s) {
  ComboStatus out{record(s["record"]), ComboState::Working, {}};
  for (const ComboState x : {ComboState::Working, ComboState::Filled, ComboState::Withdrawn, ComboState::NotExecuted,
                             ComboState::Incomplete}) {
    if (combo_state_code(x) == s["state"].as_string()) out.state = x;
  }
  for (const auto& l : s["legs"].items()) out.legs.push_back(leg_status(l));
  return out;
}

std::vector<ComboTrade> trades(const Value& v) {
  // The fixture's nets are decimal strings, as the engine writes them.
  return combo_trades_from(v);
}

AcademyLeg academy_leg(const Value& l) {
  return {leg_kind_from(l["kind"].as_string()).value_or(LegKind::Call),
          l["side"].as_string() != "sell",
          l["strike"].as_double(),
          l["qty"].as_double(),
          l["days"].as_double(),
          l["ivPct"].as_double(),
          l["entry"].as_double()};
}

std::vector<AcademyLeg> academy_legs(const Value& v) {
  std::vector<AcademyLeg> out;
  for (const auto& l : v.items()) out.push_back(academy_leg(l));
  return out;
}

LabShift shift(const Value& s) {
  return {static_cast<int>(s["price"].as_int()), static_cast<int>(s["time"].as_int()), static_cast<int>(s["iv"].as_int())};
}

std::optional<Slider> slider_from(const std::string& s) {
  for (const Slider x : kSliders) {
    if (slider_code(x) == s) return x;
  }
  return std::nullopt;
}

// ---- outputs ----

std::string json_of(const BuilderLeg& l) {
  return "{\"kind\":" + text(std::string(leg_kind_code(l.kind))) + ",\"side\":" + text(l.buy ? "buy" : "sell") +
         ",\"strike\":" + opt_num(l.strike) + ",\"qty\":" + num(l.qty) + "}";
}

std::string json_of(const std::vector<BuilderLeg>& legs) {
  return array(legs, [](const BuilderLeg& l) { return json_of(l); });
}

std::string json_of(const OrderLeg& o) {
  return "{\"series\":" + std::to_string(o.series) + ",\"side\":" + text(o.buy ? "buy" : "sell") + ",\"qty\":" + text(o.qty) + "}";
}

std::string json_of(const std::vector<OrderLeg>& orders) {
  return array(orders, [](const OrderLeg& o) { return json_of(o); });
}

std::string json_of(const ComboRecord& r) {
  return "{\"id\":" + text(r.id) + ",\"strategy\":" + text(r.strategy) + ",\"step\":" + std::to_string(r.step) +
         ",\"legs\":" + json_of(r.legs) + ",\"withdrawn\":" + boolean(r.withdrawn) + "}";
}

std::string json_of(const LegStatus& l) {
  return "{\"leg\":" + json_of(l.leg) + ",\"orderId\":" + opt_int(l.order_id) + ",\"state\":" +
         text(std::string(leg_state_code(l.state))) + ",\"reason\":" + (l.reason.empty() ? "null" : text(l.reason)) +
         ",\"price\":" + opt_text(l.price) + ",\"fillStep\":" + opt_int(l.fill_step) + "}";
}

std::string json_of(const ComboStatus& s) {
  return "{\"record\":" + json_of(s.record) + ",\"state\":" + text(std::string(combo_state_code(s.state))) +
         ",\"legs\":" + array(s.legs, [](const LegStatus& l) { return json_of(l); }) + "}";
}

std::string path_json(const std::vector<ChartPoint>& points) { return text(path_text(points)); }

std::string json_of(const std::optional<PayoffGeometry>& g) {
  if (!g) return "null";
  const auto nums = [](const std::vector<double>& v) { return array(v, [](double x) { return num(x); }); };
  return "{\"expiryPath\":" + (g->expiry.empty() ? std::string("null") : path_json(g->expiry)) +
         ",\"todayPath\":" + path_json(g->today) + ",\"zeroY\":" + num(g->zero_y) + ",\"forwardX\":" + num(g->forward_x) +
         ",\"breakevenX\":" + nums(g->breakeven_x) + ",\"strikeX\":" + nums(g->strike_x) + ",\"lo\":" + num(g->lo) +
         ",\"hi\":" + num(g->hi) + ",\"maxAbs\":" + num(g->max_abs) + "}";
}

std::string shift_json(const LabShift& s) {
  return "{\"price\":" + std::to_string(s.price) + ",\"time\":" + std::to_string(s.time) + ",\"iv\":" + std::to_string(s.iv) + "}";
}

WhatIfResult result(const Value& r) {
  const auto a = whatif_from_json(storage::dump(r));
  REQUIRE(a.ok());
  return *a.value;
}

// ---- ports ----

std::map<std::string, Port> strategy_ports() {
  const auto tmpl = [](const Value& v) { return *template_from(v.as_string()); };
  return {
      {"expiriesOf", [](const Value& a) { return array(expiries_of(chain(a[0])), [](int e) { return std::to_string(e); }); }},
      {"strikesOf",
       [](const Value& a) {
         return array(strikes_of(chain(a[0]), static_cast<int>(a[1].as_int())), [](double k) { return num(k); });
       }},
      {"templateWrites", [tmpl](const Value& a) { return boolean(template_writes(tmpl(a[0]), a[1].as_bool())); }},
      {"strategyOf", [tmpl](const Value& a) { return text(strategy_of(tmpl(a[0]), a[1].as_bool())); }},
      {"templateShortable", [tmpl](const Value& a) { return boolean(template_shortable(tmpl(a[0]))); }},
      {"templateLegs",
       [tmpl](const Value& a) {
         const auto legs = template_legs(tmpl(a[0]), doubles(a[1]), a[2].as_double(), a[3].as_double(), a[4].as_bool());
         return legs ? json_of(*legs) : std::string("null");
       }},
      {"submissionOrder", [](const Value& a) { return json_of(submission_order(builder_legs(a[0]))); }},
      {"writesOptions", [](const Value& a) { return boolean(writes_options(builder_legs(a[0]))); }},
      {"legsProblem",
       [](const Value& a) {
         const std::string p = legs_problem(builder_legs(a[0]));
         return p.empty() ? std::string("null") : text(p);
       }},
      {"orderLegs",
       [](const Value& a) {
         const auto o = order_legs(game(a[0]), static_cast<int>(a[1].as_int()), builder_legs(a[2]));
         return o ? json_of(*o) : std::string("null");
       }},
      {"perpContracts", [](const Value& a) { return text(perp_contracts(game(a[0]), a[1].as_double())); }},
      {"heldLong",
       [](const Value& a) {
         const auto h = held_long(game(a[0]));
         return h ? "{\"qty\":" + text(h->qty) + ",\"entry\":" + text(h->entry) + "}" : std::string("null");
       }},
      {"coverContracts",
       [](const Value& a) { return num(cover_contracts(game(a[0]), HeldLong{a[1]["qty"].as_string(), "0"})); }},
      {"positionLegs", [](const Value& a) { return legs_json(position_legs(game(a[0]))); }},
      {"comboLegs", [](const Value& a) { return legs_json(combo_legs(order_legs_of(a[0]))); }},
      {"whatifLegs",
       [](const Value& a) {
         std::optional<HeldLong> held;
         if (!a[1].is_null()) held = HeldLong{a[1]["qty"].as_string(), a[1]["entry"].as_string()};
         return legs_json(whatif_legs(order_legs_of(a[0]), held));
       }},
  };
}

std::map<std::string, Port> combo_ports() {
  return {
      {"nextComboId",
       [](const Value& a) {
         std::vector<ComboRecord> combos;
         for (const auto& c : a[0].items()) combos.push_back({c["id"].as_string(), "custom", 0, {}, false});
         return text(next_combo_id(combos));
       }},
      {"combosFromLog",
       [](const Value& a) {
         std::vector<std::string> log;
         for (const auto& l : a[0].items()) log.push_back(storage::dump(l));
         return array(combos_from_log(log, static_cast<int>(a[1].as_int())), [](const ComboRecord& r) { return json_of(r); });
       }},
      {"comboOrderIds",
       [](const Value& a) { return array(combo_order_ids(events(a[0]), record(a[1])), [](const auto& id) { return opt_int(id); }); }},
      {"comboStatus", [](const Value& a) { return json_of(combo_status(events(a[0]), record(a[1]))); }},
      {"netPremium", [](const Value& a) { return num(net_premium(status(a[0]), a[1].as_double())); }},
      {"openLegs", [](const Value& a) { return array(open_legs(status(a[0])), [](const LegStatus& l) { return json_of(l); }); }},
      {"comboContracts", [](const Value& a) { return text(combo_contracts(order_legs_of(a[0]))); }},
      {"comboR", [](const Value& a) { return opt_num(combo_r(trades(a[0]), a[1].as_double())); }},
      {"groupTrades",
       [](const Value& a) {
         std::vector<ComboStatus> st;
         for (const auto& s : a[1].items()) st.push_back(status(s));
         const GroupedTrades g = group_trades(trades(a[0]), st);
         // Each trade as the fixture gave it (found by its number).
         const Value& all = a[0];
         const auto trade_json = [&all](const ComboTrade& t) {
           for (const auto& x : all.items()) {
             if (x["n"].as_int() == t.n) return storage::dump(x);
           }
           return std::string("null");
         };
         return "{\"combos\":" + array(g.combos, [&](const ComboTrades& c) {
                  return "{\"status\":" + json_of(c.status) + ",\"trades\":" + array(c.trades, trade_json) +
                         ",\"net\":" + num(c.net) + ",\"r\":" + opt_num(c.r) + "}";
                }) +
                ",\"rest\":" + array(g.rest, trade_json) + "}";
       }},
  };
}

std::map<std::string, Port> lab_ports() {
  return {
      {"shiftedForward", [](const Value& a) { return text(shifted_forward(a[0].as_double(), static_cast<int>(a[1].as_int()))); }},
      {"academyRequest",
       [](const Value& a) { return academy_request(academy_legs(a[0]), shift(a[1]), a[2].as_double()); }},
      {"runRequest", [](const Value& a) { return run_request(analysis_legs(a[0]), shift(a[1]), a[2].as_double()); }},
      {"academyMaxDays", [](const Value& a) { return num(academy_max_days(academy_legs(a[0]))); }},
      {"runMaxBars",
       [](const Value& a) {
         std::vector<int> steps;
         for (const auto& s : a[0].items()) steps.push_back(static_cast<int>(s.as_int()));
         return std::to_string(run_max_bars(steps, static_cast<int>(a[1].as_int())));
       }},
      {"clampShift", [](const Value& a) { return shift_json(clamp_shift(shift(a[0]), static_cast<int>(a[1].as_int()))); }},
      {"setSlider",
       [](const Value& a) {
         std::vector<Slider> unlocked;
         for (const auto& s : a[3].items()) unlocked.push_back(*slider_from(s.as_string()));
         return shift_json(set_slider(shift(a[0]), *slider_from(a[1].as_string()), static_cast<int>(a[2].as_int()), unlocked));
       }},
      {"strikesOf", [](const Value& a) { return array(result_strikes(result(a[0])), [](double k) { return num(k); }); }},
      {"unboundedAbove", [](const Value& a) { return opt_text(unbounded_above(result(a[0]))); }},
      {"chartGeometry",
       [](const Value& a) { return json_of(payoff_geometry(result(a[0]), a[1].as_double(), a[2].as_double(), doubles(a[3]))); }},
  };
}

}  // namespace

TEST_CASE("web parity: strategies.ts (templates, legs, submission order, orders, held long)") {
  replay("strategies", strategy_ports());
}

TEST_CASE("web parity: combos.ts (log, legs followed through events, premium, R, grouped trades)") {
  replay("combos", combo_ports());
}

TEST_CASE("web parity: whatifLab.ts (requests, sliders, the payoff chart's geometry from engine answers)") {
  replay("whatifLab", lab_ports());
}
