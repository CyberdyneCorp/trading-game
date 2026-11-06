// Web parity of the Academy ports (add-academy-and-strategies tasks D3, D6, D8): every case of the
// payoff, ticketSandbox, lessonRun, review and glossary fixtures replayed through the C++ port.
#include <doctest/doctest.h>

#include "model/content.hpp"
#include "model/progress_json.hpp"
#include "support/web_fixture.hpp"
#include "viewmodel/academy/glossary.hpp"
#include "viewmodel/academy/payoff.hpp"
#include "viewmodel/academy/review.hpp"
#include "viewmodel/academy/ticket_sandbox.hpp"
#include "viewmodel/lesson_runner.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::academy;
using namespace tradelab::desktop::test_support;

namespace {

PayoffLeg leg(const Value& v) {
  return {v["kind"].as_string(), v["side"].as_string() != "sell", v["qty"].as_double(), v["strike"].as_double(),
          v["price"].as_double()};
}

std::vector<PayoffLeg> legs(const Value& v) {
  std::vector<PayoffLeg> out;
  for (const auto& l : v.items()) out.push_back(leg(l));
  return out;
}

void check_numbers(const std::vector<double>& got, const Value& want) {
  REQUIRE(got.size() == want.size());
  for (std::size_t i = 0; i < got.size(); ++i) check_number(got[i], want[i]);
}

std::map<std::string, Case> payoff_ports() {
  return {
      {"pnlAt", [](const Value& a, const Value& o) { check_number(pnl_at(legs(a[0]), a[1].as_double()), o); }},
      {"legPnlAt", [](const Value& a, const Value& o) { check_number(leg_pnl_at(leg(a[0]), a[1].as_double()), o); }},
      {"breakevens", [](const Value& a, const Value& o) { check_numbers(breakevens(legs(a[0])), o); }},
      {"maxProfit", [](const Value& a, const Value& o) { check_optional(max_profit(legs(a[0])), o); }},
      {"maxLoss", [](const Value& a, const Value& o) { check_optional(max_loss(legs(a[0])), o); }},
      {"payoffCurve",
       [](const Value& a, const Value& o) {
         const auto pts = payoff_curve(legs(a[0]), a[1].as_double(), a[2].as_double(), static_cast<int>(a[3].as_int()));
         REQUIRE(pts.size() == o.size());
         for (std::size_t i = 0; i < pts.size(); ++i) {
           check_number(pts[i].s, o[i]["s"]);
           check_number(pts[i].pnl, o[i]["pnl"]);
         }
       }},
  };
}

TicketWidget widget(const Value& w) {
  TicketWidget t;
  t.market = w["market"].as_string();
  t.equity = w["equity"].as_double();
  t.entry = w["entry"].as_double();
  t.buy = w["side"].as_string() != "sell";
  t.qty = w["qty"].as_double();
  t.leverage = static_cast<int>(w["leverage"].as_int());
  t.max_leverage = static_cast<int>(w["maxLeverage"].as_int());
  t.stop = opt(w["stop"]);
  t.target = opt(w["target"]);
  t.mmr = w["mmr"].as_double();
  t.lot = w["lot"].as_double();
  t.edit = strings(w["edit"]);
  return t;
}

SandboxInputs inputs(const Value& x) {
  return {x["side"].as_string() != "sell", x["qty"].as_double(), static_cast<int>(x["leverage"].as_int()), opt(x["stop"]),
          opt(x["target"])};
}

void check_inputs(const SandboxInputs& got, const Value& o) {
  CHECK(got.buy == (o["side"].as_string() == "buy"));
  check_number(got.qty, o["qty"]);
  CHECK(got.leverage == o["leverage"].as_int());
  check_optional(got.stop, o["stop"]);
  check_optional(got.target, o["target"]);
}

void check_outputs(const SandboxOutputs& got, const Value& o) {
  check_number(got.notional, o["notional"]);
  check_number(got.margin, o["margin"]);
  check_optional(got.liquidation, o["liquidation"]);
  check_optional(got.risk_at_stop, o["riskAtStop"]);
  check_optional(got.risk_pct, o["riskPct"]);
  check_optional(got.r_target, o["rTarget"]);
  CHECK(got.stop_beyond_liquidation == o["stopBeyondLiquidation"].as_bool());
}

std::map<std::string, Case> sandbox_ports() {
  return {
      {"initialInputs", [](const Value& a, const Value& o) { check_inputs(initial_inputs(widget(a[0])), o); }},
      {"sandboxOutputs", [](const Value& a, const Value& o) { check_outputs(sandbox_outputs(widget(a[0]), inputs(a[1])), o); }},
      {"sandboxValue",
       [](const Value& a, const Value& o) {
         const SandboxInputs x = inputs(a[1]);
         // The outputs as the web computed them: only the field read matters.
         SandboxOutputs out;
         out.notional = a[2]["notional"].as_double();
         out.margin = a[2]["margin"].as_double();
         out.liquidation = opt(a[2]["liquidation"]);
         out.risk_at_stop = opt(a[2]["riskAtStop"]);
         out.risk_pct = opt(a[2]["riskPct"]);
         out.r_target = opt(a[2]["rTarget"]);
         check_optional(sandbox_value(a[0].as_string(), x, out), o);
       }},
      {"reachesTarget",
       [](const Value& a, const Value& o) {
         CHECK(reaches_target(opt(a[0]), a[1].as_double(), a[2].as_double()) == o.as_bool());
       }},
  };
}

std::map<std::string, Case> lesson_run_ports() {
  return {
      {"readTypedNumber",
       [](const Value& a, const Value& o) { check_number(read_typed_number(a[0].as_string(), locale(a[1])), o); }},
  };
}

LessonReviews reviews(const Value& v) { return progress_json::reviews_from(v); }

void check_reviews(const LessonReviews& got, const Value& o) {
  REQUIRE(got.size() == o.fields().size());
  for (const auto& [id, e] : o.fields()) {
    const auto it = got.find(id);
    REQUIRE(it != got.end());
    CHECK(it->second.box == e["box"].as_int());
    CHECK(it->second.due == e["due"].as_string());
  }
}

std::map<std::string, Case> review_ports() {
  return {
      {"addDays", [](const Value& a, const Value& o) { check_string(add_days(a[0].as_string(), static_cast<int>(a[1].as_int())), o); }},
      {"scheduleOnComplete",
       [](const Value& a, const Value& o) { check_reviews(schedule_on_complete(reviews(a[0]), a[1].as_string(), a[2].as_string()), o); }},
      {"applyReview",
       [](const Value& a, const Value& o) {
         check_reviews(apply_review(reviews(a[0]), a[1].as_string(), a[2].as_bool(), a[3].as_string()), o);
       }},
      {"dueReviews",
       [](const Value& a, const Value& o) { check_strings(due_reviews(reviews(a[0]), a[1].as_string(), strings(a[2])), o); }},
      {"isRetired",
       [](const Value& a, const Value& o) {
         LessonReviews r;
         r.emplace("x", ReviewEntry{static_cast<int>(a[0]["box"].as_int()), a[0]["due"].as_string()});
         CHECK(is_retired(r, "x") == o.as_bool());
       }},
      {"readReviews", [](const Value& a, const Value& o) { check_reviews(reviews(a[0]), o); }},
  };
}

std::map<std::string, Case> glossary_ports() {
  return {
      {"searchTerms",
       [](const Value& a, const Value& o) {
         Catalog catalog = builtin_catalog();
         catalog.set_locale(locale(a[1]));
         check_strings(search_terms(web_content(), catalog, a[0].as_string()), o);
       }},
      {"fold", [](const Value& a, const Value& o) { check_string(fold(a[0].as_string()), o); }},
  };
}

}  // namespace

TEST_CASE("web parity: content/payoff.ts") { replay("payoff", payoff_ports()); }
TEST_CASE("web parity: content/ticketSandbox.ts") { replay("ticketSandbox", sandbox_ports()); }
TEST_CASE("web parity: lessonRun.svelte.ts") { replay("lessonRun", lesson_run_ports()); }
TEST_CASE("web parity: review.ts") { replay("review", review_ports()); }
TEST_CASE("web parity: content/glossary.ts") { replay("glossary", glossary_ports()); }
