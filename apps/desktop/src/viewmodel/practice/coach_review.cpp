#include "viewmodel/practice/coach_review.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

#include "viewmodel/format.hpp"

namespace tradelab::desktop::practice {

namespace {

using Kind = json::Value::Kind;

std::string price(const json::Value& v, Locale locale) { return v.is_null() ? std::string() : fmt::fmt_price(v.as_string(), locale); }
std::string integer(const json::Value& v) { return v.is_null() ? std::string() : std::to_string(v.as_int()); }
// A basis-point value divided by `scale`, with two decimals ("" when absent).
std::string scaled(const json::Value& v, double scale, Locale locale) {
  return v.is_null() ? std::string() : fmt::fmt_num(static_cast<double>(v.as_int()) / scale, locale, 2);
}
std::string pct(const json::Value& v, Locale locale) { return v.is_null() ? std::string() : scaled(v, 100, locale) + "%"; }

std::string limit_name(const std::string& limit, Locale locale) {
  if (limit == "gamma" && locale == Locale::PtBR) return "gama";
  return limit;  // delta, gamma (en), vega, theta
}

// coachReview.ts SAME: the coach code a debrief finding's code names.
std::string same_code(const std::string& code) {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kSame = {{
      {"revenge_trade", "revenge_trade"},
      {"fomo_entry", "fomo_entry"},
      {"averaging_down", "averaging_down"},
      {"risk_limit_exceeded", "risk_limit_exceeded"},
      {"no_stop", "entry_without_stop"},
      {"oversized", "risk_above_budget"},
  }};
  const auto it = std::ranges::find_if(kSame, [&](const auto& kv) { return kv.first == code; });
  return it == kSame.end() ? std::string() : std::string(it->second);
}

template <class T>
std::vector<T> up_to(const std::vector<T>& v, int step) {
  const auto n = static_cast<std::size_t>(std::max(0, step + 1));
  return {v.begin(), v.begin() + static_cast<std::ptrdiff_t>(std::min(n, v.size()))};
}

void cut_indicators(Indicators& ind, int step) {
  for (Series* s : {&ind.sma20, &ind.ema50, &ind.rsi14, &ind.atr14, &ind.vwap, &ind.bb_upper, &ind.bb_lower, &ind.macd,
                    &ind.macd_signal, &ind.macd_hist, &ind.plus_di14, &ind.minus_di14, &ind.adx14}) {
    *s = up_to(*s, step);
  }
  std::erase_if(ind.patterns, [step](const PatternHit& p) { return p.step > step; });
}

}  // namespace

CoachMoment coach_moment_from(const json::Value& v) {
  return {static_cast<int>(v["step"].as_int()), v["code"].as_string(), v["tone"].as_string(), v["evidence"],
          v["lesson"].as_string(), v["glossary"].as_string()};
}

CoachReview coach_review_from(const json::Value& v) {
  CoachReview out;
  if (v.kind() != Kind::Object) {
    out.error = "unsupported";
    return out;
  }
  out.error = v["error"].as_string();
  for (const json::Value& m : v["moments"].items()) out.moments.push_back(coach_moment_from(m));
  return out;
}

std::string moment_key(const CoachMoment& m) {
  const json::Value& e = m.evidence;
  const std::string base = "coachReview.moment." + m.code;
  if (m.code == "added_against_structure" && e.has("close") && e.has("sma") &&
      fmt::num(e["close"].as_string()) > fmt::num(e["sma"].as_string())) {
    return base + ".short";
  }
  if (m.code == "held_into_release_unhedged" && !e.has("qty")) return base + ".vega";
  if (m.code == "revenge_trade" && e["planned"].as_bool(false)) return base + ".planned";
  if ((m.code == "averaging_down" || m.code == "fomo_entry") && !(fmt::num(e["stop"].as_string("0")) > 0)) return base + ".nostop";
  return base;
}

Params moment_params(const CoachMoment& m, Locale locale) {
  const json::Value& e = m.evidence;
  Params out{{"step", std::to_string(m.step)}, {"qty", e.is_null() || !e.has("qty") ? std::string() : fmt::fmt_qty(e["qty"].as_string(), locale)}};
  for (const char* key : {"entry", "close", "sma", "level", "stop", "from", "to", "price", "risk", "budget", "loss", "plannedRisk",
                          "avgBefore", "avgAfter", "riskBefore", "riskAfter", "planRisk"}) {
    out.emplace_back(key, price(e[key], locale));
  }
  out.emplace_back("trade", integer(e["trade"]));
  out.emplace_back("exit", integer(e["exitStep"]));
  out.emplace_back("ratio", scaled(e["sizeRatioBp"], 10'000, locale));
  out.emplace_back("closes", integer(e["closes"]));
  out.emplace_back("ext", scaled(e["extensionAtr"], 100, locale));
  out.emplace_back("release", integer(e["releaseStep"]));
  out.emplace_back("limit", e.has("limit") ? limit_name(e["limit"].as_string(), locale) : std::string());
  out.emplace_back("limitPct", pct(e["limitBp"], locale));
  out.emplace_back("valuePct", pct(e["valueBp"], locale));
  out.emplace_back("r", scaled(e["profitRBp"], 10'000, locale));
  return out;
}

std::string moment_text(const CoachMoment& m, const Catalog& catalog) { return catalog.tr(moment_key(m), moment_params(m, catalog.locale())); }

GameState state_at(const GameState& s, int step) {
  GameState out = s;
  out.step = step;
  out.bars = up_to(s.bars, step);
  cut_indicators(out.indicators, step);
  std::erase_if(out.events, [step](const EngineEvent& e) { return e.step > step; });
  std::erase_if(out.news, [step](const News& n) { return n.step > step; });
  out.orders.clear();
  out.position.qty = 0;
  out.position.qty_text = "0";
  out.position.entry = 0;
  out.position.entry_text = "0";
  out.account.liquidation.reset();
  out.account.liquidation_text.clear();
  out.upcoming.clear();
  out.calendar.clear();
  return out;
}

std::string mistake_outcome(const std::string& code, int step, const std::vector<CoachMoment>& moments,
                            const std::vector<FindingRef>& findings) {
  const auto in_window = [step](int s) { return s >= step && s <= step + kRepeatWindow; };
  const std::string mapped = same_code(code);
  const std::string coach_code = mapped.empty() ? code : mapped;
  const bool by_moment = std::ranges::any_of(moments, [&](const CoachMoment& m) { return m.code == coach_code && in_window(m.step); });
  const bool by_finding = std::ranges::any_of(findings, [&](const FindingRef& f) {
    return (f.code == code || same_code(f.code) == coach_code) && in_window(f.step);
  });
  return by_moment || by_finding ? "repeated" : "avoided";
}

std::string mistake_name(const std::string& code) {
  static constexpr std::array<std::string_view, 11> kNamed = {
      "entry_without_stop", "stop_moved_away", "added_against_structure", "risk_above_budget", "held_into_release_unhedged",
      "revenge_trade",      "fomo_entry",      "averaging_down",          "risk_limit_exceeded", "oversized",
      "leverage_vs_stop"};
  return std::ranges::find(kNamed, code) != kNamed.end() ? "mistake.name." + code : "mistake.name.other";
}

}  // namespace tradelab::desktop::practice
