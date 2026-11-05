// GameView: static labels, top bar, chart texts and bar table, rail, orders, advisor, news, feed.
#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include "view/game/game_view.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop::view {

namespace n = names::game;

namespace {

void items(Rid combo, const std::vector<std::string>& texts) {
  if (combo == R_INVALID_ID) return;
  const int selected = std::max(0, Rcombo_box_get_selected(combo));
  Rcombo_box_clear_items(combo);
  for (const auto& s : texts) Rcombo_box_add_item(combo, s.c_str());
  Rcombo_box_set_selected(combo, selected);
}

std::string kv_text(const std::vector<game::KeyValue>& rows) {
  std::string out;
  for (const auto& r : rows) out += (out.empty() ? "" : "\n") + r.key + ": " + r.value;
  return out;
}

}  // namespace

void GameView::bind_rail_and_panels() {
  cb_.text(find(n::kMarginAmount), [this](const std::string& text) { vm().margin_amount = text; });
  cb_.click(find(n::kMarginAdd), [this] { vm().adjust_margin(1); });
  cb_.click(find(n::kMarginRemove), [this] { vm().adjust_margin(-1); });
  cb_.click(find(n::kAdvAsk), [this] { vm().ask_advice(); });
  cb_.click(find(n::kAdvApply), [this] { vm().apply_advice(); });
}

// Texts that change only with the language.
void GameView::apply_labels() {
  const game::Texts t = vm().texts();
  const auto label = [&](std::string_view name, const std::string& text) { set_label(find(name), text); };
  ui::set_text(find(n::kShowPatterns), t.tr("chart.patterns"));
  ui::set_text(find(n::kTableToggle), "\xE2\x96\xA6 " + t.tr("chart.tableToggle"));
  ui::set_text(find(n::kTableLast), t.tr("chart.lastPage"));
  ui::set_text(find(n::kAutoEvents), t.tr("auto.pauseOnEvents"));
  ui::set_text(find(n::kPatterns), t.tr("patterns.open"));
  ui::set_text(find(n::kJournal), t.tr("journal.open"));
  ui::set_text(find(n::kLessons), t.tr("lesson.title"));
  ui::set_text(find(n::kTutorial), t.tr("tut.title"));
  ui::set_text(find(n::kPerks), t.tr("perk.title"));
  ui::set_text(find(n::kMarginAdd), t.tr("rail.addMargin"));
  ui::set_text(find(n::kMarginRemove), t.tr("rail.removeMargin"));
  ui::set_text(find(n::kAdvAsk), t.tr("adv.ask"));
  ui::set_text(find(n::kAdvApply), t.tr("adv.apply"));
  ui::set_text(find(n::kProtect), t.tr("ticket.protectSubmit"));
  ui::set_text(find(n::kBeyondConfirm), t.tr("ticket.confirmBeyond"));
  ui::set_text(find(n::kProtBeyondConfirm), t.tr("ticket.confirmBeyond"));
  label("game-orders-title", t.tr("orders.title"));
  label("game-adv-title", t.tr("adv.title"));
  label("game-news-title", t.tr("news.title") + "  [" + t.tr("news.fictional") + "]");
  label("game-feed-title", t.tr("feed.title"));
  label(n::kOrdersNone, t.tr("orders.none"));
  items(find(n::kType), {t.tr("ticket.market"), t.tr("ticket.limit"), t.tr("ticket.stop")});
  items(find(n::kSizeBy), {t.tr("ticket.sizeRisk"), t.tr("ticket.sizeQty"), t.tr("ticket.sizeNotional"), t.tr("ticket.sizeEquity")});
  items(find(n::kOptType), {t.tr("opt.market"), t.tr("opt.limit")});
  items(find(n::kOsc), {"RSI", "MACD", "ADX", t.tr("chart.oscNone")});
  Rwidget_set_tooltip(find(n::kAutoSpeed), t.tr("auto.speed").c_str());
  Rwidget_set_tooltip(find(n::kAutoEvents), t.tr("auto.pauseOnEventsHint").c_str());
  Rwidget_set_tooltip(find(n::kHelp), t.tr("top.help").c_str());
  Rwidget_set_accessible_name(find(names::kGameChart), t.tr("chart.focusHint").c_str());
  Rwidget_set_accessible_name(find(n::kTable), t.tr("chart.table").c_str());
  // Dialog titles and buttons.
  label("game-plan-title", t.tr("plan.title"));
  label("game-plan-why", t.tr("plan.why"));
  label("game-plan-thesis-label", t.tr("plan.thesis"));
  label("game-plan-inv-label", t.tr("plan.invalidation"));
  label("game-plan-target-label", t.tr("plan.target"));
  label("game-plan-lose-label", t.tr("opt.loseScenario"));
  ui::set_text(find(n::kPlanCancel), t.tr("plan.cancel"));
  ui::set_text(find(n::kPlanSkip), t.tr("plan.skip"));
  ui::set_text(find(n::kPlanSave), t.tr("plan.save"));
  label("game-cal-title", t.tr("cal.title"));
  label("game-cal-why", t.tr("cal.why"));
  ui::set_text(find(n::kCalSkip), t.tr("cal.skip"));
  ui::set_text(find(n::kCalAnswer), t.tr("cal.answer"));
  label("game-confirm-title", "\xE2\x9A\xA0 " + t.tr("chart.moveBeyondTitle"));
  ui::set_text(find(n::kConfirmNo), t.tr("chart.moveBeyondNo"));
  ui::set_text(find(n::kConfirmYes), t.tr("chart.moveBeyondYes"));
  label("game-help-title", t.tr("help.title"));
  label("game-help-note", t.tr("help.note"));
  ui::set_text(find(n::kHelpReset), t.tr("keys.reset"));
  label("game-journal-title", t.tr("runJournal.title"));
  label("game-lesson-title", t.tr("lesson.title"));
  label("game-lesson-intro", t.tr("lesson.intro"));
  ui::set_text(find(n::kTutorialPrev), t.tr("tut.prev"));
  ui::set_text(find(n::kTutorialSkip), t.tr("tut.skip"));
  label("game-perk-title", t.tr("perk.title"));
  label("game-perk-note", t.tr("perk.note"));
  chain_key_.clear();
  lesson_key_.clear();
}

void GameView::apply_top(const game::Texts& t, const GameState& g) {
  game::GameViewModel& m = vm();
  set_label(find(n::kInterval), game::interval_label(g.step_seconds));
  const bool running = m.autoplay.running;
  ui::set_text(find(n::kAuto), std::string(running ? "\xE2\x80\x96 " : "\xC2\xBB ") + t.tr(running ? "auto.pause" : "auto.play") +
                                   " [" + game::key_label(m.keymap().key(game::Action::Auto)) + "]");
  set_enabled(find(names::kGameAdvance), !m.busy() && g.active());
  ui::set_text(find(n::kSound), m.muted() ? "\xE2\x99\xAA\xE2\x9C\x95" : "\xE2\x99\xAA");  // ♪✕ / ♪
  Rwidget_set_tooltip(find(n::kSound), t.tr(m.muted() ? "sound.unmute" : "sound.mute").c_str());
  set_visible(find(n::kLessons), !game::LessonDialog{}.questions(t, g.lesson).empty());
  set_visible(find(n::kTutorial), !game::tutorial_cards(t, g.mission).empty());
  set_visible(find(n::kPerks), !g.known_scenario);
  const game::Line& notice = m.notice();
  set_color(find(names::kGameNotice), notice.tone == game::Tone::Plain ? game::Tone::Attention : notice.tone);
}

void GameView::apply_chart(const game::Texts& t, const GameState& g) {
  game::GameViewModel& m = vm();
  const bool picking = m.picking() != game::PickTarget::None;
  set_label(find(n::kReadout), m.chart.readout(t, g));
  std::string patterns;
  const auto hits = m.chart.hover_patterns(t, g);
  if (!hits.empty()) patterns = t.tr("chart.patternAt");
  for (const auto& [name, bias] : hits) {
    patterns += std::string("  ") + (bias > 0 ? "\xE2\x96\xB2 " : bias < 0 ? "\xE2\x96\xBC " : "\xE2\x97\x86 ") + name;
  }
  set_label(find(n::kPatternLine), patterns);
  std::string status = m.chart.status_line(t, g, picking);
  const std::string said = m.chart.announcement(t, g);
  if (!said.empty()) status += (status.empty() ? "" : "  \xC2\xB7  ") + said;
  set_label(find(n::kChartStatus), status);
  const Rid chart = find(names::kGameChart);
  Rwidget_set_accessible_description(chart, game::chart_summary(t, g.bars).c_str());
  Rwidget_set_cursor(chart, picking ? R_CURSOR_CROSSHAIR : (m.chart.grabbable() || m.chart.dragging()) ? R_CURSOR_RESIZE_V : R_CURSOR_CROSSHAIR);
  apply_table(t, g);
}

void GameView::apply_table(const game::Texts& t, const GameState& g) {
  game::GameViewModel& m = vm();
  set_visible(find(n::kTableBox), m.chart.table_open);
  if (!m.chart.table_open) return;
  const game::BarPage p = m.chart.page(g);
  set_label(find(n::kTablePage), t.tr("chart.tablePage", {{"from", std::to_string(p.start)},
                                                         {"to", std::to_string(std::max(p.start, p.end - 1))},
                                                         {"total", std::to_string(g.bars.size())}}));
  // Only the bars the engine released: the page never reaches past the current bar.
  Rtable_set_virtual_rows(find(n::kTable), p.end - p.start, on_table_cell, this);
  set_enabled(find(n::kTablePrev), p.page > 0);
  set_enabled(find(n::kTableNext), p.page < p.pages - 1);
}

void GameView::apply_rail(const game::Texts& t, const GameState& g) {
  game::GameViewModel& m = vm();
  const game::RailView r = game::rail_view(t, g, m.planned_risk());
  set_label(find(n::kRailEquity), t.tr("rail.equity") + "  " + r.equity);
  set_label(find(n::kRailPnl), r.pnl.text);
  set_color(find(n::kRailPnl), r.pnl.tone);
  set_label(find(n::kRailAccount), kv_text(r.account));
  set_label(find(n::kRailPosition), t.tr("rail.position") + ": " + r.position.text);
  set_color(find(n::kRailPosition), r.position.tone);
  std::vector<game::KeyValue> details = r.details;
  if (r.planned_risk) details.push_back(*r.planned_risk);
  set_label(find(n::kRailDetails), kv_text(details));
  set_label(find(n::kRailLiq), r.open ? r.liq_label + "  " + r.liq_price + "\n" + r.liq_distance : std::string());
  set_label(find(n::kRailLiqWarning), r.liq_warning);
  set_visible(find(n::kRailLiqWarning), r.liq_danger);
  set_visible(find(n::kMarginBox), r.open);
  ui::set_text(find(n::kMarginAmount), m.margin_amount);
  std::vector<game::Line> options;
  if (r.options) options.push_back({t.tr("opt.positions"), game::Tone::Muted});
  options.insert(options.end(), r.option_positions.begin(), r.option_positions.end());
  for (const auto& kv : r.option_book) options.push_back({kv.key + ": " + kv.value, game::Tone::Plain});
  rail_options_.set(options);
  set_label(find(n::kRailFunding), r.funding_title + "\n" + r.funding.key + ": " + r.funding.value + "\n" + r.funding_in +
                                       (r.funding_who.empty() ? "" : "\n" + r.funding_who));
  std::string objectives = t.tr("rail.objective");
  for (const auto& o : r.objectives) objectives += "\n\xE2\x80\xA2 " + o;
  set_label(find(n::kRailObjectives), objectives);
}

void GameView::apply_orders(const game::Texts& t, const GameState& g) {
  const Rid box = find(n::kOrders);
  const auto rows = game::order_rows(t, g, vm().series_names());
  set_visible(find(n::kOrdersNone), rows.empty());
  std::vector<int> ids;
  for (const auto& r : rows) ids.push_back(r.id);
  if (ids != order_ids_ || order_rows_.size() != rows.size()) {
    clear_rows(box, order_rows_);
    order_ids_ = ids;
    for (const auto& r : rows) {
      const Rid row = Rwidget_create(R_WIDGET_HBOX);
      const Rid label = Rlabel_create("");
      Rlabel_set_text_size(label, 12);
      Rwidget_add_child(row, label);
      Rslot_set_fill(label, 1);
      const Rid cancel = Rbutton_create(t.tr("orders.cancel").c_str());
      const int id = r.id;
      cb_.click(cancel, [this, id] { vm().cancel_order(id); });
      Rwidget_set_name(cancel, ("game-order-cancel-" + std::to_string(id)).c_str());
      Rwidget_add_child(row, cancel);
      Rwidget_add_child(box, row);
      order_rows_.push_back(row);
    }
  }
  for (std::size_t i = 0; i < rows.size() && i < order_rows_.size(); ++i) {
    const Rid label = Rwidget_get_child(order_rows_[i], 0);
    ui::set_text(label, rows[i].text);
    set_color(label, rows[i].tone);
    set_enabled(Rwidget_get_child(order_rows_[i], 1), !vm().busy());
  }
}

void GameView::apply_advisor(const game::Texts& t, const GameState& g) {
  game::GameViewModel& m = vm();
  // Practice packs start without the advisor, so it is not offered.
  set_visible(find(n::kAdvisorBox), !g.known_scenario);
  const game::AdvisorView a = game::advisor_view(t, g, m.busy());
  set_label(find(n::kAdvPoints), a.points);
  set_enabled(find(n::kAdvAsk), a.can_ask);
  Rwidget_set_tooltip(find(n::kAdvAsk), a.ask_tooltip.c_str());
  std::vector<game::Line> read;
  if (!a.has_read) read.push_back({a.intro, game::Tone::Muted});
  if (a.has_read) read.push_back(a.stance);
  if (!a.detail.empty()) read.push_back({a.detail, game::Tone::Plain});
  adv_read_.set(read);
  set_visible(find(n::kAdvApply), a.actionable);
  std::vector<game::Line> reasons = a.reasons;
  if (a.has_read) reasons.push_back({a.disclaimer, game::Tone::Muted});
  adv_reasons_.set(reasons);
}

void GameView::apply_lists(const game::Texts& t, const GameState& g) {
  const game::NewsView nv = game::news_view(t, g);
  std::vector<game::Line> news;
  for (const auto& u : nv.upcoming) news.push_back({u, game::Tone::Info});
  if (!nv.upcoming_note.empty()) news.push_back({nv.upcoming_note, game::Tone::Muted});
  for (const auto& c : nv.calendar) news.push_back({c, game::Tone::Info});
  news.insert(news.end(), nv.items.begin(), nv.items.end());
  news.push_back({nv.note, game::Tone::Muted});
  news_.set(news);
  auto feed = game::feed_lines(t, g, vm().series_names());
  if (feed.empty()) feed.push_back({t.tr("feed.empty"), game::Tone::Muted});
  feed_.set(feed);
}

}  // namespace tradelab::desktop::view
