// Briefing (web Briefing.svelte): the engine's mission rules (objectives, permissions, limits), the
// contract (margin, tiers, mark price, costs, liquidity shocks) and, in a campaign, what the
// curriculum still keeps locked.
#include "model/content.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/dialogs_vm.hpp"
#include "viewmodel/report_texts.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::screens {

namespace {

constexpr std::array<std::string_view, 4> kPerms = {"long", "short", "optionsBuy", "optionsWrite"};

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (const auto& p : parts) out += (out.empty() ? "" : std::string(sep)) + p;
  return out;
}

std::string kv(const AppViewModel& vm, std::string_view key, const std::string& value) {
  return vm.tr(key) + ": " + value;
}

std::string mode_text(const AppViewModel& vm, const Snapshot& s) {
  const auto& link = vm.shell().run_campaign;
  const std::string seed = std::to_string(s.seed);
  if (link) {
    return vm.tr(link->index == 0 ? "brief.modeCampaignFirst" : "brief.modeCampaign",
                 {{"n", std::to_string(link->index + 1)},
                  {"total", std::to_string(campaign_missions(web_content()).size())},
                  {"seed", seed}});
  }
  return vm.tr("brief.modeSynthetic", {{"seed", seed}});
}

void permissions(const AppViewModel& vm, const json::Value& rules, ScreenModel& m) {
  std::vector<std::string> allowed, not_allowed;
  for (std::string_view p : kPerms) {
    (rules["permissions"][p].as_bool(false) ? allowed : not_allowed).push_back(vm.tr("perm." + std::string(p)));
  }
  m.text(names::kBriefPermissions, kv(vm, "brief.permissions", join(allowed, kSep)));
  m.text(names::kBriefNotAllowed, kv(vm, "brief.notAllowed", join(not_allowed, kSep)));
  m.show(names::kBriefNotAllowed, !not_allowed.empty());
}

// The option margin rule (missionRules.spreadMargin), where options trade and writing is allowed: a
// spread's defined risk, or every written option on its own.
void margin_rule(const AppViewModel& vm, const Snapshot& s, const json::Value& rules, ScreenModel& m) {
  const json::Value& spread = rules["spreadMargin"];
  const bool shown = s.state["options"]["enabled"].as_bool(false) && rules["permissions"]["optionsWrite"].as_bool(false) &&
                     !spread.is_null();
  m.show(names::kBriefMarginRule, shown);
  // add-practice-and-advanced-strategies D5: a long expiring later may cover a short of the same kind.
  const std::string calendar = rules["calendarMargin"].as_bool(false) ? " " + vm.tr("brief.calendarMargin") : std::string();
  if (shown) {
    m.text(names::kBriefMarginRule,
           kv(vm, "brief.marginRule", vm.tr(spread.as_bool() ? "brief.marginRuleSpread" : "brief.marginRulePerLeg") + calendar));
  }
}

// D9: the mission's portfolio risk limits (bp of equity), only those declared.
void risk_limits(const AppViewModel& vm, const json::Value& rules, ScreenModel& m) {
  const json::Value& r = rules["riskLimits"];
  std::string text;
  for (const char* kind : {"deltaBp", "gammaBp", "vegaBp", "thetaBp"}) {
    const auto bp = r[kind].as_int(0);
    if (bp == 0) continue;
    text += (text.empty() ? "" : std::string(kSep)) + vm.tr(std::string("brief.riskLimit.") + kind, {{"n", fmt::fmt_num(static_cast<double>(bp), vm.locale(), 0)}});
  }
  m.show(names::kBriefRiskLimits, !text.empty());
  if (!text.empty()) m.text(names::kBriefRiskLimits, kv(vm, "brief.riskLimits", text));
}

void contract_lines(const AppViewModel& vm, const json::Value& c, ScreenModel& m) {
  const Locale L = vm.locale();
  m.text(names::kBriefMmr, kv(vm, "brief.mmr", fmt::fmt_pct(fmt::num(c["maintenanceMargin"].as_string()), L)));
  const auto tiers = brief_tiers(vm.catalog(), c);
  m.text(names::kBriefTiers, kv(vm, "brief.tiers", join(tiers, kSep)));
  m.show(names::kBriefTiers, !tiers.empty());
  const json::Value& mark = c["markFromLastPrice"];
  m.show(names::kBriefMark, !mark.is_null());
  if (!mark.is_null()) m.text(names::kBriefMark, kv(vm, "brief.mark", vm.tr(mark.as_bool() ? "brief.markLast" : "brief.markSeries")));
  const double slip = fmt::num(c["halfSpread"].as_string()) + fmt::num(c["slippage"].as_string());
  m.text(names::kBriefCosts, kv(vm, "brief.costs",
                                vm.tr("brief.costsValue", {{"taker", fmt::fmt_pct(fmt::num(c["takerFee"].as_string()), L, 3)},
                                                           {"maker", fmt::fmt_pct(fmt::num(c["makerFee"].as_string()), L, 3)},
                                                           {"slip", fmt::fmt_pct(slip, L, 3)},
                                                           {"funding", c["fundingInterval"].as_string()}})));
  const bool shocks = c["liquidityShocks"].as_bool(false);
  m.show(names::kBriefShocks, shocks);
  m.text(names::kBriefShocks, "\xE2\x9A\xA0 " + vm.tr("brief.costsShock"));  // ⚠
}

// What the curriculum keeps locked in this campaign mission (option unlocks only where options
// trade), and the stage that unlocks it.
void locked_line(const AppViewModel& vm, const Snapshot& s, ScreenModel& m) {
  std::vector<std::string> locked;
  if (vm.shell().run_campaign) {
    const WebContent& content = web_content();
    const bool options = s.state["options"]["enabled"].as_bool(false);
    for (const auto& u : campaign_permissions(content, s.mission, vm.progress().assessed).locked) {
      if (!options && u.starts_with("options")) continue;
      const std::string what = vm.tr("unlock." + u, {{"cap", std::to_string(content.locked_leverage_cap)}});
      locked.push_back(vm.tr("unlock.after", {{"what", what}, {"stage", vm.tr("stage." + unlock_stage(content, u))}}));
    }
  }
  m.show(names::kBriefLocked, !locked.empty());
  m.text(names::kBriefLocked, "\xF0\x9F\x94\x92 " + vm.tr("campaign.locked", {{"what", join(locked, ", ")}}));  // 🔒
}

}  // namespace

ScreenModel briefing(const AppViewModel& vm) {
  ScreenModel m;
  for (auto& b : vm.bindings()) m.text(b.widget, std::move(b.text));
  const Snapshot* s = vm.snapshot();
  if (s == nullptr) return m;
  const json::Value& rules = s->state["missionRules"];
  const json::Value& c = s->state["contract"];
  m.text(names::kBriefTitle, vm.tr("brief.title") + std::string(kSep) + s->alias);
  m.text(names::kBriefMode, kv(vm, "brief.mode", mode_text(vm, *s)));
  m.text(names::kBriefLeverage, kv(vm, "brief.leverage", "1x \xE2\x80\x93 " + std::to_string(s->leverage_cap) + "x"));
  m.text(names::kBriefLimits,
         kv(vm, "brief.limits",
            vm.tr("brief.limitsValue", {{"liq", bp_pct(static_cast<int>(rules["liqWarningBp"].as_int()), vm.locale())},
                                        {"dd", bp_pct(static_cast<int>(rules["drawdownBudgetBp"].as_int()), vm.locale())}})));
  permissions(vm, rules, m);
  contract_lines(vm, c, m);
  margin_rule(vm, *s, rules, m);
  risk_limits(vm, rules, m);
  m.text(names::kBriefObjectivesTitle, vm.tr("brief.objectives"));
  WidgetLines objectives{names::kBriefObjectives, {}};
  for (auto& o : objective_texts(vm.catalog(), rules)) objectives.lines.push_back({"\xE2\x80\xA2 " + o, Tone::Normal});
  m.lines.push_back(std::move(objectives));
  locked_line(vm, *s, m);
  m.text(names::kBriefRules, vm.tr("brief.rules"));
  const std::string topic = "mission." + s->mission + ".topic";
  m.text(names::kBriefTopic, vm.catalog().has(topic) ? vm.tr(topic) : vm.tr("menu.practiceNote"));
  // The mission's tutorial can be replayed from here (web Briefing.svelte).
  m.show(names::kBriefTutorial, !game::tutorial_cards(vm.game().texts(), s->mission).empty());
  m.text(names::kBriefTutorial, vm.tr("brief.tutorial"));
  m.text(names::kBriefGo, "\xE2\x96\xB6 " + vm.tr("brief.go"));
  academy_briefing(vm, m);
  return m;
}

}  // namespace tradelab::desktop::screens
