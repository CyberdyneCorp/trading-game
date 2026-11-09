// Web parity of wave 1 of add-practice-and-advanced-strategies: orderRules.ts and stops.ts trailTrigger
// (time in force, OCO links, the trailing stop), heatmap.ts and portfolio.ts. Every case of
// tests/fixtures/web/{orderRules,heatmap}.json, written by scripts/gen_web_fixtures.mjs from the web's
// own modules, replayed through the C++ ports (numbers with a 1e-9 relative tolerance).
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
#include "viewmodel/game/heatmap.hpp"
#include "viewmodel/game/order_rules.hpp"
#include "viewmodel/report_texts.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;
using json::Value;

namespace {

using Port = std::function<std::string(const Value& args)>;

Value fixture(const std::string& name) {
  std::ifstream in(std::string(TRADELAB_WEB_FIXTURES_DIR) + "/" + name + ".json", std::ios::binary);
  REQUIRE_MESSAGE(in.good(), "missing fixture " << name << ".json: run scripts/gen_web_fixtures.mjs");
  std::ostringstream text;
  text << in.rdbuf();
  auto v = json::parse(text.str());
  REQUIRE(v.has_value());
  return *v;
}

bool same(const Value& got, const Value& want);

bool same_object(const Value& got, const Value& want) {
  if (got.fields().size() != want.fields().size()) return false;
  return std::all_of(want.fields().begin(), want.fields().end(),
                     [&](const auto& kv) { return got.has(kv.first) && same(got[kv.first], kv.second); });
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
    case Value::Kind::Bool: return got.as_bool() == want.as_bool();
    case Value::Kind::String: return got.as_string() == want.as_string();
    case Value::Kind::Array:
      if (got.size() != want.size()) return false;
      for (std::size_t i = 0; i < want.size(); ++i) {
        if (!same(got[i], want[i])) return false;
      }
      return true;
    case Value::Kind::Object: return same_object(got, want);
    default: return true;
  }
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
std::string null_or_text(const std::string& s) { return s.empty() ? std::string("null") : text(s); }
Locale locale_of(const Value& v) { return v.as_string() == "en" ? Locale::En : Locale::PtBR; }

Order order(const Value& o) {
  return game_state_from_json(*json::parse("{\"orders\":[" + storage::dump(o) + "]}")).orders.front();
}

std::vector<Order> orders(const Value& v) {
  return game_state_from_json(*json::parse("{\"orders\":" + storage::dump(v) + "}")).orders;
}

TifChoice choice(const Value& v) { return tif_choice_from(v.as_string()).value_or(TifChoice::Gtc); }

std::map<std::string, Port> order_rule_ports() {
  return {
      {"dayBars", [](const Value& a) { return std::to_string(day_bars(static_cast<int>(a[0].as_int()))); }},
      {"barsLeft",
       [](const Value& a) {
         return std::to_string(bars_left(static_cast<int>(a[0]["totalBars"].as_int()), static_cast<int>(a[0]["step"].as_int())));
       }},
      {"tifBlock", [](const Value& a) { return null_or_text(tif_block(choice(a[0]), a[1].as_string())); }},
      {"tifOrder",
       [](const Value& a) {
         const TifOrder o = tif_order(choice(a[0]), a[1].as_double(), static_cast<int>(a[2].as_int()), static_cast<int>(a[3].as_int()));
         if (!o.ok()) return "{\"error\":" + text(o.error) + "}";
         return "{\"tif\":" + text(o.tif) + (o.tif == "bars" ? ",\"tifBars\":" + std::to_string(o.bars) : "") + "}";
       }},
      {"tifCell",
       [](const Value& a) {
         const TifCell c = tif_cell(order(a[0]));
         return "{\"kind\":" + text(c.kind) + ",\"until\":" + (c.until ? std::to_string(*c.until) : "null") + "}";
       }},
      {"linkProblem", [](const Value& a) { return null_or_text(link_problem(order(a[0]), order(a[1]))); }},
      {"linkMark", [](const Value& a) { return null_or_text(link_mark(order(a[0]), orders(a[1]))); }},
      {"bestClose",
       [](const Value& a) {
         std::vector<double> closes;
         for (const auto& c : a[0].items()) closes.push_back(c.is_null() ? NAN : c.as_double());
         const auto b = best_close(closes, static_cast<int>(a[1].as_int()), a[2].as_string() == "sell");
         return b ? "{\"step\":" + std::to_string(b->step) + ",\"close\":" + num(b->close) + "}" : std::string("null");
       }},
      {"trailTrigger",
       [](const Value& a) {
         return text(trail_trigger(a[0].as_string() == "sell", a[1].as_string(), a[2]["distance"].as_string(), a[2]["rate"].as_string(),
                                   a[3].as_string()));
       }},
      {"trailLabel", [](const Value& a) { return text(trail_label(order(a[0]), locale_of(a[1]))); }},
      {"plannedTrailOrder",
       [](const Value& a) {
         const auto o = planned_trail_order({a[0]["mode"].as_string() == "pct", a[0]["value"].as_string()}, a[1].as_double(), Locale::En);
         if (!o) return std::string("null");
         return o->rate.empty() ? "{\"trail\":" + text(o->trail) + "}" : "{\"trailRate\":" + text(o->rate) + "}";
       }},
  };
}

WhatIfHeatmap heatmap_of(const Value& m) {
  const auto r = whatif_from_json(R"({"net":{},"legs":[],"today":[],"heatmap":)" + storage::dump(m) + "}");
  REQUIRE(r.ok());
  return r.value->heatmap.value_or(WhatIfHeatmap{});
}

std::vector<AnalysisLeg> analysis_legs(const Value& v) {
  std::vector<AnalysisLeg> out;
  for (const auto& l : v.items()) {
    AnalysisLeg a;
    a.series = static_cast<int>(l["series"].as_int());
    a.buy = l["side"].as_string() != "sell";
    a.qty = l["qty"].as_string();
    if (l.has("entry")) a.entry = l["entry"].as_string();
    out.push_back(a);
  }
  return out;
}

std::optional<WhatIfRisk> risk_of(const Value& v) {
  if (v.is_null()) return std::nullopt;
  const auto r = whatif_from_json(R"({"net":{},"legs":[],"today":[],"risk":)" + storage::dump(v) + "}");
  return r.ok() ? r.value->risk : std::nullopt;
}

std::string limit_json(const RiskLimitCheck& l) {
  return "{\"kind\":" + text(l.kind) + ",\"limitBp\":" + std::to_string(l.limit_bp) + ",\"valueBp\":" + std::to_string(l.value_bp) +
         ",\"exceeded\":" + (l.exceeded ? "true" : "false") + "}";
}

std::string leg_json(const OrderLeg& o) {
  return "{\"series\":" + std::to_string(o.series) + ",\"side\":" + text(o.buy ? "buy" : "sell") + ",\"qty\":" + text(o.qty) + "}";
}

template <typename T, typename F>
std::string array(const std::vector<T>& items, F each) {
  std::string out = "[";
  for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + each(items[i]);
  return out + "]";
}

std::map<std::string, Port> heatmap_ports() {
  return {
      {"heatmapTimes",
       [](const Value& a) {
         return array(heatmap_times(a[0].as_double(), static_cast<int>(a[1].as_int())), [](int t) { return std::to_string(t); });
       }},
      {"heatmapRange",
       [](const Value& a) {
         const HeatmapRange r = heatmap_range(a[0].as_double(), a[1].as_double());
         return "{\"lo\":" + text(r.lo) + ",\"hi\":" + text(r.hi) + "}";
       }},
      {"heatmapRequest",
       [](const Value& a) {
         return heatmap_request(analysis_legs(a[0]), a[1].as_double(), a[2].as_double(), a[3].as_double(), a[4].as_bool());
       }},
      {"heatmapScale", [](const Value& a) { return num(heatmap_scale(heatmap_of(a[0]))); }},
      {"cellStyle",
       [](const Value& a) {
         const HeatmapCell c = heatmap_cell(a[0].as_double(), a[1].as_double());
         const std::string ink = c.up ? "var(--up)" : "var(--down)";
         return text("background: color-mix(in srgb, " + ink + " " + std::to_string(c.share) + "%, var(--bg, #081010))");
       }},
      {"cellParams",
       [](const Value& a) {
         const auto c = heatmap_cell_text(heatmap_of(a[0]), static_cast<std::size_t>(a[1].as_int()), static_cast<std::size_t>(a[2].as_int()),
                                          locale_of(a[3]));
         return "{\"price\":" + text(c.price) + ",\"bars\":" + std::to_string(c.bars) + ",\"pnl\":" + text(c.pnl) + "}";
       }},
      {"portfolioRows",
       [](const Value& a) {
         const Value& legs = a[0]["legs"];
         const auto r = whatif_from_json(R"({"net":{},"today":[],"legs":)" + storage::dump(legs) + "}");
         REQUIRE(r.ok());
         const auto rows = portfolio_rows(*r.value);
         std::string out = "[";
         for (std::size_t i = 0; i < rows.size(); ++i) {
           out += (i ? "," : "") + std::string("{\"leg\":") + storage::dump(legs[i]) + ",\"qty\":" + num(rows[i].qty) + "}";
         }
         return out + "]";
       }},
      {"exceededLimits", [](const Value& a) { return array(exceeded_limits(risk_of(a[0])), limit_json); }},
      {"declaredLimits",
       [](const Value& a) {
         const GameState g = game_state_from_json(*json::parse(R"({"missionRules":)" + storage::dump(a[0]["missionRules"]) + "}"));
         return array(declared_limits(g.rules),
                      [](const DeclaredLimit& d) { return "{\"kind\":" + text(d.kind) + ",\"bp\":" + std::to_string(d.bp) + "}"; });
       }},
      {"hedgeTicket",
       [](const Value& a) {
         const auto h = hedge_ticket(game_state_from_json(a[0]), risk_of(a[1]));
         return h ? leg_json(*h) : std::string("null");
       }},
      {"deltaAfterHedge",
       [](const Value& a) {
         const auto risk = risk_of(a[1]);
         const OrderLeg hedge{0, a[2]["side"].as_string() != "sell", a[2]["qty"].as_string(), std::nullopt};
         return num(delta_after_hedge(game_state_from_json(a[0]), *risk, hedge));
       }},
  };
}

}  // namespace

TEST_CASE("web parity: orderRules.ts (time in force, OCO links, trailing stop) and stops.ts trailTrigger") {
  replay("orderRules", order_rule_ports());
}

TEST_CASE("web parity: heatmap.ts and portfolio.ts (requests, cells, rows, limits, hedge ticket)") {
  replay("heatmap", heatmap_ports());
}
