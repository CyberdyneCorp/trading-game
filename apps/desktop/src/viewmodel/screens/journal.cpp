// Journal (web Journal.svelte + JournalEntries.svelte): the plans recorded before each entry, run by
// run, newest first, filterable by mission. The free text stays on this device and is never scored.
#include <cmath>
#include <cstdio>
#include <ctime>

#include "viewmodel/format.hpp"
#include "viewmodel/journal.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

// toLocaleString(locale) with the local time zone: "05/11/2026, 16:45" (PT-BR), "11/5/2026, 4:45 PM" (en).
std::string local_date(std::int64_t ms, Locale L) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[40];
  if (L == Locale::PtBR) {
    std::strftime(buf, sizeof buf, "%d/%m/%Y, %H:%M", &tm);
    return buf;
  }
  const int hour = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
  std::snprintf(buf, sizeof buf, "%d/%d/%d, %d:%02d %s", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, hour, tm.tm_min,
                tm.tm_hour < 12 ? "AM" : "PM");
  return buf;
}

std::string level(const AppViewModel& vm, const std::optional<double>& v) {
  if (!v || !std::isfinite(*v) || *v <= 0) return {};
  return fmt::fmt_price(fmt::number(*v, Locale::En, 8), vm.locale());
}

std::string details(const AppViewModel& vm, const PlanEntry& p) {
  std::string out = vm.tr("journal.conf", {{"v", std::to_string(p.confidence)}});
  const auto add = [&](std::string_view key, const std::string& v) {
    if (!v.empty()) out += std::string(kSep) + vm.tr(key, {{"v", v}});
  };
  add("journal.inv", level(vm, p.invalidation));
  add("journal.target", level(vm, p.target));
  if (p.planned_risk && *p.planned_risk > 0) add("journal.risk", fmt::fmt_money(*p.planned_risk, vm.locale()));
  return out;
}

void entry_lines(const AppViewModel& vm, const PlanEntry& p, std::vector<Line>& out) {
  const std::string side = p.buy ? "\xE2\x96\xB2 LONG" : "\xE2\x96\xBC SHORT";
  std::string head = "[" + vm.tr("journal.bar", {{"step", std::to_string(p.step)}}) + "] " + side +
                     (p.instrument.empty() ? "" : std::string(kSep) + p.instrument);
  if (p.skipped) {
    out.push_back({head + " \xE2\x80\x94 " + vm.tr("journal.skipped"), Tone::Muted});
    return;
  }
  out.push_back({head + "  " + p.thesis, p.buy ? Tone::Up : Tone::Down});
  out.push_back({"    " + details(vm, p), Tone::Muted});
  if (!p.lose_scenario.empty()) out.push_back({"    " + vm.tr("journal.lose", {{"v", p.lose_scenario}}), Tone::Muted});
}

std::string run_header(const AppViewModel& vm, const JournalRun& run) {
  std::string head = mission_title(vm, run.mission_id) + "  [" + vm.tr("journal.mode." + std::string(run_mode_name(run.mode))) + "]";
  if (run.intel) head += " [" + vm.tr("journal.intel") + "]";
  if (run.assisted) head += " [" + vm.tr("journal.assisted") + "]";
  return head + std::string(kSep) + local_date(run.created_at, vm.locale()) + std::string(kSep) + "seed " +
         std::to_string(run.seed) + std::string(kSep) + vm.tr("journal.status." + std::string(run_status_name(run.status)));
}

}  // namespace

ScreenModel journal(const AppViewModel& vm) {
  ScreenModel m;
  const auto& records = vm.progress().runs;
  const auto missions = journal_missions(records);
  const int filter = vm.shell().journal_filter;
  const std::string mission = filter > 0 && filter <= static_cast<int>(missions.size()) ? missions[static_cast<std::size_t>(filter - 1)] : "";
  m.text(names::kJournalTitle, vm.tr("journal.title"));
  m.text(names::kJournalBack, vm.tr("debrief.menu"));
  m.text(names::kJournalNote, vm.tr("journal.note"));
  m.text(names::kJournalFilterLabel, vm.tr("journal.filter"));
  m.show(names::kJournalFilterRow, missions.size() > 1);
  WidgetItems filter_items{names::kJournalFilter, {vm.tr("journal.all")}, filter};
  for (const auto& id : missions) filter_items.items.push_back(mission_title(vm, id));
  m.items.push_back(std::move(filter_items));
  m.name(names::kJournalFilter, vm.tr("journal.filter"));
  const auto runs = journal_runs(records, mission);
  m.show(names::kJournalEmpty, runs.empty());
  m.text(names::kJournalEmpty, vm.tr("journal.empty"));
  WidgetLines lines{names::kJournalRuns, {}};
  for (const auto& run : runs) {
    lines.lines.push_back({run_header(vm, run), Tone::Title});
    for (const auto& p : run.entries) entry_lines(vm, p, lines.lines);
  }
  m.lines.push_back(std::move(lines));
  journal_run_files(vm, m);
  return m;
}

}  // namespace tradelab::desktop::screens
