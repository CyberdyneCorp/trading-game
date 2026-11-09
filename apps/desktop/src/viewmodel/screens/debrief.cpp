// Debrief (web Debrief.svelte with CampaignOutcome, CampaignReport, LessonResults, DecisionStrip,
// RegimeReveal and ReleaseReveal): every number comes from the engine's debrief (tl_debrief), its last
// state and the seeded reveal (tl_mission_reveal); these functions only format them.
#include <algorithm>
#include <cmath>
#include <utility>

#include "model/content.hpp"
#include "viewmodel/campaign.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/texts.hpp"
#include "viewmodel/report_texts.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/academy/glossary.hpp"
#include "viewmodel/screens/screens.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::screens {

namespace {

constexpr std::string_view kDash = "\xE2\x80\x94";  // —

struct Ctx {
  const AppViewModel& vm;
  const Snapshot& s;
  const json::Value& d;  // tl_debrief
  Locale L;
  [[nodiscard]] std::string tr(std::string_view key, const Params& p = {}) const { return vm.tr(key, p); }
  [[nodiscard]] std::string money(const std::string& v, bool sign = false) const { return fmt::fmt_money(v, L, sign); }
  [[nodiscard]] std::string money(double v, bool sign = false) const { return fmt::fmt_money(v, L, sign); }
};

Tone sign_tone(double v) { return v >= 0 ? Tone::Up : Tone::Down; }

Line kv(const Ctx& c, std::string_view key, std::string value, Tone tone = Tone::Normal) {
  return {c.tr(key) + ": " + std::move(value), tone};
}

std::string str(long v) { return std::to_string(v); }

// ---- CampaignOutcome.svelte ----

const CampaignLeg* campaign_leg(const Ctx& c) {
  const auto& link = c.vm.shell().run_campaign;
  const auto& camp = c.vm.progress().campaign;
  if (!link || !camp || camp->id != link->id) return nullptr;
  for (const auto& leg : camp->legs) {
    if (leg.run_id == c.vm.shell().run_id) return &leg;
  }
  return nullptr;
}

void campaign_end_lines(const Ctx& c, const CampaignLeg& leg, const CampaignRun& camp, WidgetLines& out) {
  const WebContent& content = web_content();
  if (c.s.status == "finished" && below_campaign_minimum(content, leg.end_capital)) {
    out.lines.push_back({c.tr("campaign.broke", {{"min", c.money(content.campaign_min_capital)}}), Tone::Down});
  }
  if (camp.status == CampaignStatus::Complete) {
    out.lines.push_back({c.tr("campaign.complete", {{"capital", c.money(camp.capital)}}), Tone::Up});
  } else {
    out.lines.push_back({c.tr("campaign.over", {{"capital", c.money(content.campaign_capital)}}), Tone::Down});
  }
}

void campaign_outcome(const Ctx& c, ScreenModel& m) {
  const CampaignLeg* leg = campaign_leg(c);
  m.show(names::kDebriefCampaignPanel, leg != nullptr);
  if (leg == nullptr) return;
  const WebContent& content = web_content();
  const CampaignRun& camp = *c.vm.progress().campaign;
  const auto total = campaign_order(content, camp).size();
  WidgetLines out{names::kDebriefCampaign, {}};
  out.lines.push_back({c.tr("campaign.tag", {{"n", str(c.vm.shell().run_campaign->index + 1)}, {"total", str(static_cast<long>(total))}}), Tone::Title});
  out.lines.push_back({c.tr("campaign.leg", {{"start", c.money(leg->start_capital)}, {"end", c.money(leg->end_capital)}})});
  std::vector<std::string> passed;
  if (leg->result == LegResult::Finished) {
    for (const auto& st : stages_passed_with(content, leg->mission_id, c.vm.progress().assessed)) passed.push_back(c.tr("stage." + st));
  }
  std::string stages;
  for (const auto& p : passed) stages += (stages.empty() ? "" : ", ") + p;
  if (!stages.empty()) {
    out.lines.push_back({"\xE2\x9C\x93 " + c.tr(passed.size() > 1 ? "campaign.passedMany" : "campaign.passed", {{"stages", stages}}), Tone::Up});
  }
  const std::string next = next_campaign_mission(content, c.vm.progress().campaign);
  const bool going = camp.status == CampaignStatus::Active && !next.empty();
  if (going) {
    out.lines.push_back({c.tr("campaign.carry", {{"capital", c.money(camp.capital)}, {"mission", mission_title(c.vm, next)}})});
  } else {
    campaign_end_lines(c, *leg, camp, out);
  }
  m.lines.push_back(std::move(out));
  m.show(names::kDebriefCampaignNext, going);
  m.show(names::kDebriefCampaignNew, !going);
  m.text(names::kDebriefCampaignNext, "\xE2\x96\xB6 " + c.tr("campaign.next"));
  m.text(names::kDebriefCampaignNew, "\xE2\x96\xB6 " + c.tr("campaign.new"));
}

// ---- CampaignReport.svelte (the capstone closes the campaign run) ----

std::string fmt_r(const Ctx& c, const std::optional<double>& v) {
  return v ? fmt::fmt_num(*v, c.L, 2, true) + "R" : std::string(kDash);
}

void report_lines(const Ctx& c, const CampaignReport& r, WidgetLines& out) {
  out.lines.push_back({c.tr("report.missions", {{"n", str(r.missions)}}), Tone::Muted});
  out.lines.push_back({(r.too_few ? "\xE2\x9A\xA0 " : "\xE2\x84\xB9 ") + c.tr(r.too_few ? "report.tooFew" : "report.enough", {{"n", str(r.trades)}}),
                       r.too_few ? Tone::Attention : Tone::Info});
  out.lines.push_back(kv(c, "report.trades", str(r.trades)));
  out.lines.push_back(kv(c, "report.winRate", r.win_rate ? fmt::fmt_pct(*r.win_rate, c.L, 0) : std::string(kDash)));
  out.lines.push_back(kv(c, "report.avgWin", fmt_r(c, r.avg_win_r), Tone::Up));
  out.lines.push_back(kv(c, "report.avgLoss", fmt_r(c, r.avg_loss_r), Tone::Down));
  out.lines.push_back(kv(c, "report.expectancy", fmt_r(c, r.expectancy_r)));
  out.lines.push_back(kv(c, "report.costs", c.money(r.costs)));
  out.lines.push_back(kv(c, "report.costShare", r.cost_share ? fmt::fmt_pct(*r.cost_share, c.L, 1) : c.tr("report.grossNegative")));
  if (r.without_r > 0) out.lines.push_back({c.tr("report.noR"), Tone::Muted});
}

void campaign_report_panel(const Ctx& c, ScreenModel& m) {
  const auto& link = c.vm.shell().run_campaign;
  const auto& camp = c.vm.progress().campaign;
  const bool shown = c.s.mission == "m12-capstone" && link && camp && camp->id == link->id;
  m.show(names::kDebriefReportPanel, shown);
  if (!shown) return;
  const CampaignReport r = campaign_report(web_content(), camp->legs);
  WidgetLines out{names::kDebriefReport, {{c.tr("report.title"), Tone::Title}}};
  if (r.missions == 0) {
    out.lines.push_back({c.tr("report.none"), Tone::Muted});
  } else {
    report_lines(c, r, out);
  }
  m.lines.push_back(std::move(out));
  WidgetTable t{names::kDebriefReportRegimes,
                {c.tr("report.col.regime"), c.tr("report.col.trades"), c.tr("report.col.wins"), c.tr("report.col.net")},
                {}};
  for (const auto& row : r.by_regime) {
    t.rows.push_back({regime_name(c.vm.catalog(), row.regime), str(row.trades), str(row.wins), c.money(row.net, true)});
  }
  m.show(names::kDebriefReportRegimes, !t.rows.empty());
  m.tables.push_back(std::move(t));
}

// ---- Outcome and score ----

void outcome_lines(const Ctx& c, ScreenModel& m) {
  const json::Value& totals = c.s.state["totals"];
  WidgetLines out{names::kDebriefOutcomeLines, {}};
  out.lines.push_back(kv(c, "rail.realized", c.money(totals["realizedPnl"].as_string(), true)));
  const double open = fmt::num(c.s.equity) - fmt::num(c.s.wallet);
  if (open != 0 && std::isfinite(open)) out.lines.push_back(kv(c, "debrief.openPnl", c.money(open, true), sign_tone(open)));
  out.lines.push_back(kv(c, "rail.fees", c.money(totals["feesPaid"].as_string())));
  out.lines.push_back(kv(c, "rail.funding", c.money(totals["fundingNet"].as_string(), true)));
  out.lines.push_back(kv(c, "debrief.spread", c.money(c.d["spreadSlippage"].as_string())));
  if (c.s.state["options"]["enabled"].as_bool(false)) out.lines.push_back(kv(c, "opt.realized", c.money(totals["optionRealized"].as_string(), true)));
  if (fmt::num(c.d["writtenOff"].as_string()) > 0) out.lines.push_back(kv(c, "debrief.writtenOff", c.money(c.d["writtenOff"].as_string(), true), Tone::Up));
  out.lines.push_back(kv(c, "debrief.maxDrawdown", c.money(totals["maxDrawdown"].as_string())));
  out.lines.push_back(kv(c, "debrief.peakExposure", fmt_leverage_bp(totals["peakLeverageBp"].as_double(NAN), c.L)));
  out.lines.push_back(kv(c, "debrief.minLiqDistance", fmt_liq_distance(totals["minLiqDistanceBp"].as_double(-1), c.L)));
  out.lines.push_back(kv(c, "debrief.turnover", c.money(totals["turnover"].as_string())));
  out.lines.push_back(kv(c, "debrief.vsFlat", c.money(0.0)));
  const double hold = fmt::num(c.d["buyAndHoldPnl"].as_string());
  out.lines.push_back(kv(c, "debrief.vsHold", c.money(hold, true), sign_tone(hold)));
  m.lines.push_back(std::move(out));
}

void score_lines(const Ctx& c, ScreenModel& m) {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kDims = {
      {{"risk", "debrief.risk"}, {"plan", "debrief.plan"}, {"understanding", "debrief.understanding"}, {"calibration", "debrief.calibration"}}};
  WidgetLines out{names::kDebriefDims, {}};
  for (const auto& [key, label] : kDims) {
    const json::Value& dim = c.d["score"]["dims"][key];
    const std::string score = dim["score"].is_null() ? c.tr("debrief.na") : dim["score"].as_string();
    out.lines.push_back({c.tr(label) + " (" + dim["weight"].as_string() + "%): " + score});
  }
  m.lines.push_back(std::move(out));
  m.text(names::kDebriefScoreNote, c.tr("debrief.scoreNote"));
}

// ---- Reveal ----

void reveal_lines(const Ctx& c, ScreenModel& m) {
  m.text(names::kDebriefRevealTitle, c.tr("debrief.reveal"));
  WidgetLines out{names::kDebriefReveal, {}};
  out.lines.push_back({c.tr("debrief.revealSynthetic", {{"alias", c.d["alias"].as_string()}, {"seed", c.d["seed"].as_string()}})});
  out.lines.push_back({c.tr("debrief.scenario"), Tone::Title});
  out.lines.push_back({c.tr("mission." + c.s.mission + ".reveal"), Tone::Attention});
  out.lines.push_back({c.tr("debrief.marketSpan", {{"from", fmt::fmt_price(c.d["startPrice"].as_string(), c.L)},
                                                   {"to", fmt::fmt_price(c.d["endPrice"].as_string(), c.L)}}),
                       Tone::Muted});
  if (c.s.step < c.s.total_bars - 1) {
    out.lines.push_back({c.tr("debrief.endedEarly", {{"step", str(c.s.step)}, {"end", str(c.s.total_bars - 1)}}), Tone::Muted});
  }
  m.lines.push_back(std::move(out));
}

// ---- Findings, lesson results, decisions ----

Tone finding_tone(const std::string& tone) {
  if (tone == "bad") return Tone::Down;
  if (tone == "good") return Tone::Up;
  return tone == "warn" ? Tone::Attention : Tone::Info;
}

std::string_view finding_mark(Tone tone) {
  if (tone == Tone::Up) return "\xE2\x9C\x94 ";         // ✔
  if (tone == Tone::Down) return "\xE2\x9C\x96 ";       // ✖
  if (tone == Tone::Attention) return "\xE2\x9A\xA0 ";  // ⚠
  return "\xE2\x84\xB9 ";                               // ℹ
}

void findings_lines(const Ctx& c, ScreenModel& m) {
  m.text(names::kDebriefFindingsTitle, c.tr("debrief.findings"));
  const json::Value& findings = c.d["findings"];
  const int liq = static_cast<int>(c.s.state["missionRules"]["liqWarningBp"].as_int(-1));
  const auto texts = describe_findings(c.vm.catalog(), findings, liq);
  WidgetLines out{names::kDebriefFindings, {}};
  for (std::size_t i = 0; i < texts.size(); ++i) {
    const Tone tone = finding_tone(findings[i]["tone"].as_string());
    // The finding's glossary term, marked in place (glossary.ts FINDING_TERMS).
    const auto term = academy::finding_term(web_content(), findings[i]["code"].as_string());
    out.lines.push_back({std::string(finding_mark(tone)) + texts[i], tone, term ? term_tip(c.vm, *term) : std::string()});
  }
  m.lines.push_back(std::move(out));
  m.text(names::kDebriefNextLesson, c.tr("debrief.nextLesson") + ": " + describe_lesson(c.vm.catalog(), c.d["lesson"].as_string()));
  m.text(names::kDebriefDisclaimer, c.tr("debrief.disclaimer"));
}

// lessons.ts answerText: a choice's wording, or a numeric answer in the locale.
std::string answer_text(const Ctx& c, const json::Value& q, std::string_view which) {
  const json::Value& v = q[which];
  if (v.is_null()) return std::string(kDash);
  if (q["kind"].as_string() == "numeric") {
    const double x = v.as_double();
    return fmt::fmt_num(x, c.L, std::floor(x) == x ? 0 : 2);
  }
  const std::string key = "lessonq." + c.s.mission + "." + q["id"].as_string() + ".choice" + v.as_string();
  return c.vm.catalog().has(key) ? c.tr(key) : v.as_string();
}

void question_lines(const Ctx& c, const json::Value& q, std::size_t n, WidgetLines& out) {
  const std::string prompt = "lessonq." + c.s.mission + "." + q["id"].as_string() + ".prompt";
  out.lines.push_back({std::to_string(n) + ". " + (c.vm.catalog().has(prompt) ? c.tr(prompt) : q["id"].as_string())});
  const bool right = q["correct"].as_bool(false);
  const std::string yours = q["answered"].as_bool(false) ? answer_text(c, q, "given")
                                                         : c.tr(q["skipped"].as_bool(false) ? "lesson.skipped" : "lesson.none");
  out.lines.push_back({std::string(right ? "\xE2\x9C\x94 " : "\xE2\x9C\x96 ") + c.tr("lesson.yours") + ": " + yours + " (" +
                           c.tr(right ? "lesson.right" : "lesson.wrong") + ")",
                       right ? Tone::Up : Tone::Down});
  if (!right) out.lines.push_back({c.tr("lesson.key") + ": " + answer_text(c, q, "answer"), Tone::Muted});
}

// LessonResults.svelte: each question's wording, the player's answer and, when wrong, the key.
void lesson_results(const Ctx& c, ScreenModel& m) {
  const json::Value& l = c.d["lessons"];
  const auto& questions = l["questions"].items();
  const bool asked = std::ranges::any_of(questions, [](const json::Value& q) { return q["answered"].as_bool(false) || q["skipped"].as_bool(false); });
  m.show(names::kDebriefLessons, asked);
  if (!asked) return;
  WidgetLines out{names::kDebriefLessons, {{c.tr("debrief.lessons"), Tone::Title}}};
  out.lines.push_back({c.tr("debrief.lessonsScore", {{"points", l["points"].as_string()}, {"total", l["total"].as_string()}})});
  if (!l["reviewed"].as_bool(false)) out.lines.push_back({c.tr("debrief.lessonsUnreviewed"), Tone::Muted});
  for (std::size_t i = 0; i < questions.size(); ++i) question_lines(c, questions[i], i + 1, out);
  m.lines.push_back(std::move(out));
}

void decisions(const Ctx& c, ScreenModel& m) {
  const DecisionCounts n = decision_counts(c.d["decisions"]);
  const long total = n.act + n.hold + n.wait;
  WidgetLines out{names::kDebriefDecisions, {{c.tr("debrief.decisions"), Tone::Title}}};
  out.lines.push_back({c.tr("debrief.decisionsSummary", {{"act", str(n.act)}, {"hold", str(n.hold)}, {"wait", str(n.wait)}, {"n", str(total)}})});
  out.lines.push_back({c.tr("debrief.decisionsNote"), Tone::Muted});
  m.show(names::kDebriefDecisions, total > 0);
  m.lines.push_back(std::move(out));
}

void greeks(const Ctx& c, ScreenModel& m) {
  const json::Value& g = c.d["greeks"];
  m.show(names::kDebriefGreeksPanel, !g.is_null());
  if (g.is_null()) return;
  WidgetLines out{names::kDebriefGreeks, {{c.tr("debrief.greeksTitle"), Tone::Title}}};
  for (const auto& row : greek_rows(g)) {
    out.lines.push_back(kv(c, "greek." + row.key, std::string(fmt::sign_mark(row.value)) + " " + c.money(row.value, true), sign_tone(row.value)));
  }
  out.lines.push_back(kv(c, "greek.total", c.money(g["total"].as_string(), true)));
  out.lines.push_back({c.tr("debrief.greeksNote"), Tone::Muted});
  m.lines.push_back(std::move(out));
}

// ---- StrategyComparison.svelte ----

// A structure's name and its rule: "Iron condor (asas 4 strikes fora)" -> {"Iron condor", "asas 4 strikes fora"}.
// The table keeps the short name (a cell cannot wrap); the rule is listed under it.
// The player's own row ("Você (esta partida)") stays whole.
// Longer names cut at their first separator ("O mesmo straddle, delta zerado ..." -> {"O mesmo
// straddle", "delta zerado ..."}; "Diagonal: call curta ..." -> {"Diagonal", "call curta ..."}): a
// parenthesis opened in the head is closed in the rule's text.
std::pair<std::string, std::string> name_parts(const Ctx& c, const ComparisonLine& l) {
  const std::string full = c.tr(l.name);
  if (l.key == "you") return {full, ""};
  std::size_t cut = std::string::npos;
  std::size_t skip = 0;
  for (const std::string_view sep : {" (", ": ", ", "}) {
    const auto at = full.find(sep);
    if (at < cut) {
      cut = at;
      skip = sep.size();
    }
  }
  if (cut == std::string::npos) return {full, ""};
  std::string rule = full.substr(cut + skip);
  if (full.compare(cut, 2, " (") == 0) {
    if (const auto close = rule.find(')'); close != std::string::npos) rule.erase(close, 1);
  }
  return {full.substr(0, cut), rule};
}

// The table's names: two structures with the same short name are told apart by their order (" · 2").
std::vector<std::string> comparison_heads(const Ctx& c, const std::vector<ComparisonLine>& lines) {
  std::vector<std::string> heads;
  for (const auto& l : lines) heads.push_back(name_parts(c, l).first);
  std::vector<std::string> out;
  for (std::size_t i = 0; i < heads.size(); ++i) {
    const auto same = std::count(heads.begin(), heads.end(), heads[i]);
    const auto nth = std::count(heads.begin(), heads.begin() + static_cast<std::ptrdiff_t>(i) + 1, heads[i]);
    out.push_back(same > 1 ? heads[i] + std::string(kSep) + std::to_string(nth) : heads[i]);
  }
  return out;
}

std::vector<std::string> comparison_cells(const Ctx& c, const ComparisonLine& l, const std::string& head) {
  std::string name = head + (l.best ? " [" + c.tr("comparison.best") + "]" : "");
  if (!l.refused.empty()) return {"\xE2\x9C\x96 " + name, std::string(kDash), std::string(kDash), std::string(kDash), std::string(kDash)};
  const std::string bars = l.entry_step ? str(*l.entry_step) + " \xE2\x86\x92 " + str(l.exit_step.value_or(0)) : std::string(kDash);  // →
  return {std::move(name), bars, l.net ? c.money(*l.net, true) : std::string(kDash), l.drawdown ? c.money(*l.drawdown) : std::string(kDash),
          l.margin_calls ? str(*l.margin_calls) : std::string(kDash)};
}

void comparison(const Ctx& c, ScreenModel& m) {
  const json::Value& cmp = c.d["strategyComparison"];
  m.show(names::kDebriefComparisonPanel, !cmp.is_null());
  if (cmp.is_null()) return;
  m.text(names::kDebriefComparisonTitle, c.tr("comparison.title"));
  m.text(names::kDebriefComparisonBasis, c.tr(comparison_basis_key(cmp),
                                              {{"step", cmp["entryStep"].as_string()}, {"qty", fmt::fmt_qty(cmp["qty"].as_string(), c.L)}}));
  WidgetTable t{names::kDebriefComparison,
                {c.tr("comparison.col.structure"), c.tr("comparison.col.bars"), c.tr("comparison.col.net"), c.tr("comparison.col.drawdown"),
                 c.tr("comparison.col.calls")},
                {},
                {3.4f, 1, 1.2f, 1.2f, 1}};
  WidgetLines notes{names::kDebriefComparisonNotes, {}};
  const auto lines = comparison_lines(cmp, fmt::num(c.d["netPnl"].as_string()));
  const auto heads = comparison_heads(c, lines);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const ComparisonLine& l = lines[i];
    const std::string& name = heads[i];
    t.rows.push_back(comparison_cells(c, l, name));
    const std::string rule = name_parts(c, l).second;
    if (!rule.empty()) notes.lines.push_back({name + ": " + rule, Tone::Muted});
    if (!l.refused.empty()) {
      notes.lines.push_back({"\xE2\x9C\x96 " + name + ": " + c.tr("comparison.refused", {{"reason", game::rejection_text(c.vm.game().texts(), l.refused)}}),
                             Tone::Attention});
    }
  }
  m.name(names::kDebriefComparison, c.tr("comparison.title"));
  m.tables.push_back(std::move(t));
  m.show(names::kDebriefComparisonNotes, !notes.lines.empty());
  m.lines.push_back(std::move(notes));
  m.text(names::kDebriefComparisonNote, c.tr("comparison.note"));
}

}  // namespace

// Defined in debrief_tables.cpp.
void debrief_trades(const AppViewModel& vm, const Snapshot& s, const json::Value& d, ScreenModel& m);
void debrief_reveal_tables(const AppViewModel& vm, const Snapshot& s, const json::Value& d, ScreenModel& m);

namespace {

// The run's chart (painted by the view from AppViewModel::debrief_scene) and its candle table, a page
// of 50 closed bars at a time (web ChartTable.svelte).
void chart_panel(const AppViewModel& vm, const Snapshot& s, ScreenModel& m) {
  m.text(names::kDebriefChartTitle, vm.tr("desktop.debrief.chart"));
  m.text(names::kDebriefChartNote, vm.tr("desktop.debrief.chartNote"));
  m.name(names::kDebriefChart, vm.tr("desktop.debrief.chart"));
  if (!s.game) return;
  const GameState& g = *s.game;
  const game::BarPage p = vm.debrief_bar_page();
  m.text(names::kDebriefBarsPage, vm.tr("chart.tablePage", {{"from", std::to_string(p.start)},
                                                           {"to", std::to_string(std::max(p.start, p.end - 1))},
                                                           {"total", std::to_string(g.bars.size())}}));
  WidgetTable table{names::kDebriefBars, {"#", "O", "H", "L", "C", "V"}, {}};
  for (int i = p.start; i < p.end; ++i) {
    const GameBar& b = g.bars[static_cast<std::size_t>(i)];
    std::vector<std::string> row{std::to_string(i) + (b.continuity ? " *" : "")};
    for (auto& cell : game::candle_cells(b, vm.locale())) row.push_back(std::move(cell));
    table.rows.push_back(std::move(row));
  }
  m.tables.push_back(std::move(table));
  m.name(names::kDebriefBars, vm.tr("chart.table"));
  m.text(names::kDebriefBarsPrev, "\xE2\x97\x80 " + vm.tr("chart.prevPage"));
  m.text(names::kDebriefBarsNext, vm.tr("chart.nextPage") + " \xE2\x96\xB6");
  m.enable(names::kDebriefBarsPrev, p.page > 0);
  m.enable(names::kDebriefBarsNext, p.page < p.pages - 1);
}

}  // namespace

ScreenModel debrief(const AppViewModel& vm) {
  ScreenModel m;
  for (auto& b : vm.bindings()) m.text(b.widget, std::move(b.text));
  const Snapshot* s = vm.snapshot();
  if (s == nullptr || !s->debrief) return m;
  const Ctx c{vm, *s, s->debrief->raw, vm.locale()};
  const bool bankrupt = s->status == "bankrupt";
  m.show(names::kDebriefText, bankrupt);
  m.text(names::kDebriefMission, mission_title(vm, s->mission));
  m.show(names::kDebriefTags, false);  // intel perks: not on the desktop yet
  const bool assisted = vm.run_assisted().has_value();
  m.show(names::kDebriefAssisted, assisted);
  m.text(names::kDebriefAssisted, "[" + vm.tr("assist.tag") + "] " + vm.tr("assist.banner"));
  campaign_outcome(c, m);
  campaign_report_panel(c, m);
  outcome_lines(c, m);
  score_lines(c, m);
  reveal_lines(c, m);
  findings_lines(c, m);
  debrief_episodes(vm, m);
  lesson_results(c, m);
  decisions(c, m);
  greeks(c, m);
  debrief_trades(vm, *s, c.d, m);
  comparison(c, m);
  debrief_reveal_tables(vm, *s, c.d, m);
  chart_panel(vm, *s, m);
  assist_panel(vm, names::kDebriefAssist, m);
  academy_debrief(vm, m);
  m.show(names::kDebriefAgain, !vm.shell().run_campaign);
  m.text(names::kDebriefAgain, "\xE2\x86\xBB " + vm.tr(vm.run_pack().empty() ? "debrief.again" : "debrief.againPack"));  // ↻
  debrief_replay(vm, m);
  return m;
}

}  // namespace tradelab::desktop::screens
