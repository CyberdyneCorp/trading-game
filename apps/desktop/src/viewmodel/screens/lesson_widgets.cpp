// The widgets of a lesson step (add-academy-and-strategies design D2; web LessonChart's levels,
// LessonPayoff.svelte, LessonTable.svelte, LessonTicket.svelte) and the question rows that are not
// choices (StepQuestion.svelte: a typed number, a sandbox target). Marked glossary terms carry
// their definitions as tooltips and accessible descriptions.
#include <cmath>
#include <set>

#include "viewmodel/academy/payoff.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/whatif_lab.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

std::string caption(const AppViewModel& vm, const std::string& market, const std::string& what) {
  return vm.tr("academy.market", {{"market", market}, {"interval", what}}) + std::string(kSep) + vm.tr("academy.illustrative");
}

Tone level_tone(const std::string& kind) {
  if (kind == "stop" || kind == "liquidation") return Tone::Down;
  if (kind == "target") return Tone::Up;
  if (kind == "entry") return Tone::Info;
  return kind == "strike" ? Tone::Attention : Tone::Muted;
}

// The chart's labeled lines, also as text (a screen reader cannot read the drawing).
void levels(const AppViewModel& vm, const LessonStep& step, ScreenModel& m) {
  const bool shown = step.chart && !step.chart->levels.empty();
  m.show(names::kLessonLevels, shown);
  if (!shown) return;
  WidgetLines out{names::kLessonLevels, {}};
  for (const auto& l : step.chart->levels) {
    out.lines.push_back({vm.tr("academy.level." + l.kind) + ": " + fmt::fmt_num(l.price, vm.locale(), 2), level_tone(l.kind)});
  }
  m.lines.push_back(std::move(out));
}

// ---- payoff (LessonPayoff.svelte) ----

std::string payoff_number(double v, Locale L) { return fmt::fmt_num(v, L, v >= 1000 ? 0 : 2); }

std::string leg_text(const AppViewModel& vm, const PayoffLeg& l) {
  const Locale L = vm.locale();
  return vm.tr(l.kind == "perp" ? "academy.payoff.perp" : "academy.payoff.option",
               {{"side", vm.tr(l.buy ? "academy.payoff.buy" : "academy.payoff.sell")},
                {"qty", fmt::fmt_num(l.qty, L, 0)},
                {"kind", l.kind == "call" ? "call" : "put"},
                {"strike", payoff_number(l.strike, L)},
                {"price", payoff_number(l.price, L)}});
}

std::string bound(const AppViewModel& vm, const std::optional<double>& v) {
  return v ? payoff_number(*v, vm.locale()) : vm.tr("academy.payoff.unbounded");
}

void payoff(const AppViewModel& vm, const PayoffWidget& w, ScreenModel& m) {
  m.text(names::kLessonPayoffCaption, caption(vm, w.market, vm.tr("academy.payoff.atExpiry")));
  std::string legs;
  for (const auto& l : w.legs) legs += (legs.empty() ? "" : std::string(kSep)) + leg_text(vm, l);
  m.text(names::kLessonPayoffLegs, legs);
  m.name(names::kLessonPayoff, vm.tr("academy.payoff.aria", {{"legs", legs}}));
  m.show(names::kLessonPayoffAnalytics, !w.hide_analytics);
  m.text(names::kLessonPayoffNote, vm.tr("academy.payoff.note"));
  if (w.hide_analytics) return;
  std::string bes;
  for (double b : academy::breakevens(w.legs)) bes += (bes.empty() ? "" : std::string(kSep)) + payoff_number(b, vm.locale());
  m.lines.push_back({names::kLessonPayoffAnalytics,
                     {{vm.tr("academy.payoff.breakeven") + ": " + (bes.empty() ? "\xE2\x80\x94" : bes), Tone::Info,
                       term_tip(vm, "breakeven")},
                      {vm.tr("academy.payoff.maxProfit") + ": " + bound(vm, academy::max_profit(w.legs)), Tone::Up},
                      {vm.tr("academy.payoff.maxLoss") + ": " + bound(vm, academy::max_loss(w.legs)), Tone::Down,
                       term_tip(vm, "defined-risk")}}});
}

// ---- table (LessonTable.svelte) ----

// A number in the locale with the column's digits and suffix, or the step's text of a text cell.
std::string table_cell(const AppViewModel& vm, const std::string& step_base, const TableCell& v, const TableColumn& col) {
  if (!v.number) {
    const std::string key = step_base + ".cell." + v.id;
    return vm.catalog().has(key) ? vm.tr(key) : v.id;
  }
  const int digits = col.digits.value_or(std::floor(v.value) == v.value ? 0 : 2);
  return fmt::fmt_num(v.value, vm.locale(), digits) + col.suffix;
}

void table(const AppViewModel& vm, const std::string& step_base, const TableWidget& w, ScreenModel& m) {
  m.text(names::kLessonTableCaption, caption(vm, w.market, vm.tr("academy.data")));
  WidgetTable t{names::kLessonTable, {}, {}};
  for (std::size_t c = 0; c < w.columns.size(); ++c) {
    const std::string key = step_base + ".column." + std::to_string(c);
    t.columns.push_back(vm.catalog().has(key) ? vm.tr(key) : w.columns[c].id);
  }
  for (const auto& row : w.rows) {
    std::vector<std::string> cells;
    for (std::size_t c = 0; c < row.size() && c < w.columns.size(); ++c) cells.push_back(table_cell(vm, step_base, row[c], w.columns[c]));
    t.rows.push_back(std::move(cells));
  }
  m.tables.push_back(std::move(t));
  m.name(names::kLessonTable, caption(vm, w.market, vm.tr("academy.data")));
}

// ---- ticket sandbox (LessonTicket.svelte) ----

std::string price_or_dash(const std::optional<double>& v, Locale L) { return v ? fmt::fmt_num(*v, L, 2) : "\xE2\x80\x94"; }

void sandbox_inputs(const AppViewModel& vm, const TicketWidget& w, const academy::SandboxInputs& x, ScreenModel& m) {
  const auto& typed = vm.shell().academy.sandbox_text;
  const bool side = w.editable("side");
  m.show(names::kSandboxSideRow, side);
  m.text(names::kSandboxBuy, std::string(x.buy ? "\xE2\x97\x8F " : "") + "\xE2\x96\xB2 " + vm.tr("ticket.buy"));    // ● ▲
  m.text(names::kSandboxSell, std::string(!x.buy ? "\xE2\x97\x8F " : "") + "\xE2\x96\xBC " + vm.tr("ticket.sell"));  // ● ▼
  m.name(names::kSandboxBuy, vm.tr("academy.ticket.side") + ": " + vm.tr("ticket.buy") + (x.buy ? " \xE2\x9C\x93" : ""));
  m.name(names::kSandboxSell, vm.tr("academy.ticket.side") + ": " + vm.tr("ticket.sell") + (!x.buy ? " \xE2\x9C\x93" : ""));
  m.text(names::kSandboxQtyLabel, vm.tr("ticket.qty"));
  m.text(names::kSandboxQty, typed[0]);
  m.enable(names::kSandboxQty, w.editable("qty"));
  m.text(names::kSandboxLevLabel, vm.tr("ticket.leverage") + " (\xE2\x89\xA4 " + std::to_string(w.max_leverage) + "x)");  // ≤
  m.tip(names::kSandboxLevLabel, term_tip(vm, "leverage"));
  m.tip(names::kSandboxLeverage, term_tip(vm, "leverage"));
  m.text(names::kSandboxLeverage, typed[1]);
  m.enable(names::kSandboxLeverage, w.editable("leverage"));
  m.text(names::kSandboxStopLabel, vm.tr("ticket.stopLoss"));
  m.tip(names::kSandboxStopLabel, term_tip(vm, "stop-loss"));
  m.tip(names::kSandboxStop, term_tip(vm, "stop-loss"));
  m.text(names::kSandboxStop, typed[2]);
  m.enable(names::kSandboxStop, w.editable("stop"));
  m.show(names::kSandboxTargetRow, w.target.has_value() || w.editable("target"));
  m.text(names::kSandboxTargetLabel, vm.tr("ticket.takeProfit"));
  m.tip(names::kSandboxTargetLabel, term_tip(vm, "take-profit"));
  m.tip(names::kSandboxTarget, term_tip(vm, "take-profit"));
  m.text(names::kSandboxTarget, typed[3]);
  m.enable(names::kSandboxTarget, w.editable("target"));
}

void sandbox_outputs(const AppViewModel& vm, const academy::SandboxOutputs& out, ScreenModel& m) {
  const Locale L = vm.locale();
  WidgetLines lines{names::kSandboxOut, {}};
  lines.lines.push_back({vm.tr("ticket.estNotional") + ": " + fmt::fmt_money(out.notional, L), Tone::Normal, term_tip(vm, "notional")});
  lines.lines.push_back({vm.tr("ticket.estMargin") + ": " + fmt::fmt_money(out.margin, L), Tone::Normal, term_tip(vm, "initial-margin")});
  lines.lines.push_back({vm.tr("ticket.estLiq") + ": " + price_or_dash(out.liquidation, L), Tone::Down, term_tip(vm, "liquidation-price")});
  const std::string risk = out.risk_at_stop ? fmt::fmt_money(*out.risk_at_stop, L) + " (" + fmt::fmt_pct(out.risk_pct.value_or(0) / 100, L) + ")"
                                            : "\xE2\x80\x94";
  lines.lines.push_back({vm.tr("ticket.estRisk") + ": " + risk, Tone::Normal, term_tip(vm, "risk-budget")});
  if (out.r_target) {
    lines.lines.push_back({vm.tr("academy.ticket.r") + ": " + fmt::fmt_num(*out.r_target, L, 2) + "R", Tone::Info, term_tip(vm, "r-multiple")});
  }
  m.lines.push_back(std::move(lines));
  m.show(names::kSandboxWarn, out.stop_beyond_liquidation);
  m.text(names::kSandboxWarn, "\xE2\x9A\xA0 " + vm.tr("academy.ticket.beyondLiq"));
}

void ticket(const AppViewModel& vm, const TicketWidget& w, ScreenModel& m) {
  const auto& x = vm.shell().academy.sandbox;
  const auto out = vm.sandbox_outputs();
  if (!x || !out) return;
  const Locale L = vm.locale();
  m.text(names::kSandboxCaption, caption(vm, w.market, vm.tr("academy.sandbox")));
  WidgetLines fixed{names::kSandboxFixed, {}};
  fixed.lines.push_back({vm.tr("academy.ticket.equity") + ": " + fmt::fmt_money(w.equity, L), Tone::Normal, term_tip(vm, "equity")});
  fixed.lines.push_back({vm.tr("academy.ticket.entry") + ": " + fmt::fmt_num(w.entry, L, 2)});
  if (!w.editable("side")) fixed.lines.push_back({vm.tr("academy.ticket.side") + ": " + vm.tr(x->buy ? "ticket.buy" : "ticket.sell")});
  m.lines.push_back(std::move(fixed));
  sandbox_inputs(vm, w, *x, m);
  sandbox_outputs(vm, *out, m);
  m.text(names::kSandboxNote, vm.tr("academy.ticket.noCosts"));
}

// ---- what-if lab and builder (LessonWhatIf.svelte, LessonBuilder.svelte): the boxes and captions here;
// the lab panel and the builder's texts come from AppViewModel::lesson_lab() / lesson_builder() ----

void engine_widgets(const AppViewModel& vm, const LessonStep& step, ScreenModel& m) {
  const auto* w = std::get_if<WhatIfWidget>(&step.widget);
  const auto* b = std::get_if<BuilderWidget>(&step.widget);
  m.show(names::kLessonWhatIfBox, w != nullptr);
  m.show(names::kLessonBuilderBox, b != nullptr);
  if (w != nullptr) m.text(names::kLessonWhatIfCaption, caption(vm, w->market, vm.tr("academy.whatif.caption")));
  if (b != nullptr) {
    m.text(names::kLessonBuilderCaption, caption(vm, b->market, vm.tr("academy.builder.caption", {{"days", game::js_number(b->days)}})));
  }
}

// ---- question rows that are not choices (StepQuestion.svelte) ----

void question_rows(const AppViewModel& vm, const LessonStep& step, ScreenModel& m) {
  const LessonRunner& r = vm.shell().lesson;
  const auto type = step.question ? std::optional(step.question->type) : std::nullopt;
  const bool number = type == StepQuestion::Type::Number;
  m.show(names::kLessonNumberRow, number);
  m.show(names::kLessonSetRow, type == StepQuestion::Type::Set);
  if (number) {
    const std::string& typed = vm.shell().academy.typed;
    m.text(names::kLessonNumberLabel, vm.tr("academy.answer"));
    m.text(names::kLessonNumber, typed);
    m.name(names::kLessonNumber, vm.tr("academy.answer"));
    m.enable(names::kLessonNumber, !r.solved());
    m.text(names::kLessonNumberUnit, step.question->unit);
    m.show(names::kLessonNumberUnit, !step.question->unit.empty());
    m.text(names::kLessonCheck, vm.tr("academy.check"));
    m.enable(names::kLessonCheck, !r.solved() && typed.find_first_not_of(" \t") != std::string::npos);
  }
  m.text(names::kLessonSetCheck, vm.tr("academy.check"));
  m.enable(names::kLessonSetCheck, !r.solved());
  m.text(names::kLessonSetHint, vm.tr("academy.setHint"));
}

}  // namespace

void lesson_widgets(const AppViewModel& vm, const std::string& base, const LessonStep& step, ScreenModel& m) {
  const std::string step_base = base + "." + step.id;
  levels(vm, step, m);
  const auto* p = std::get_if<PayoffWidget>(&step.widget);
  const auto* t = std::get_if<TableWidget>(&step.widget);
  const auto* k = std::get_if<TicketWidget>(&step.widget);
  m.show(names::kLessonPayoffBox, p != nullptr);
  m.show(names::kLessonTableBox, t != nullptr);
  m.show(names::kLessonTicketBox, k != nullptr);
  if (p != nullptr) payoff(vm, *p, m);
  if (t != nullptr) table(vm, step_base, *t, m);
  if (k != nullptr) ticket(vm, *k, m);
  engine_widgets(vm, step, m);
  question_rows(vm, step, m);
}

}  // namespace tradelab::desktop::screens
