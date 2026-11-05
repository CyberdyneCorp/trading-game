// Debrief tables: per-trade R table with the stop/liquidation advice, calibration, advisor reads,
// signals by revealed regime (RegimeReveal.svelte), releases (ReleaseReveal.svelte) and the timeline.
#include <algorithm>
#include <cmath>

#include "model/content.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/report_texts.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

constexpr std::string_view kDash = "\xE2\x80\x94";

std::string str(long v) { return std::to_string(v); }
long as_long(const json::Value& v, long fallback = 0) { return static_cast<long>(v.as_int(fallback)); }

std::string plan_cell(const AppViewModel& vm, const json::Value& respected) {
  if (respected.is_null()) return std::string(kDash);
  return respected.as_bool() ? "\xE2\x9C\x94 " + vm.tr("trade.respected") : "\xE2\x9C\x96 " + vm.tr("trade.violated");
}

std::vector<std::string> trade_row(const AppViewModel& vm, const json::Value& tr, const json::Value& spans) {
  const Locale L = vm.locale();
  const double net = fmt::num(tr["net"].as_string());
  const std::string r = tr["rBp"].is_null() ? std::string(kDash) : fmt::fmt_num(tr["rBp"].as_double() / 10000.0, L, 2, true) + "R";
  std::vector<std::string> row = {
      tr["n"].as_string(),
      trade_label(vm.catalog(), tr) + " \xC3\x97 " + fmt::fmt_qty(tr["qty"].as_string(), L),
      fmt::fmt_price(tr["entryAvg"].as_string(), L) + " @" + tr["entryStep"].as_string(),
      tr["exitStep"].is_null() ? std::string(kDash) : fmt::fmt_price(tr["exitAvg"].as_string(), L),
      std::string(fmt::sign_mark(net)) + " " + fmt::fmt_money(net, L, true),
      r,
      tr["bars"].as_string(),
      vm.tr("trade." + tr["reason"].as_string()),
      plan_cell(vm, tr["planRespected"])};
  if (!spans.is_null()) row.push_back(entry_regime_name(vm.catalog(), tr, spans));
  return row;
}

std::vector<std::string> trade_columns(const AppViewModel& vm, bool regimes) {
  std::vector<std::string> cols = {"#",          vm.tr("trade.side"), vm.tr("trade.entry"), vm.tr("trade.exit"), vm.tr("trade.net"),
                                   "R",          vm.tr("trade.bars"), vm.tr("trade.reason"), vm.tr("trade.plan")};
  if (regimes) cols.push_back(vm.tr("trade.regime"));
  return cols;
}

void calibration_line(const AppViewModel& vm, const json::Value& d, ScreenModel& m) {
  const json::Value& score = d["score"]["dims"]["calibration"]["score"];
  std::string line = vm.tr("debrief.calibrationTitle") + ": ";
  if (score.is_null()) {
    line += vm.tr("debrief.calibrationNa");
  } else {
    const double brier = d["calibration"]["brierBp"].as_double(0) / 10000.0;
    line += tr_count(vm.catalog(), "debrief.calibrationLine", as_long(d["calibration"]["resolved"]),
                     {{"brier", fmt::fmt_num(brier, vm.locale(), 3)}});
  }
  m.text(names::kDebriefCalibration, line);
}

Tone stance_tone(const std::string& stance) {
  if (stance == "long") return Tone::Up;
  return stance == "short" ? Tone::Down : Tone::Attention;
}

Tone outcome_tone(const std::string& outcome) {
  if (outcome == "right") return Tone::Up;
  return outcome == "wrong" ? Tone::Down : Tone::Muted;
}

// Advisor reads (synthetic markets only): stance, confidence and outcome per read, then the points.
void advisor_panel(const AppViewModel& vm, const Snapshot& s, const json::Value& d, ScreenModel& m) {
  const bool shown = !s.state["knownScenario"].as_bool(false);
  m.show(names::kDebriefAdvisorPanel, shown);
  if (!shown) return;
  WidgetLines out{names::kDebriefAdvisor, {{vm.tr("debrief.advisor"), Tone::Title}}};
  const auto& advice = d["advice"].items();
  if (advice.empty()) out.lines.push_back({vm.tr("debrief.adviceNone"), Tone::Muted});
  long judged = 0;
  for (const auto& a : advice) {
    const std::string outcome = a["outcome"].as_string();
    judged += outcome == "right" || outcome == "wrong" ? 1 : 0;
    out.lines.push_back({"[" + a["step"].as_string() + "] " + vm.tr("adv.stance." + a["stance"].as_string()) + " \xC2\xB7 " +
                             str(std::lround(a["confidenceBp"].as_double() / 100.0)) + "% \xC2\xB7 " +
                             vm.tr("debrief.outcome." + outcome),
                         outcome_tone(outcome) == Tone::Muted ? stance_tone(a["stance"].as_string()) : outcome_tone(outcome)});
  }
  if (!d["adviceHitBp"].is_null()) {
    out.lines.push_back({vm.tr("debrief.adviceHit", {{"pct", str(std::lround(d["adviceHitBp"].as_double() / 100.0))}, {"n", str(judged)}}),
                         Tone::Muted});
  }
  const long used = static_cast<long>(s.state["advisor"]["advice"].size());
  out.lines.push_back({vm.tr("debrief.points", {{"used", str(used)}, {"earned", d["advisorPointsEarned"].as_string("0")}, {"balance", kDash.data()}})});
  out.lines.push_back({vm.tr("debrief.pointsHow"), Tone::Muted});
  m.lines.push_back(std::move(out));
}

void regime_tables(const AppViewModel& vm, const Snapshot& s, const json::Value& d, const json::Value& spans, ScreenModel& m) {
  m.show(names::kDebriefRegimesPanel, !spans.is_null());
  if (spans.is_null()) return;
  std::string band;
  for (const auto& piece : regime_band(spans, s.warmup_bars, s.step)) {
    band += (band.empty() ? "" : ", ") + regime_name(vm.catalog(), piece.label, piece.direction) + " " +
            str(piece.start) + "\xE2\x80\x93" + str(piece.start + piece.bars - 1);
  }
  m.name(names::kDebriefRegimeBand, vm.tr("debrief.regimeBand", {{"from", str(s.warmup_bars)}, {"to", str(s.step)}}) + ": " + band);
  WidgetTable t{names::kDebriefRegimes, {vm.tr("regime.col.regime"), vm.tr("regime.col.bars")}, {}};
  static constexpr std::array<std::string_view, 4> kSignals = {"rsiFade", "bollingerFade", "macdFollow", "smaFollow"};
  for (auto k : kSignals) t.columns.push_back(vm.tr("regime.col." + std::string(k)));
  for (const auto& r : d["regimes"].items()) {
    if (std::ranges::none_of(kSignals, [&](std::string_view k) { return r[k]["samples"].as_int() > 0; })) continue;
    std::vector<std::string> row = {regime_name(vm.catalog(), r["label"].as_string(), static_cast<int>(r["direction"].as_int())) + " @" +
                                        r["start"].as_string(),
                                    r["bars"].as_string()};
    for (auto k : kSignals) row.push_back(fmt_tally(vm.catalog(), r[k]));
    t.rows.push_back(std::move(row));
  }
  m.tables.push_back(std::move(t));
  m.text(names::kDebriefRegimesNote, vm.tr("debrief.regimesTitle") + " \xC2\xB7 " + vm.tr("debrief.regimesNote"));
}

std::string reaction_text(const AppViewModel& vm, long r) {
  if (r > 0) return vm.tr("release.reaction.up");
  return vm.tr(r < 0 ? "release.reaction.down" : "release.reaction.none");
}

std::vector<std::string> release_cells(const AppViewModel& vm, const ReleaseRow& r) {
  const Locale L = vm.locale();
  const json::Value& e = r.event;
  const long lead = as_long(e["announceLead"]);
  const std::string when = lead > 0 ? tr_count(vm.catalog(), "release.announced", lead) : vm.tr("release.unannounced");
  const auto level = [&](long v) { return vm.tr("newsLevel." + str(std::clamp(v + 2, 0L, 4L))); };
  return {e["step"].as_string(),
          vm.tr("newsKind." + e["kind"].as_string()) + " \xC2\xB7 " + when,
          level(as_long(e["expectation"])),
          level(as_long(e["actual"])),
          fmt::fmt_num(r.weighted, L, 2, true),
          e["intensityPct"].as_string() + "%",
          fmt_bp(e["gapBp"].as_double(), L),
          reaction_text(vm, as_long(e["reaction"])),
          vm.tr("release.you." + std::string(release_position_name(r.you)))};
}

void release_table(const AppViewModel& vm, const Snapshot& s, const json::Value& d, const json::Value& reveal, ScreenModel& m) {
  const auto rows = release_rows(reveal["events"], d["trades"], s.step, web_content().after_release_bars);
  m.show(names::kDebriefReleasesPanel, !rows.empty());
  WidgetTable t{names::kDebriefReleases, {}, {}};
  for (auto k : {"bar", "event", "consensus", "actual", "weighted", "size", "gap", "reaction", "you"}) {
    t.columns.push_back(vm.tr("release.col." + std::string(k)));
  }
  std::string implied;
  for (const auto& r : rows) {
    t.rows.push_back(release_cells(vm, r));
    if (r.event["impliedBp"].as_int(-1) < 0) continue;
    std::string pct = fmt_bp(r.event["impliedBp"].as_double(), vm.locale());
    if (!pct.empty() && pct[0] == '+') pct.erase(0, 1);
    implied += vm.tr("release.implied", {{"step", r.event["step"].as_string()}, {"implied", pct},
                                         {"realized", fmt_bp(r.event["moveBp"].as_double(), vm.locale())}}) + "\n";
  }
  m.tables.push_back(std::move(t));
  m.text(names::kDebriefReleasesNote, implied + vm.tr("debrief.releasesNote"));
}

std::string plan_level(const AppViewModel& vm, const std::optional<double>& v) {
  return v && std::isfinite(*v) ? fmt::fmt_price(fmt::number(*v, Locale::En, 8), vm.locale()) : std::string(kDash);
}

// The timeline: trades opened and closed (engine trade rows), the plans of this run's journal and the
// released headlines, by bar.
using TimelineRows = std::vector<std::pair<long, Line>>;

void trade_rows(const AppViewModel& vm, const json::Value& d, TimelineRows& rows) {
  for (const auto& tr : d["trades"].items()) {
    rows.push_back({as_long(tr["entryStep"]), {trade_label(vm.catalog(), tr) + " \xC3\x97 " + fmt::fmt_qty(tr["qty"].as_string(), vm.locale()) +
                                                    " @ " + fmt::fmt_price(tr["entryAvg"].as_string(), vm.locale()),
                                                tr["side"].as_string() == "long" ? Tone::Up : Tone::Down}});
    if (tr["exitStep"].is_null()) continue;
    rows.push_back({as_long(tr["exitStep"]), {"\xE2\x96\xA0 " + vm.tr("trade." + tr["reason"].as_string()) + " @ " +
                                                  fmt::fmt_price(tr["exitAvg"].as_string(), vm.locale()) + " \xC2\xB7 " +
                                                  fmt::fmt_money(tr["net"].as_string(), vm.locale(), true),
                                              Tone::Info}});
  }
}

void plan_rows(const AppViewModel& vm, TimelineRows& rows) {
  for (const auto& r : vm.progress().runs) {
    if (r.run_id != vm.shell().run_id) continue;
    for (const auto& p : r.journal) {
      const std::string text = p.skipped ? vm.tr("debrief.noPlan")
                                         : vm.tr("debrief.planLine", {{"thesis", p.thesis}, {"inv", plan_level(vm, p.invalidation)},
                                                                      {"target", plan_level(vm, p.target)}, {"conf", str(p.confidence)}});
      rows.push_back({p.step, {"\xE2\x9C\x8E " + text, Tone::Info}});
    }
  }
}

void news_rows(const AppViewModel& vm, const Snapshot& s, TimelineRows& rows) {
  for (const auto& n : s.state["news"].items()) {
    if (n["early"].as_bool(false)) continue;
    const long tone = as_long(n["tone"]);
    rows.push_back({as_long(n["step"]), {"\xF0\x9F\x93\xB0 " + vm.tr("newsKind." + n["kind"].as_string()),
                                         tone > 0 ? Tone::Up : tone < 0 ? Tone::Down : Tone::Info}});
  }
}

void timeline(const AppViewModel& vm, const json::Value& d, const Snapshot& s, ScreenModel& m) {
  TimelineRows rows;
  trade_rows(vm, d, rows);
  plan_rows(vm, rows);
  news_rows(vm, s, rows);
  std::ranges::stable_sort(rows, [](const auto& a, const auto& b) { return a.first < b.first; });
  WidgetLines out{names::kDebriefTimeline, {{vm.tr("debrief.timeline"), Tone::Title}}};
  for (auto& [step, line] : rows) out.lines.push_back({"[" + str(step) + "] " + line.text, line.tone});
  m.lines.push_back(std::move(out));
}

}  // namespace

void debrief_trades(const AppViewModel& vm, const Snapshot& s, const json::Value& d, ScreenModel& m) {
  const json::Value& spans = s.debrief->reveal["spans"];
  m.text(names::kDebriefTradesTitle, vm.tr("debrief.trades"));
  WidgetTable t{names::kDebriefTrades, trade_columns(vm, !spans.is_null()), {}};
  WidgetLines notes{names::kDebriefTradeNotes, {}};
  for (const auto& tr : d["trades"].items()) {
    t.rows.push_back(trade_row(vm, tr, spans));
    const std::string advice = stop_advice_text(vm.catalog(), tr);
    if (!advice.empty()) notes.lines.push_back({"#" + tr["n"].as_string() + " \xE2\x9C\x96 " + advice, Tone::Down});
  }
  if (t.rows.empty()) notes.lines.push_back({vm.tr("debrief.noTrades"), Tone::Muted});
  m.name(names::kDebriefTrades, vm.tr("debrief.trades"));
  m.tables.push_back(std::move(t));
  m.lines.push_back(std::move(notes));
  calibration_line(vm, d, m);
  advisor_panel(vm, s, d, m);
}

void debrief_reveal_tables(const AppViewModel& vm, const Snapshot& s, const json::Value& d, ScreenModel& m) {
  const json::Value& reveal = s.debrief->reveal;
  regime_tables(vm, s, d, reveal["spans"], m);
  release_table(vm, s, d, reveal, m);
  m.name(names::kDebriefRegimes, vm.tr("debrief.regimesTitle"));
  m.name(names::kDebriefReleases, vm.tr("debrief.releasesTitle"));
  timeline(vm, d, s, m);
}

}  // namespace tradelab::desktop::screens
