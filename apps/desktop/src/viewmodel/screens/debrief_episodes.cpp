// Debrief views of scoring v14 (web Debrief.svelte's objective readings and "see bar N" links, and
// EpisodesPanel.svelte; spec gameplay-loop "Debrief views of biases and episodes"): the m22-m25
// objectives read from the engine's facts, the bars its findings and episodes point to (selecting one
// marks it on the run chart) and each perpetual position from flat to flat. All engine numbers.
#include <algorithm>

#include "model/content.hpp"
#include "viewmodel/academy/glossary.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/objective_checks.hpp"
#include "viewmodel/report_texts.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

const json::Value* debrief_of(const AppViewModel& vm) {
  const Snapshot* s = vm.snapshot();
  return s != nullptr && s->debrief ? &s->debrief->raw : nullptr;
}

double initial_of(const AppViewModel& vm) { return fmt::num(vm.snapshot()->initial_balance); }

std::string objective_of(const AppViewModel& vm, const json::Value& d, const std::string& code) {
  for (const json::Value& o : d["missionRules"]["objectives"].items()) {
    if (o["code"].as_string() == code) return objective_text(vm.catalog(), o);
  }
  return code;
}

// The finding's name: its glossary term (glossary.ts FINDING_TERMS), else its code.
std::string finding_name(const AppViewModel& vm, const std::string& code) {
  const auto term = academy::finding_term(web_content(), code);
  const std::string key = term ? "glossary." + *term + ".term" : std::string();
  return term && vm.catalog().has(key) ? vm.tr(key) : code;
}

std::string show_moment(const AppViewModel& vm, int step) { return vm.tr("debrief.showMoment", {{"step", std::to_string(step)}}); }

void objective_lines(const AppViewModel& vm, const json::Value& d, ScreenModel& m) {
  const auto readings = objective_readings(d, initial_of(vm));
  m.show(names::kDebriefObjectives, !readings.empty());
  WidgetLines out{names::kDebriefObjectives, {{vm.tr("debrief.objectives"), Tone::Title}}};
  for (const auto& r : readings) {
    const std::string verdict = vm.tr(r.met ? "debrief.objectiveMet" : "debrief.objectiveMissed");
    out.lines.push_back({std::string(r.met ? "\xE2\x9C\x94 " : "\xE2\x9A\xA0 ") + objective_of(vm, d, r.code) + std::string(kSep) + verdict,
                         r.met ? Tone::Up : Tone::Attention});
  }
  m.lines.push_back(std::move(out));
}

void moments_list(const AppViewModel& vm, ScreenModel& m) {
  const auto moments = debrief_moments(vm);
  m.show(names::kDebriefMomentsBox, !moments.empty());
  m.text(names::kDebriefMomentsTitle, vm.tr("desktop.debrief.moments"));
  WidgetItems items{names::kDebriefMoments, {}, -1};
  for (std::size_t i = 0; i < moments.size(); ++i) {
    items.items.push_back(moments[i].label);
    if (vm.debrief_moment() == moments[i].step && items.selected < 0) items.selected = static_cast<int>(i);
  }
  m.items.push_back(std::move(items));
  m.name(names::kDebriefMoments, vm.tr("desktop.debrief.moments"));
}

std::vector<std::string> episode_columns(const AppViewModel& vm) {
  return {"#", vm.tr("trade.side"), vm.tr("episodes.entries"), vm.tr("episodes.exits"), vm.tr("episodes.stops"), "R", "MFE \xC2\xB7 MAE"};
}

void episodes_panel(const AppViewModel& vm, const json::Value& d, ScreenModel& m) {
  const auto& episodes = d["episodes"].items();
  m.show(names::kDebriefEpisodesPanel, !episodes.empty());
  if (episodes.empty()) return;
  m.text(names::kDebriefEpisodesTitle, vm.tr("episodes.title"));
  WidgetTable t{names::kDebriefEpisodes, episode_columns(vm), {}, {0.4f, 1, 2.6f, 2.6f, 2.2f, 1.6f, 1.4f}};
  for (const json::Value& e : episodes) t.rows.push_back(episode_cells(vm.catalog(), e));
  m.tables.push_back(std::move(t));
  m.name(names::kDebriefEpisodes, vm.tr("episodes.title"));
  m.text(names::kDebriefEpisodesNote, vm.tr("episodes.note"));
}

}  // namespace

std::vector<DebriefMoment> debrief_moments(const AppViewModel& vm) {
  std::vector<DebriefMoment> out;
  const json::Value* d = debrief_of(vm);
  if (d == nullptr) return out;
  for (const json::Value& f : (*d)["findings"].items()) {
    if (const auto at = finding_moment(f)) out.push_back({*at, show_moment(vm, *at) + std::string(kSep) + finding_name(vm, f["code"].as_string())});
  }
  for (const auto& r : objective_readings(*d, initial_of(vm))) {
    if (r.step) out.push_back({*r.step, show_moment(vm, *r.step) + std::string(kSep) + objective_of(vm, *d, r.code)});
  }
  for (const json::Value& e : (*d)["episodes"].items()) {
    const int step = static_cast<int>(e["entryStep"].as_int());
    out.push_back({step, "#" + e["n"].as_string() + std::string(kSep) + vm.tr("episodes.show", {{"step", std::to_string(step)}})});
  }
  return out;
}

void debrief_episodes(const AppViewModel& vm, ScreenModel& m) {
  const json::Value* d = debrief_of(vm);
  if (d == nullptr) {
    for (const auto w : {names::kDebriefObjectives, names::kDebriefMomentsBox, names::kDebriefEpisodesPanel}) m.show(w, false);
    return;
  }
  objective_lines(vm, *d, m);
  moments_list(vm, m);
  episodes_panel(vm, *d, m);
}

}  // namespace tradelab::desktop::screens
