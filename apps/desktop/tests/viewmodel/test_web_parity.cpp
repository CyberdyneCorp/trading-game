// Web parity (tasks 1.8): every case of tests/fixtures/web/*.json, written by
// scripts/gen_web_fixtures.mjs from the web's own helpers, replayed through the C++ port. Numbers are
// compared with a 1e-9 relative tolerance, texts and flags exactly; JSON null is "none" (or NaN).
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <string>

#include "model/content.hpp"
#include "model/i18n.hpp"
#include "model/json.hpp"
#include "model/storage/json_writer.hpp"
#include "viewmodel/audio/audio_rules.hpp"
#include "viewmodel/campaign.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/hotkeys.hpp"
#include "viewmodel/game/options_math.hpp"
#include "viewmodel/game/progress.hpp"
#include "viewmodel/game/sizing.hpp"
#include "support/web_fixture.hpp"
#include "viewmodel/game/texts.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::game;
using json::Value;
using namespace tradelab::desktop::test_support;

namespace {

Side side(const Value& v) { return v.as_string() == "buy" ? Side::Buy : Side::Sell; }

// ---- sizing.ts / stops.ts ----

Costs costs(const Value& k) {
  Costs c{k["multiplier"].as_double(), k["takerFee"].as_double(), k["maintenanceMargin"].as_double(),
          k["halfSpread"].as_double(), k["slippage"].as_double(), {}};
  for (const auto& t : k["tiers"].items()) {
    c.tiers.push_back({t["floor"].as_double(), t["mmr"].as_double(), t["maxLeverage"].as_double()});
  }
  return c;
}

TicketInput ticket(const Value& v) {
  return {side(v["side"]), v["qty"].as_double(), v["entry"].as_double(), static_cast<int>(v["leverage"].as_int()),
          opt(v["stop"])};
}

OpenPosition position(const Value& v) {
  return {v["qty"].as_double(), v["entry"].as_double(), v["margin"].as_double(), static_cast<int>(v["leverage"].as_int())};
}

SizeBy size_by(const std::string& s) {
  if (s == "notional") return SizeBy::Notional;
  if (s == "equity") return SizeBy::Equity;
  if (s == "risk") return SizeBy::Risk;
  return SizeBy::Qty;
}

std::string_view opposing_name(Opposing o) {
  switch (o) {
    case Opposing::Reduce: return "reduce";
    case Opposing::Close: return "close";
    case Opposing::Reverse: return "reverse";
  }
  return "";
}

void check_estimate(const TicketEstimate& e, const Value& want) {
  check_number(e.notional, want["notional"]);
  check_number(e.margin, want["margin"]);
  check_number(e.fee, want["fee"]);
  check_optional(e.liquidation, want["liquidation"]);
  check_optional(e.planned_risk, want["plannedRisk"]);
  CHECK(e.stop_beyond_liquidation == want["stopBeyondLiquidation"].as_bool());
}

std::map<std::string, Case> sizing_ports() {
  return {
      {"estimatedMarketEntry",
       [](const Value& a, const Value& o) {
         check_number(estimated_market_entry(side(a[0]), a[1].as_double(), costs(a[2])), o);
       }},
      {"liquidationPrice",
       [](const Value& a, const Value& o) {
         check_optional(liquidation_price(side(a[0]), a[1].as_double(), a[2].as_double(), a[3].as_double(), costs(a[4])), o);
       }},
      {"releveredLiquidation",
       [](const Value& a, const Value& o) {
         check_optional(relevered_liquidation(side(a[0]), a[1].as_double(), a[2].as_double(),
                                              static_cast<int>(a[3].as_int()), costs(a[4])),
                        o);
       }},
      {"marginTier",
       [](const Value& a, const Value& o) {
         const Tier t = margin_tier(costs(a[0]), a[1].as_double());
         check_number(t.floor, o["floor"]);
         check_number(t.mmr, o["mmr"]);
         if (o["maxLeverage"].is_null()) CHECK(std::isinf(t.max_leverage));  // JSON has no Infinity
         else check_number(t.max_leverage, o["maxLeverage"]);
       }},
      {"estimate",
       [](const Value& a, const Value& o) {
         const auto pos = a.size() > 2 ? std::optional<OpenPosition>(position(a[2])) : std::nullopt;
         check_estimate(estimate(ticket(a[0]), costs(a[1]), pos), o);
       }},
      {"liquidationAfter",
       [](const Value& a, const Value& o) { check_optional(liquidation_after(ticket(a[0]), position(a[1]), costs(a[2])), o); }},
      {"qtyForRisk",
       [](const Value& a, const Value& o) {
         check_number(qty_for_risk(a[0].as_double(), a[1].as_double(), a[2].as_double(), a[3].as_double(), costs(a[4]),
                                   a[5].as_double()),
                      o);
       }},
      {"requiredMargin",
       [](const Value& a, const Value& o) {
         check_number(required_margin(a[0].as_double(), a[1].as_double(), static_cast<int>(a[2].as_int()), costs(a[3])), o);
       }},
      {"fitToMargin",
       [](const Value& a, const Value& o) {
         const Fit f = fit_to_margin(a[0].as_double(), a[1].as_double(), a[2].as_double(), static_cast<int>(a[3].as_int()),
                                     static_cast<int>(a[4].as_int()), costs(a[5]), a[6].as_double());
         check_number(f.qty, o["qty"]);
         CHECK(f.leverage == o["leverage"].as_int());
       }},
      {"qtyForNotional",
       [](const Value& a, const Value& o) {
         check_number(qty_for_notional(a[0].as_double(), a[1].as_double(), costs(a[2]), a[3].as_double()), o);
       }},
      {"qtyForEquityPct",
       [](const Value& a, const Value& o) {
         check_number(qty_for_equity_pct(a[0].as_double(), a[1].as_double(), a[2].as_double(), costs(a[3]), a[4].as_double()), o);
       }},
      {"sizedQty",
       [](const Value& a, const Value& o) {
         const Value& x = a[1];
         const SizeInputs in{x["equity"].as_double(), x["entry"].as_double(), opt(x["stop"]), x["qty"].as_double(),
                             x["amount"].as_double()};
         check_number(sized_qty(size_by(a[0].as_string()), in, costs(a[2]), a[3].as_double()), o);
       }},
      {"opposingEffect",
       [](const Value& a, const Value& o) {
         const auto e = opposing_effect(a[0].as_double(), side(a[1]), a[2].as_double(), a[3].as_double());
         REQUIRE(e.has_value() == !o.is_null());
         if (!e) return;
         CHECK(opposing_name(e->kind) == o["kind"].as_string());
         check_number(e->open, o["open"]);
         check_number(e->after, o["after"]);
       }},
  };
}

std::vector<GameBar> bars(const Value& v) {
  std::vector<GameBar> out;
  for (const auto& b : v.items()) {
    out.push_back({b[0].as_double(), b[1].as_double(), b[2].as_double(), b[3].as_double(), b[4].as_double(), false, false});
  }
  return out;
}

std::map<std::string, Case> stops_ports() {
  return {
      {"stopFromAtr",
       [](const Value& a, const Value& o) {
         check_optional(stop_from_atr(side(a[0]), a[1].as_double(), opt(a[2]), a[3].as_double()), o);
       }},
      {"stopFromPct", [](const Value& a, const Value& o) { check_optional(stop_from_pct(side(a[0]), a[1].as_double(), a[2].as_double()), o); }},
      {"trailRateFromPercent",
       [](const Value& a, const Value& o) {
         const double pct = a[0].is_null() ? std::numeric_limits<double>::infinity() : a[0].as_double();
         const auto got = trail_rate_from_percent(pct);
         REQUIRE(got.has_value() == !o.is_null());
         if (got) CHECK(*got == o.as_string());
       }},
      {"trailTriggerFromPercent",
       [](const Value& a, const Value& o) {
         check_optional(trail_trigger_from_percent(side(a[0]), a[1].as_double(), a[2].as_double(), a[3].as_double()), o);
       }},
      {"swingStop",
       [](const Value& a, const Value& o) {
         const int lookback = a.size() > 4 ? static_cast<int>(a[4].as_int()) : 10;
         check_optional(swing_stop(side(a[0]), a[1].as_double(), bars(a[2]), a[3].as_double(), lookback), o);
       }},
      {"targetFromR",
       [](const Value& a, const Value& o) {
         check_optional(target_from_r(side(a[0]), a[1].as_double(), opt(a[2]), a[3].as_double()), o);
       }},
      {"rewardRisk",
       [](const Value& a, const Value& o) {
         check_optional(reward_risk(side(a[0]), a[1].as_double(), opt(a[2]), opt(a[3])), o);
       }},
      {"stopOnLosingSide",
       [](const Value& a, const Value& o) { CHECK(stop_on_losing_side(side(a[0]), a[1].as_double(), a[2].as_double()) == o.as_bool()); }},
      {"targetOnWinningSide",
       [](const Value& a, const Value& o) {
         CHECK(target_on_winning_side(side(a[0]), a[1].as_double(), a[2].as_double()) == o.as_bool());
       }},
      {"distanceOf",
       [](const Value& a, const Value& o) {
         const auto d = distance_of(a[0].as_double(), opt(a[1]), opt(a[2]));
         REQUIRE(d.has_value() == !o.is_null());
         if (!d) return;
         check_number(d->abs, o["abs"]);
         check_number(d->pct, o["pct"]);
         check_optional(d->atr, o["atr"]);
       }},
  };
}

// ---- optionsMath.ts ----

OptionQuote quote(const Value& q) {
  OptionQuote out;
  out.id = static_cast<int>(q["id"].as_int());
  out.kind = q["kind"].as_string();
  out.strike = q["strike"].as_double();
  out.strike_text = q["strike"].as_string();
  out.expiry_step = static_cast<int>(q["expiryStep"].as_int());
  out.bid = q["bid"].as_double();
  out.ask = q["ask"].as_double();
  out.delta = q["delta"].as_double();
  out.gamma = q["gamma"].as_double();
  out.theta = q["theta"].as_double();
  out.vega = q["vega"].as_double();
  return out;
}

OptionLeg leg(const Value& l) {
  return {l["kind"].as_string() == "call", l["strike"].as_double(), l["side"].as_string() == "buy", l["qty"].as_double(),
          l["premium"].as_double(), l["multiplier"].as_double()};
}

std::map<std::string, Case> options_ports() {
  return {
      {"legFromQuote",
       [](const Value& a, const Value& o) {
         const OptionLeg l = leg_from_quote(quote(a[0]), a[1].as_string() == "buy", a[2].as_double(), a[3].as_double());
         const OptionLeg want = leg(o);
         CHECK(l.call == want.call);
         CHECK(l.buy == want.buy);
         CHECK(l.strike == doctest::Approx(want.strike));
         CHECK(l.premium == doctest::Approx(want.premium));
         CHECK(l.qty == doctest::Approx(want.qty));
         CHECK(l.multiplier == doctest::Approx(want.multiplier));
       }},
      {"payoffAt", [](const Value& a, const Value& o) { check_number(payoff_at(leg(a[0]), a[1].as_double()), o); }},
      {"breakeven", [](const Value& a, const Value& o) { check_number(breakeven(leg(a[0])), o); }},
      {"maxLoss", [](const Value& a, const Value& o) { check_optional(max_loss(leg(a[0])), o); }},
      {"optionFee", [](const Value& a, const Value& o) { check_number(option_fee(leg(a[0]), a[1].as_double(), a[2].as_double()), o); }},
      {"optionBuyCost",
       [](const Value& a, const Value& o) { check_number(option_buy_cost(leg(a[0]), a[1].as_double(), a[2].as_double()), o); }},
      {"writeMargin",
       [](const Value& a, const Value& o) {
         check_number(write_margin(quote(a[0]), a[1].as_double(), a[2].as_double(), a[3].as_double()), o);
       }},
      {"payoffCurve",
       [](const Value& a, const Value& o) {
         const auto curve = payoff_curve(leg(a[0]), a[1].as_double(), a[2].as_double(), static_cast<int>(a[3].as_int()));
         REQUIRE(curve.size() == o.size());
         for (std::size_t i = 0; i < curve.size(); ++i) {
           check_number(curve[i].s, o[i]["s"]);
           check_number(curve[i].pnl, o[i]["pnl"]);
         }
       }},
      {"seriesLabel", [](const Value& a, const Value& o) { check_string(series_label(quote(a[0]), locale(a[1])), o); }},
      {"positionGreeks",
       [](const Value& a, const Value& o) {
         Options book;
         book.multiplier = a[0]["multiplier"].as_double();
         for (const auto& q : a[0]["chain"].items()) book.chain.push_back(quote(q));
         for (const auto& p : a[0]["positions"].items()) {
           book.positions.push_back({static_cast<int>(p["series"].as_int()), p["qty"].as_double(), 0});
         }
         const BookGreeks g = position_greeks(book);
         check_number(g.delta, o["delta"]);
         check_number(g.gamma, o["gamma"]);
         check_number(g.theta, o["theta"]);
         check_number(g.vega, o["vega"]);
       }},
  };
}

// ---- format.ts ----

std::map<std::string, Case> format_ports() {
  return {
      {"fmtPrice", [](const Value& a, const Value& o) { check_string(fmt::fmt_price(a[0].as_string(), locale(a[1])), o); }},
      {"fmtQty", [](const Value& a, const Value& o) { check_string(fmt::fmt_qty(a[0].as_string(), locale(a[1])), o); }},
      {"fmtMoney",
       [](const Value& a, const Value& o) {
         const bool signed_display = a[2].as_bool();
         if (a[0].kind() == Value::Kind::String) {
           check_string(fmt::fmt_money(std::string_view(a[0].as_string()), locale(a[1]), signed_display), o);
         } else {
           const double v = a[0].is_null() ? std::numeric_limits<double>::quiet_NaN() : a[0].as_double();
           check_string(fmt::fmt_money(v, locale(a[1]), signed_display), o);
         }
       }},
      {"fmtNum",
       [](const Value& a, const Value& o) {
         const double v = a[0].is_null() ? std::numeric_limits<double>::quiet_NaN() : a[0].as_double();
         check_string(fmt::fmt_num(v, locale(a[1]), static_cast<int>(a[2].as_int()), a[3].as_bool()), o);
       }},
      {"fmtPct",
       [](const Value& a, const Value& o) {
         const double v = a[0].is_null() ? std::numeric_limits<double>::quiet_NaN() : a[0].as_double();
         check_string(fmt::fmt_pct(v, locale(a[1]), static_cast<int>(a[2].as_int()), a[3].as_bool()), o);
       }},
      {"parseDecimal", [](const Value& a, const Value& o) { check_number(fmt::parse_decimal(a[0].as_string()), o); }},
      {"parseAmount", [](const Value& a, const Value& o) { check_number(fmt::parse_amount(a[0].as_string(), locale(a[1])), o); }},
      {"signMark", [](const Value& a, const Value& o) { CHECK(fmt::sign_mark(a[0].as_double()) == o.as_string()); }},
      {"qtyToLot",
       [](const Value& a, const Value& o) {
         const double q = a[0].is_null() ? std::numeric_limits<double>::quiet_NaN() : a[0].as_double();
         check_string(fmt::qty_to_lot(q, a[1].as_double()), o);
       }},
      {"priceToTick", [](const Value& a, const Value& o) { check_string(fmt::price_to_tick(a[0].as_double(), a[1].as_double()), o); }},
      {"priceInputText",
       [](const Value& a, const Value& o) { check_string(fmt::price_input_text(a[0].as_double(), a[1].as_double(), locale(a[2])), o); }},
      {"qtyInputText",
       [](const Value& a, const Value& o) { check_string(fmt::qty_input_text(a[0].as_double(), a[1].as_double(), locale(a[2])), o); }},
  };
}

// ---- hotkeys.ts ----

Focus focus(const std::string& f) {
  static const std::map<std::string, Focus> kinds = {
      {"none", Focus::None},     {"text", Focus::Text},   {"textarea", Focus::Text},         {"select", Focus::Select},
      {"slider", Focus::Slider}, {"radio", Focus::Radio}, {"checkbox", Focus::Checkbox}, {"button", Focus::Button}};
  return kinds.at(f);
}

std::map<std::string, Case> hotkeys_ports() {
  return {{"keepsKey", [](const Value& a, const Value& o) {
             const std::string key = a[1].as_string() == " " ? "Space" : a[1].as_string();  // e.key " " is the shortcut "Space"
             CHECK(keeps_key(focus(a[0].as_string()), key) == o.as_bool());
           }}};
}

// ---- advisor.ts ----

std::map<std::string, Case> advisor_ports() {
  return {
      {"voteMark", [](const Value& a, const Value& o) { check_string(vote_mark(static_cast<int>(a[0].as_int())), o); }},
      {"pctFromBp", [](const Value& a, const Value& o) { CHECK(pct_from_bp(static_cast<int>(a[0].as_int())) == o.as_int()); }},
      {"reasonLine",
       [](const Value& a, const Value& o) {
         Catalog catalog = builtin_catalog();
         catalog.set_locale(locale(a[2]));
         const AdviceReason r{a[0]["code"].as_string(), static_cast<int>(a[0]["vote"].as_int()), static_cast<int>(a[0]["ref"].as_int())};
         std::vector<PatternHit> patterns;
         for (const auto& p : a[1].items()) {
           patterns.push_back({static_cast<int>(p["step"].as_int()), p["id"].as_string(), static_cast<int>(p["bars"].as_int()),
                               static_cast<int>(p["bias"].as_int())});
         }
         check_string(advice_reason_line(Texts(catalog), r, patterns), o);
       }},
  };
}

// ---- points.ts ----

std::map<std::string, Case> points_ports() {
  return {
      {"allowanceFor",
       [](const Value& a, const Value& o) {
         GamePrefs p;
         p.advisor_points = static_cast<int>(std::floor(a[0]["advisorPoints"].as_double()));
         CHECK(allowance_for(p) == o.as_int());
       }},
      {"settleRun",
       [](const Value& a, const Value& o) {
         GamePrefs p;
         p.advisor_points = static_cast<int>(a[0]["advisorPoints"].as_int());
         p.settled_runs = strings(a[0]["settledRuns"]);
         settle_run(p, a[1].as_string(), static_cast<int>(a[2].as_int()), static_cast<int>(a[3].as_int()));
         CHECK(p.advisor_points == o["advisorPoints"].as_int());
         check_strings(p.settled_runs, o["settledRuns"]);
       }},
  };
}

// ---- campaign.ts ----

std::string_view status_name(CampaignStatus s) {
  switch (s) {
    case CampaignStatus::Active: return "active";
    case CampaignStatus::Bankrupt: return "bankrupt";
    case CampaignStatus::Complete: return "complete";
    case CampaignStatus::Abandoned: return "abandoned";
  }
  return "";
}

std::string_view result_name(LegResult r) {
  switch (r) {
    case LegResult::Finished: return "finished";
    case LegResult::Bankrupt: return "bankrupt";
    case LegResult::Left: return "left";
  }
  return "";
}

LegResult result_of(const std::string& s) {
  if (s == "bankrupt") return LegResult::Bankrupt;
  if (s == "left") return LegResult::Left;
  return LegResult::Finished;
}

void check_run(const CampaignRun& c, const Value& want) {
  CHECK(c.id == want["id"].as_string());
  CHECK(c.capital == want["capital"].as_string());
  CHECK(c.index == want["index"].as_int());
  CHECK(status_name(c.status) == want["status"].as_string());
  REQUIRE(c.legs.size() == want["legs"].size());
  for (std::size_t i = 0; i < c.legs.size(); ++i) {
    const Value& l = want["legs"][i];
    CHECK(c.legs[i].run_id == l["runId"].as_string());
    CHECK(c.legs[i].mission_id == l["missionId"].as_string());
    CHECK(c.legs[i].start_capital == l["startCapital"].as_string());
    CHECK(c.legs[i].end_capital == l["endCapital"].as_string());
    CHECK(result_name(c.legs[i].result) == l["result"].as_string());
  }
}

// The fixture is one scripted sequence: each case applies its step to the progress the previous left.
std::map<std::string, Case> campaign_ports(Progress& p) {
  const WebContent& c = web_content();
  return {
      {"step",
       [&p, &c](const Value& a, const Value& o) {
         const std::string op = a[0].as_string();
         if (op == "start") {
           p = start_campaign(c, p, a[1].as_string(), a[2].as_int());
         } else if (op == "abandon") {
           p = abandon_campaign(c, p);
         } else {
           const CampaignLink link{a[1]["id"].as_string(), static_cast<int>(a[1]["index"].as_int())};
           const Value& out = a[2];
           p = settle_campaign_run(c, p, link,
                                   {out["runId"].as_string(), out["missionId"].as_string(), result_of(out["result"].as_string()),
                                    out["equity"].as_string(), std::nullopt});
         }
         REQUIRE(p.campaign.has_value() == !o["campaign"].is_null());
         if (p.campaign) check_run(*p.campaign, o["campaign"]);
         REQUIRE(p.campaign_history.size() == o["history"].size());
         for (std::size_t i = 0; i < p.campaign_history.size(); ++i) check_run(p.campaign_history[i], o["history"][i]);
         check_strings(p.assessed, o["assessed"]);
         CHECK(next_campaign_mission(c, p.campaign) == o["next"].as_string());
       }},
      {"belowCampaignMinimum",
       [&c](const Value& a, const Value& o) { CHECK(below_campaign_minimum(c, a[0].as_string()) == o.as_bool()); }},
      {"bestCompletedCapital",
       [](const Value& a, const Value& o) {
         std::vector<CampaignRun> history;
         for (const auto& h : a[0].items()) {
           CampaignRun r;
           r.id = h["id"].as_string();
           r.capital = h["capital"].as_string();
           r.status = h["status"].as_string() == "complete" ? CampaignStatus::Complete : CampaignStatus::Abandoned;
           history.push_back(r);
         }
         CHECK(best_completed_capital(history) == o.as_string());
       }},
  };
}

// ---- curriculum.ts ----

std::map<std::string, Case> curriculum_ports() {
  const WebContent& c = web_content();
  return {
      {"campaignMissions", [&c](const Value&, const Value& o) { check_strings(campaign_missions(c), o); }},
      {"stagePassed",
       [&c](const Value& a, const Value& o) {
         CHECK(stage_passed(c, a[0].as_string(), strings(a[1]), strings(a[2])) == o.as_bool());
       }},
      {"stageStatus",
       [&c](const Value& a, const Value& o) {
         CHECK(stage_status_name(stage_status(c, a[0].as_string(), strings(a[1]), strings(a[2]))) == o.as_string());
       }},
      {"curriculumSummary",
       [&c](const Value& a, const Value& o) {
         const auto s = curriculum_summary(c, strings(a[0]), strings(a[1]));
         CHECK(s.passed == o["passed"].as_int());
         CHECK(s.with_content == o["withContent"].as_int());
       }},
      {"campaignPermissions",
       [&c](const Value& a, const Value& o) {
         const Permissions p = campaign_permissions(c, a[0].as_string(), strings(a[1]));
         check_strings(p.granted, o["granted"]);
         check_strings(p.locked, o["locked"]);
         REQUIRE(p.leverage_cap.has_value() == !o["leverageCap"].is_null());
         if (p.leverage_cap) CHECK(*p.leverage_cap == o["leverageCap"].as_int());
       }},
      {"stagesOf", [&c](const Value& a, const Value& o) { check_strings(stages_of(c, a[0].as_string()), o); }},
      {"lessonProgress",
       [&c](const Value& a, const Value& o) {
         const LessonProgress p = lesson_progress(c, a[0].as_string(), strings(a[1]));
         CHECK(p.done == o["done"].as_int());
         CHECK(p.total == o["total"].as_int());
       }},
      {"recommendedLesson",
       [&c](const Value& a, const Value& o) {
         CHECK(recommended_lesson(c, a[0].as_string(), strings(a[1])) == (o.is_null() ? "" : o.as_string()));
       }},
      {"lessonCodeStage",
       [&c](const Value& a, const Value& o) {
         CHECK(lesson_code_stage(c, a[0].as_string()) == (o.is_null() ? "" : o.as_string()));
       }},
      {"unlockStage", [&c](const Value& a, const Value& o) { CHECK(unlock_stage(c, a[0].as_string()) == o.as_string()); }},
  };
}

// ---- audioRules.ts ----

GameState audio_state(const Value& s) {
  GameState g;
  g.status = s["status"].as_string("active");
  g.step = static_cast<int>(s["step"].as_int(50));
  g.account.mark = s["mark"].as_double(100);
  g.account.equity = s["equity"].as_double(1000);
  if (!s["liq"].is_null()) g.account.liquidation = s["liq"].as_double();
  g.position.qty = s["qty"].as_double(0);
  g.options.maintenance_margin = s["mm"].as_double(0);
  for (const auto& e : s["events"].items()) {
    EngineEvent ev;
    ev.seq = static_cast<int>(e["seq"].as_int());
    ev.step = static_cast<int>(e["step"].as_int());
    ev.type = e["type"].as_string();
    g.events.push_back(ev);
  }
  return g;
}

std::vector<EngineEvent> events(const Value& v) {
  std::vector<EngineEvent> out;
  for (const auto& e : v.items()) {
    EngineEvent ev;
    ev.seq = static_cast<int>(e["seq"].as_int());
    ev.step = static_cast<int>(e["step"].as_int());
    ev.type = e["type"].as_string();
    ev.pnl = e["pnl"].as_string();
    out.push_back(ev);
  }
  return out;
}

std::optional<audio::Mood> mood_of(const Value& v) {
  if (v.is_null()) return std::nullopt;
  const std::string s = v.as_string();
  return s == "tense" ? audio::Mood::Tense : (s == "game" ? audio::Mood::Game : audio::Mood::Menu);
}

std::string_view mood_name(audio::Mood m) {
  return m == audio::Mood::Tense ? "tense" : (m == audio::Mood::Game ? "game" : "menu");
}

void check_sting(const std::optional<audio::Sting>& got, const Value& want) {
  REQUIRE(got.has_value() == !want.is_null());
  if (got) CHECK((*got == audio::Sting::Win ? "win" : "loss") == want.as_string());
}

std::map<std::string, Case> audio_ports(audio::StingTracker& tracker) {
  return {
      {"inDanger", [](const Value& a, const Value& o) { CHECK(audio::in_danger(audio_state(a[0]), a[1].as_bool()) == o.as_bool()); }},
      {"moodFor",
       [](const Value& a, const Value& o) {
         const bool terminal = a[0].as_string() == "terminal";
         if (a[1].is_null()) {
           CHECK(mood_name(audio::mood_for(terminal, nullptr, mood_of(a[2]))) == o.as_string());
           return;
         }
         const GameState g = audio_state(a[1]);
         CHECK(mood_name(audio::mood_for(terminal, &g, mood_of(a[2]))) == o.as_string());
       }},
      {"stingFor",
       [](const Value& a, const Value& o) { check_sting(audio::sting_for(events(a[0]), static_cast<int>(a[1].as_int())), o); }},
      {"StingTracker.observe",
       [&tracker](const Value& a, const Value& o) { check_sting(tracker.observe(a[0].as_string(), events(a[1])), o); }},
  };
}

}  // namespace

TEST_CASE("web parity: sizing.ts") { replay("sizing", sizing_ports()); }
TEST_CASE("web parity: stops.ts") { replay("stops", stops_ports()); }
TEST_CASE("web parity: optionsMath.ts") { replay("optionsMath", options_ports()); }
TEST_CASE("web parity: format.ts") { replay("format", format_ports()); }
TEST_CASE("web parity: hotkeys.ts") { replay("hotkeys", hotkeys_ports()); }
TEST_CASE("web parity: advisor.ts") { replay("advisor", advisor_ports()); }
TEST_CASE("web parity: points.ts") { replay("points", points_ports()); }

TEST_CASE("web parity: campaign.ts") {
  Progress p;
  replay("campaign", campaign_ports(p));
}

TEST_CASE("web parity: curriculum.ts") { replay("curriculum", curriculum_ports()); }

TEST_CASE("web parity: audioRules.ts") {
  audio::StingTracker tracker;
  replay("audioRules", audio_ports(tracker));
}
