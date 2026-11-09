#include "viewmodel/objective_checks.hpp"

#include <algorithm>

#include "viewmodel/format.hpp"

namespace tradelab::desktop {

namespace {

using Kind = json::Value::Kind;

const json::Value* finding(const json::Value& findings, std::string_view code) {
  const auto& items = findings.items();
  const auto it = std::ranges::find_if(items, [&](const json::Value& f) { return f["code"].as_string() == code; });
  return it == items.end() ? nullptr : &*it;
}

// m22: total planned risk across the entries of one idea, in basis points of the initial balance.
bool total_risk(const std::vector<json::Value>& episodes, int value_bp, double initial) {
  const double limit = initial * value_bp / 10'000;
  return std::ranges::all_of(episodes, [&](const json::Value& e) {
    const double risk = fmt::num(e["plannedRisk"].as_string());
    return risk > 0 && risk <= limit + 1e-9;
  });
}

// m22: a partial exit (an exit on a bar before the position's last one, or any exit of a position still
// open at the end) or a trailing stop in some episode. Two orders filling on the closing bar are one exit.
bool partial_exit(const json::Value& e) {
  const bool open = e["exitStep"].is_null();
  const std::int64_t last = open ? 0 : e["exitStep"].as_int();
  return std::ranges::any_of(e["exits"].items(), [&](const json::Value& x) { return open || x["step"].as_int() < last; });
}

bool partial_or_trail(const std::vector<json::Value>& episodes) {
  return std::ranges::any_of(episodes, [](const json::Value& e) { return e["trailing"].as_bool(false) || partial_exit(e); });
}

std::optional<int> stop_away(const std::vector<json::Value>& episodes) {
  for (const json::Value& e : episodes) {
    for (const json::Value& m : e["stopMoves"].items()) {
      if (m["kind"].as_string() == "away") return static_cast<int>(m["step"].as_int());
    }
  }
  return std::nullopt;
}

ObjectiveReading by_finding(const std::string& code, const json::Value* f) {
  if (f == nullptr) return {code, true, std::nullopt};
  return {code, false, static_cast<int>((*f)["step"].as_int())};
}

const json::Value* first_of(const json::Value& findings, std::initializer_list<std::string_view> codes) {
  for (const std::string_view c : codes) {
    if (const json::Value* f = finding(findings, c)) return f;
  }
  return nullptr;
}

std::optional<ObjectiveReading> episode_reading(const std::string& code, int value_bp, const json::Value& d, double initial) {
  const auto& episodes = d["episodes"].items();  // absent from a pre-v14 engine's debrief
  const bool traded = !episodes.empty();
  if (code == "total_risk_all_entries") {
    if (!traded) return std::nullopt;
    return ObjectiveReading{code, total_risk(episodes, value_bp, initial), std::nullopt};
  }
  if (code == "partial_exit_or_trail") {
    if (!traded) return std::nullopt;
    return ObjectiveReading{code, partial_or_trail(episodes), std::nullopt};
  }
  if (code == "never_stop_away") {
    const auto step = stop_away(episodes);
    return ObjectiveReading{code, !step.has_value(), step};
  }
  return std::nullopt;
}

std::optional<ObjectiveReading> reading(const std::string& code, int value_bp, const json::Value& d, double initial) {
  const json::Value& findings = d["findings"];
  if (code == "cooldown_after_stop") return by_finding(code, finding(findings, "revenge_trade"));
  if (code == "plan_before_entry_atr_stop") return by_finding(code, first_of(findings, {"fomo_entry", "no_plan", "no_stop"}));
  if (code == "no_add_beyond_plan") return by_finding(code, finding(findings, "averaging_down"));
  return episode_reading(code, value_bp, d, initial);
}

}  // namespace

std::vector<ObjectiveReading> objective_readings(const json::Value& debrief, double initial_balance) {
  std::vector<ObjectiveReading> out;
  for (const json::Value& o : debrief["missionRules"]["objectives"].items()) {
    if (auto r = reading(o["code"].as_string(), static_cast<int>(o["valueBp"].as_int()), debrief, initial_balance)) out.push_back(*r);
  }
  return out;
}

std::optional<int> finding_moment(const json::Value& f) {
  const json::Value& e = f["evidence"];
  if (e.kind() != Kind::Object) return std::nullopt;
  if (e["entryStep"].kind() == Kind::Number) return static_cast<int>(e["entryStep"].as_int());
  if (e["addStep"].kind() == Kind::Number) return static_cast<int>(e["addStep"].as_int());
  return static_cast<int>(f["step"].as_int());
}

}  // namespace tradelab::desktop
