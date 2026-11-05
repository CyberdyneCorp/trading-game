// Menu (web MissionMenu.svelte + CampaignPanel.svelte): resume line, screens, campaign, missions with
// their Completed mark, disclaimer.
#include "model/content.hpp"
#include "viewmodel/campaign.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::screens {

namespace {

constexpr std::string_view kPlay = "\xE2\x96\xB6 ";  // "▶ "
constexpr std::string_view kCheck = "\xE2\x9C\x93";  // ✓

void resume_line(const AppViewModel& vm, ScreenModel& m) {
  m.show(names::kMenuResumeRow, vm.resumable());
  if (!vm.resumable()) return;
  const ShellState& s = vm.shell();
  const std::string tag = s.run_campaign ? "[" + vm.tr("journal.mode.campaign") + "] " : std::string();
  m.text(names::kMenuResumeText,
         tag + mission_title(vm, s.run_mission) + std::string(kSep) + "seed " + std::to_string(vm.snapshot()->seed));
  m.text(names::kMenuResume, std::string(kPlay) + vm.tr("menu.resume"));
}

// CampaignPanel.svelte mark(): ✓ done, ▶ current, ☠ where it went bankrupt, · otherwise.
std::string_view campaign_mark(const std::optional<CampaignRun>& c, int i) {
  if (!c) return "\xC2\xB7";
  if (i < c->index || (c->status == CampaignStatus::Complete && i <= c->index)) return kCheck;
  if (i == c->index && c->status == CampaignStatus::Active) return "\xE2\x96\xB6";
  if (i == c->index && c->status == CampaignStatus::Bankrupt) return "\xE2\x98\xA0";
  return "\xC2\xB7";
}

std::string ended_text(const AppViewModel& vm, const CampaignRun& c) {
  const WebContent& content = web_content();
  if (c.status == CampaignStatus::Complete) return vm.tr("campaign.ended.complete", {{"capital", fmt::fmt_money(c.capital, vm.locale())}});
  const std::string capital = fmt::fmt_money(content.campaign_capital, vm.locale());
  if (c.status == CampaignStatus::Bankrupt) {
    return vm.tr("campaign.ended.bankrupt", {{"n", std::to_string(c.index + 1)}, {"capital", capital}});
  }
  return vm.tr("campaign.ended.abandoned", {{"capital", capital}});
}

void campaign_buttons(const AppViewModel& vm, ScreenModel& m, bool running) {
  const ShellState& s = vm.shell();
  const bool confirming = running && s.confirming_end;
  m.show(names::kCampaignContinue, running && !confirming);
  m.show(names::kCampaignEnd, running && !confirming);
  m.show(names::kCampaignConfirmRow, confirming);
  m.show(names::kCampaignStart, !running);
  m.text(names::kCampaignContinue, std::string(kPlay) + vm.tr("campaign.continue"));
  m.text(names::kCampaignEnd, vm.tr("campaign.end"));
  m.text(names::kCampaignEndYes, vm.tr("campaign.endYes"));
  m.text(names::kCampaignEndNo, vm.tr("campaign.endNo"));
  m.text(names::kCampaignStart, std::string(kPlay) + vm.tr(s.progress.campaign ? "campaign.new" : "campaign.start"));
  for (auto w : {names::kCampaignContinue, names::kCampaignEnd, names::kCampaignStart, names::kCampaignEndYes}) {
    m.enable(w, !vm.waiting());
  }
}

void campaign_panel(const AppViewModel& vm, ScreenModel& m) {
  const WebContent& content = web_content();
  const Progress& p = vm.progress();
  const Ids missions = campaign_missions(content);
  const std::string next = next_campaign_mission(content, p.campaign);
  m.text(names::kCampaignTitle, vm.tr("campaign.title"));
  m.text(names::kCampaignNote, vm.tr("campaign.note"));
  WidgetLines steps{names::kCampaignSteps, {}};
  for (int i = 0; i < static_cast<int>(missions.size()); ++i) {
    const std::string_view mark = campaign_mark(p.campaign, i);
    const Tone tone = mark == kCheck ? Tone::Up : mark == "\xE2\x96\xB6" ? Tone::Accent : Tone::Muted;
    steps.lines.push_back({std::string(mark) + " " + mission_title(vm, missions[static_cast<std::size_t>(i)]), tone});
  }
  m.lines.push_back(std::move(steps));
  const bool running = p.campaign && !next.empty();
  m.show(names::kCampaignProgress, running);
  m.show(names::kCampaignEnded, !running && p.campaign.has_value());
  if (running) {
    m.text(names::kCampaignProgress,
           vm.tr("campaign.progress", {{"n", std::to_string(p.campaign->index + 1)},
                                       {"total", std::to_string(missions.size())},
                                       {"mission", mission_title(vm, next)},
                                       {"capital", fmt::fmt_money(p.campaign->capital, vm.locale())}}));
    m.text(names::kCampaignConfirm, vm.tr("campaign.endConfirm", {{"capital", fmt::fmt_money(p.campaign->capital, vm.locale())}}));
  } else if (p.campaign) {
    m.text(names::kCampaignEnded, ended_text(vm, *p.campaign));
  }
  campaign_buttons(vm, m, running);
  const std::string best = best_completed_capital(p.campaign_history);
  std::string played = p.campaign_history.empty() ? std::string()
                                                  : vm.tr("campaign.played", {{"n", std::to_string(p.campaign_history.size())}});
  if (!best.empty()) played += std::string(kSep) + vm.tr("campaign.best", {{"capital", fmt::fmt_money(best, vm.locale())}});
  m.text(names::kCampaignPlayed, played);
  m.show(names::kCampaignPlayed, !played.empty());
}

void mission_list(const AppViewModel& vm, ScreenModel& m) {
  WidgetItems items{names::kMenuList, {}, vm.selected()};
  const auto& completed = vm.progress().completed;
  for (const auto& mission : vm.missions()) {
    const bool done = contains(completed, mission.id);
    items.items.push_back(mission_title(vm, mission.id) + (done ? "  " + std::string(kCheck) + " " + vm.tr("menu.completed") : ""));
  }
  m.items.push_back(std::move(items));
  m.name(names::kMenuList, vm.tr("menu.missions"));
  if (vm.missions().empty()) return;
  const std::string& id = vm.missions()[static_cast<std::size_t>(vm.selected())].id;
  const bool done = contains(completed, id);
  m.show(names::kMenuDetailDone, done);
  m.text(names::kMenuDetailDone, std::string(kCheck) + " " + vm.tr("menu.completed"));
  // Twelve identical "Start mission" buttons on the web: the name says which one.
  m.name(names::kMenuStart, vm.tr("menu.start") + ": " + mission_title(vm, id));
}

}  // namespace

ScreenModel menu(const AppViewModel& vm) {
  ScreenModel m;
  for (auto& b : vm.bindings()) m.text(b.widget, std::move(b.text));
  resume_line(vm, m);
  m.text(names::kMenuCurriculum, "\xE2\x96\xA6 " + vm.tr("curriculum.open"));  // ▦
  m.text(names::kMenuJournal, "\xE2\x9C\x8E " + vm.tr("journal.open"));        // ✎
  m.text(names::kMenuSettings, "\xE2\x9A\x99 " + vm.tr("menu.settings"));      // ⚙
  m.name(names::kMenuLanguage, vm.tr("settings.language") + ": " + vm.tr("desktop.language"));
  m.enable(names::kMenuStart, !vm.waiting());
  m.enable(names::kMenuResume, !vm.waiting());
  campaign_panel(vm, m);
  mission_list(vm, m);
  return m;
}

}  // namespace tradelab::desktop::screens
