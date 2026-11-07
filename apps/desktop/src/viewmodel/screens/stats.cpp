// Journal statistics (web JournalStats.svelte; add-run-sharing-and-journal-stats D7): the player's own
// runs that reached a debrief (summaries stored at the debrief), each statistic with the lesson it
// measures. Numbers from viewmodel/journal_stats (the port of journalStats.ts).
#include "viewmodel/format.hpp"
#include "viewmodel/journal_stats.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

std::string count_text(const AppViewModel& vm, const std::string& key, int n) {
  return vm.tr(n == 1 && vm.catalog().has(key + "One") ? key + "One" : key, {{"n", std::to_string(n)}});
}

std::string pct(const AppViewModel& vm, const std::optional<double>& v) { return v ? fmt::fmt_pct(*v, vm.locale(), 0) : "\xE2\x80\x94"; }
std::string r_text(const AppViewModel& vm, const std::optional<double>& v) {
  return v ? fmt::number(*v, vm.locale(), 2, true) + "R" : "\xE2\x80\x94";
}
std::string share(const AppViewModel& vm, int n, int d) { return d > 0 ? fmt::fmt_pct(static_cast<double>(n) / d, vm.locale(), 0) : "\xE2\x80\x94"; }
std::string lesson_line(const AppViewModel& vm, const std::string& id) {
  return "    \xF0\x9F\x8E\x93 " + vm.tr("stats.lesson", {{"title", vm.tr("academyLesson." + id + ".title")}});
}

void kv(std::vector<Line>& out, const std::string& label, const std::string& value, Tone tone = Tone::Normal) {
  out.push_back({"  " + label + ": " + value, tone});
}

void outcome(const AppViewModel& vm, const CoreStats& c, std::vector<Line>& out) {
  out.push_back({vm.tr("stats.outcome"), Tone::Title});
  kv(out, vm.tr("stats.runs"), std::to_string(c.runs));
  kv(out, vm.tr("stats.trades"), std::to_string(c.trades));
  kv(out, vm.tr("stats.winRate"), pct(vm, c.win_rate));
  kv(out, vm.tr("stats.expectancy"), r_text(vm, c.expectancy_r));
  kv(out, vm.tr("stats.avgWin"), r_text(vm, c.avg_win_r));
  kv(out, vm.tr("stats.avgLoss"), r_text(vm, c.avg_loss_r));
  kv(out, vm.tr("stats.withoutR"), std::to_string(c.without_r));
  constexpr std::array<std::string_view, kRBuckets> kNames = {"le-2", "-2..-1", "-1..0", "0..1", "1..2", "gt2"};
  std::string dist = vm.tr("stats.distribution") + ":";
  for (int i = 0; i < kRBuckets; ++i) {
    dist += "  " + vm.tr("stats.bucket." + std::string(kNames[static_cast<std::size_t>(i)])) + " " + std::to_string(c.r_buckets[static_cast<std::size_t>(i)]);
  }
  out.push_back({"  " + dist, Tone::Normal});
  out.push_back({"  " + vm.tr("stats.outcomeLesson"), Tone::Muted});
  out.push_back({lesson_line(vm, "risk-expectancy"), Tone::Info});
}

void stops_and_risk(const AppViewModel& vm, const CoreStats& c, std::vector<Line>& out) {
  out.push_back({vm.tr("stats.stops"), Tone::Title});
  kv(out, vm.tr("stats.respected"), share(vm, c.stops_respected, c.stops_judged));
  kv(out, vm.tr("stats.movedAway"), share(vm, c.moved_away, c.stops_reported), c.moved_away > 0 ? Tone::Down : Tone::Normal);
  out.push_back({"  " + vm.tr("stats.stopsLesson"), Tone::Muted});
  out.push_back({lesson_line(vm, "mind-plan"), Tone::Info});
  out.push_back({vm.tr("stats.risk"), Tone::Title});
  kv(out, vm.tr("stats.avgRisk"), c.avg_risk_pct ? fmt::number(*c.avg_risk_pct, vm.locale(), 2) + "%" : "\xE2\x80\x94");
  kv(out, vm.tr("stats.below1"), share(vm, c.risk_below1, c.risk_trades));
  kv(out, vm.tr("stats.within"), share(vm, c.risk_1to2, c.risk_trades), Tone::Up);
  kv(out, vm.tr("stats.above2"), share(vm, c.risk_above2, c.risk_trades), c.risk_above2 > 0 ? Tone::Down : Tone::Normal);
  kv(out, vm.tr("stats.oversizedRuns"), std::to_string(c.oversized_runs));
  out.push_back({"  " + vm.tr("stats.riskLesson"), Tone::Muted});
  out.push_back({lesson_line(vm, "risk-sizing"), Tone::Info});
  out.push_back({vm.tr("stats.liquidations"), Tone::Title});
  kv(out, vm.tr("stats.liq"), std::to_string(c.liquidations), c.liquidations > 0 ? Tone::Down : Tone::Normal);
  kv(out, vm.tr("stats.marginCalls"), std::to_string(c.margin_calls), c.margin_calls > 0 ? Tone::Down : Tone::Normal);
  out.push_back({"  " + vm.tr("stats.liqLesson"), Tone::Muted});
  out.push_back({lesson_line(vm, "futures-margin"), Tone::Info});
}

void mistake_lines(const AppViewModel& vm, const std::vector<MistakeRow>& rows, std::vector<Line>& out) {
  out.push_back({vm.tr("stats.mistakes"), Tone::Title});
  if (rows.empty()) out.push_back({"  " + vm.tr("stats.mistakesNone"), Tone::Muted});
  for (const MistakeRow& m : rows) {
    std::string text = "  " + vm.tr("statsFinding." + m.code) + " \xE2\x80\x94 " + count_text(vm, "stats.mistakeRuns", m.runs);
    if (m.repeats) text += " [" + vm.tr("stats.repeats") + "]";
    out.push_back({text, m.tone == "bad" ? Tone::Down : Tone::Attention});
    if (m.lesson) out.push_back({lesson_line(vm, *m.lesson), Tone::Info});
  }
}

void group_lines(const AppViewModel& vm, const std::string& title, const std::vector<GroupRow>& rows, bool stages, std::vector<Line>& out) {
  out.push_back({title, Tone::Title});
  for (const GroupRow& g : rows) {
    const std::string name = !stages ? mission_title(vm, g.key) : (g.key == kPracticeStage ? vm.tr("stats.practice") : vm.tr("stage." + g.key));
    out.push_back({"  " + name + std::string(kSep) + vm.tr("stats.col.runs") + " " + std::to_string(g.runs) + std::string(kSep) +
                       vm.tr("stats.col.trades") + " " + std::to_string(g.trades) + std::string(kSep) + vm.tr("stats.col.winRate") + " " +
                       pct(vm, g.win_rate) + std::string(kSep) + vm.tr("stats.col.expectancy") + " " + r_text(vm, g.expectancy_r),
                   Tone::Normal});
  }
}

void trend_lines(const AppViewModel& vm, const Trend& t, std::vector<Line>& out) {
  out.push_back({vm.tr("stats.trend"), Tone::Title});
  if (!t.previous) {
    out.push_back({"  " + vm.tr("stats.trendFew"), Tone::Muted});
    return;
  }
  out.push_back({"  " + vm.tr("stats.trendNote", {{"n", std::to_string(kTrendWindow)}}), Tone::Muted});
  const auto row = [&](const std::string& label, const CoreStats& c) {
    out.push_back({"  " + label + std::string(kSep) + vm.tr("stats.col.expectancy") + " " + r_text(vm, c.expectancy_r) + std::string(kSep) +
                       vm.tr("stats.col.respected") + " " + pct(vm, c.respected_rate),
                   Tone::Normal});
  };
  row(vm.tr("stats.recent"), *t.recent);
  row(vm.tr("stats.previous"), *t.previous);
}

}  // namespace

std::vector<StatsRun> stats_runs(const Progress& p) {
  std::vector<StatsRun> out;
  for (const RunRecord& r : p.runs) {  // oldest first, as the statistics count them
    if (r.assisted || r.summary.empty()) continue;
    const auto v = json::parse(r.summary);
    const auto s = v ? summary_from_json(*v) : std::nullopt;
    if (s) out.push_back({r.run_id, r.mission_id, r.created_at, *s});
  }
  return out;
}

ScreenModel stats(const AppViewModel& vm) {
  ScreenModel m;
  m.text(names::kStatsTitle, vm.tr("stats.title"));
  m.text(names::kStatsBack, vm.tr("debrief.menu"));
  m.text(names::kStatsNote, vm.tr("stats.note"));
  const auto runs = stats_runs(vm.progress());
  stats_run_files(vm, m);
  m.show(names::kStatsEmpty, runs.empty());
  m.text(names::kStatsEmpty, vm.tr("stats.empty"));
  WidgetLines lines{names::kStatsLines, {}};
  if (!runs.empty()) {
    const JournalStats s = journal_stats(web_content(), runs);
    if (s.too_few) lines.lines.push_back({"\xE2\x9A\xA0 " + count_text(vm, "stats.tooFew", s.core.trades), Tone::Attention});
    outcome(vm, s.core, lines.lines);
    stops_and_risk(vm, s.core, lines.lines);
    mistake_lines(vm, s.mistakes, lines.lines);
    group_lines(vm, vm.tr("stats.byMission"), s.by_mission, false, lines.lines);
    group_lines(vm, vm.tr("stats.byStage"), s.by_stage, true, lines.lines);
    trend_lines(vm, s.trend, lines.lines);
  }
  m.lines.push_back(std::move(lines));
  return m;
}

}  // namespace tradelab::desktop::screens
