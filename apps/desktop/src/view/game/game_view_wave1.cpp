// GameView: wave 1 of add-practice-and-advanced-strategies (web TifField, OrderTicket's "Protect with"
// and protective tabs, TrailNotice, OrdersPanel's OCO link, ComboWorking, RightRail's portfolio,
// PortfolioPanel, HeatmapPanel, LiveCombos and ConfirmDialog). Glue only: the viewmodels carry every
// text and the engine's numbers.
#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_layout.h>
#include <raven/raven_widget.h>

#include "view/game/game_view.hpp"
#include "view/game/scene_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/game/game_widgets.hpp"
#include "viewmodel/game/wave1_widgets.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop::view {

namespace w = names::wave1;
using game::GameViewModel;

namespace {

void select_item(Rid combo, int index) {
  if (combo != R_INVALID_ID && Rcombo_box_get_selected(combo) != index) Rcombo_box_set_selected(combo, index);
}

void check_box(Rid box, bool on) {
  if (box != R_INVALID_ID && (Rcheckbox_is_checked(box) != 0) != on) Rcheckbox_set_checked(box, on ? R_TRUE : R_FALSE);
}

std::string joined(const std::vector<std::string>& items) {
  std::string out;
  for (const auto& i : items) out += i + "\n";
  return out;
}

std::string kv(const std::vector<std::pair<std::string, std::string>>& rows) {
  std::string out;
  for (const auto& [k, v] : rows) out += (out.empty() ? "" : "\n") + k + (v.empty() ? "" : ": " + v);
  return out;
}

std::string lines(const std::vector<std::string>& items) {
  std::string out;
  for (const auto& i : items) out += (out.empty() ? "" : "\n") + i;
  return out;
}

std::string marked(bool on, const std::string& text) { return (on ? "\xE2\x96\xB8 " : "") + text; }

std::string heatmap_name(std::string_view prefix, std::string_view part) { return std::string(prefix) + "-" + std::string(part); }

}  // namespace

void GameView::bind_wave1() {
  cb_.choose(find(w::kTif), [this](int i) {
    vm().ticket.tif_choice = game::kTifChoices.at(static_cast<std::size_t>(i));
    vm().touch();
  });
  cb_.text(find(w::kTifBars), [this](const std::string& text) {
    vm().ticket.tif_bars = text;
    vm().touch();
  });
  cb_.choose(find(w::kOptTif), [this](int i) {
    vm().option_ticket.tif_choice = game::kTifChoices.at(static_cast<std::size_t>(i));
    vm().touch();
  });
  cb_.text(find(w::kOptTifBars), [this](const std::string& text) {
    vm().option_ticket.tif_bars = text;
    vm().touch();
  });
  const auto ticket_edit = [this](const std::function<void(game::TicketViewModel&)>& edit) {
    edit(vm().ticket);
    vm().touch();
  };
  cb_.click(find(w::kProtectFixed), [ticket_edit] { ticket_edit([](game::TicketViewModel& t) { t.protect_with_trail = false; }); });
  cb_.click(find(w::kProtectTrail), [ticket_edit] {
    ticket_edit([](game::TicketViewModel& t) {
      t.protect_with_trail = true;
      t.stop.clear();
    });
  });
  cb_.click(find(w::kPlanTrailPrice), [ticket_edit] { ticket_edit([](game::TicketViewModel& t) { t.plan_trail_by_pct = false; }); });
  cb_.click(find(w::kPlanTrailPct), [ticket_edit] { ticket_edit([](game::TicketViewModel& t) { t.plan_trail_by_pct = true; }); });
  cb_.text(find(w::kPlanTrail), [this](const std::string& text) {
    (vm().ticket.plan_trail_by_pct ? vm().ticket.plan_trail_pct : vm().ticket.plan_trail) = text;
    vm().touch();
  });
  const std::array<std::pair<std::string_view, game::ProtectTab>, 3> tabs = {
      {{w::kTabStop, game::ProtectTab::Stop}, {w::kTabTrail, game::ProtectTab::Trail}, {w::kTabTarget, game::ProtectTab::Target}}};
  for (const auto& [name, tab] : tabs) {
    cb_.click(find(name), [ticket_edit, tab = tab] { ticket_edit([tab](game::TicketViewModel& t) { t.protect_tab = tab; }); });
  }
  cb_.click(find(w::kTrailNoticePlace), [this] { vm().trail_notice_place(); });
  cb_.click(find(w::kTrailNoticeDismiss), [this] { vm().trail_notice_dismiss(); });
  cb_.click(find(w::kLink), [this] { vm().link_oco(); });
  cb_.click(find(w::kWorkingCancel), [this] { vm().cancel_working_legs(); });
  cb_.click(find(w::kRailPortfolioOpen), [this] { vm().open_portfolio(); });
  cb_.click(find(w::kConfirmNo), [this] { vm().confirm_answer(false); });
  cb_.click(find(w::kConfirmYes), [this] { vm().confirm_answer(true); });
  cb_.click(find(w::kPortfolioHedge), [this] { vm().hedge_delta(); });
  cb_.click(find(w::kPortfolioClose), [this] { vm().close_modal(); });
  bind_heatmaps();
  bind_live();
}

void GameView::bind_heatmaps() {
  cb_.click(find(w::kBuilderHeatmapToggle), [this] {
    vm().builder.heatmap_open = !vm().builder.heatmap_open;
    vm().builder_changed();
  });
  for (const std::string_view prefix : {w::kBuilderHeatmap, w::kPortfolioHeatmap}) {
    cb_.click(find(heatmap_name(prefix, "view")), [this] {
      vm().heatmap_table = !vm().heatmap_table;
      vm().touch();
    });
  }
  const Rid builder = find(heatmap_name(w::kBuilderHeatmap, "grid"));
  const Rid portfolio = find(heatmap_name(w::kPortfolioHeatmap, "grid"));
  Rcustom_set_paint_callback(builder, on_builder_heatmap_paint, this);
  Rcustom_set_paint_callback(portfolio, on_portfolio_heatmap_paint, this);
  Rwidget_set_event_callback(builder, on_builder_heatmap_event, this);
  Rwidget_set_event_callback(portfolio, on_portfolio_heatmap_event, this);
}

void GameView::bind_live() {
  cb_.choose(find(w::kLiveTo), [this](int i) { vm().set_roll_target(i); });
  cb_.check(find(w::kLiveRecenter), [this](bool on) { vm().set_roll_recenter(on); });
  cb_.check(find(w::kLiveLimit), [this](bool on) { vm().set_roll_limit_ioc(on); });
  cb_.click(find(w::kLiveHedge), [this] { vm().adjust_add_hedge(); });
  cb_.click(find(w::kLiveSend), [this] { vm().live_send(); });
  cb_.click(find(w::kLiveCancel), [this] { vm().live_close(); });
}

void GameView::apply_tif(const game::TifView& v, std::string_view combo, std::string_view label, std::string_view bars_row,
                         std::string_view bars_label, std::string_view bars, std::string_view hint, const std::string& bars_text) {
  set_label(find(label), v.label);
  Rwidget_set_accessible_name(find(combo), v.label.c_str());
  const std::string key = joined(v.items) + joined(v.blocked);
  if (tif_items_[std::string(combo)] != key) {
    tif_items_[std::string(combo)] = key;
    const Rid c = find(combo);
    Rcombo_box_clear_items(c);
    for (std::size_t i = 0; i < v.items.size(); ++i) {
      // A choice the order type cannot take is named with its reason (the engine would refuse it).
      const std::string item = v.blocked[i].empty() ? v.items[i] : v.items[i] + " \xE2\x80\x94 \xE2\x9C\x95";
      Rcombo_box_add_item(c, item.c_str());
    }
  }
  select_item(find(combo), v.index);
  set_visible(find(bars_row), v.bars);
  set_label(find(bars_label), v.bars_label);
  Rwidget_set_accessible_name(find(bars), v.bars_label.c_str());
  if (Rwidget_is_focused(find(bars)) == 0) ui::set_text(find(bars), bars_text);
  set_label(find(hint), v.hint);
  set_color(find(hint), v.error ? game::Tone::Down : game::Tone::Muted);
}

void GameView::apply_wave1_ticket(const game::Texts& t, const game::TicketView& v) {
  GameViewModel& m = vm();
  apply_tif(v.tif, w::kTif, w::kTifLabel, w::kTifBarsRow, w::kTifBarsLabel, w::kTifBars, w::kTifHint, m.ticket.tif_bars);
  const bool trail = m.ticket.protect_with_trail;
  set_label(find(w::kProtectWithLabel), v.protect_with);
  Rwidget_set_accessible_name(find(w::kProtectWith), v.protect_with.c_str());
  ui::set_text(find(w::kProtectFixed), marked(!trail, v.protect_fixed));
  ui::set_text(find(w::kProtectTrail), marked(trail, v.protect_trail));
  set_visible(find(w::kStopBox), !trail);
  set_visible(find(w::kPlanTrailBox), trail);
  const bool pct = m.ticket.plan_trail_by_pct;
  ui::set_text(find(w::kPlanTrailPrice), marked(!pct, t.tr("ticket.trailByPrice")));
  ui::set_text(find(w::kPlanTrailPct), marked(pct, t.tr("ticket.trailByPct")));
  const Rid plan = find(w::kPlanTrail);
  if (Rwidget_is_focused(plan) == 0) ui::set_text(plan, pct ? m.ticket.plan_trail_pct : m.ticket.plan_trail);
  Rtext_input_set_placeholder(plan, t.tr(pct ? "ticket.trailPct" : "ticket.trail").c_str());
  Rwidget_set_accessible_name(plan, t.tr(pct ? "ticket.trailPct" : "ticket.trail").c_str());
  set_label(find(w::kPlanTrailNote), v.trail_planned);
  const auto tab = m.ticket.protect_tab;
  ui::set_text(find(w::kTabStop), marked(tab == game::ProtectTab::Stop, v.tabs.at(0)));
  ui::set_text(find(w::kTabTrail), marked(tab == game::ProtectTab::Trail, v.tabs.at(1)));
  ui::set_text(find(w::kTabTarget), marked(tab == game::ProtectTab::Target, v.tabs.at(2)));
  Rwidget_set_accessible_name(find(w::kTabs), t.tr("ticket.protect").c_str());
  set_visible(find(w::kStopGroup), tab == game::ProtectTab::Stop);
  set_visible(find(w::kTrailGroup), tab == game::ProtectTab::Trail);
  set_visible(find(w::kTargetGroup), tab == game::ProtectTab::Target);
  set_label(find(w::kTrailRule), v.trail_rule);
  const auto notice = m.trail_notice();
  set_visible(find(w::kTrailNotice), notice.has_value());
  if (notice) {
    set_label(find(w::kTrailNoticeText), notice->text);
    ui::set_text(find(w::kTrailNoticePlace), notice->place);
    ui::set_text(find(w::kTrailNoticeDismiss), notice->dismiss);
  }
}

void GameView::apply_wave1_option(const game::OptionTicketView& v) {
  apply_tif(v.tif, w::kOptTif, w::kOptTifLabel, w::kOptTifBarsRow, w::kOptTifBarsLabel, w::kOptTifBars, w::kOptTifHint,
            vm().option_ticket.tif_bars);
}

void GameView::apply_wave1_book(const game::Texts& /*t*/) {
  GameViewModel& m = vm();
  const auto link = m.link_view();
  set_visible(find(w::kLinkRow), link.shown);
  ui::set_text(find(w::kLink), link.button);
  set_enabled(find(w::kLink), link.can_link);
  set_label(find(w::kLinkHint), link.hint);
  const auto working = m.working_combo();
  set_visible(find(w::kWorking), working.has_value());
  if (working) {
    set_label(find(w::kWorkingTitle), "\xE2\x8F\xB3 " + working->title);
    set_label(find(w::kWorkingText), working->text);
    ui::set_text(find(w::kWorkingCancel), working->cancel);
    set_enabled(find(w::kWorkingCancel), !m.busy());
  }
  const auto rail = m.rail_portfolio_view();
  set_label(find(w::kRailPortfolioTitle), rail.title);
  set_label(find(w::kRailPortfolioTotals), kv(rail.totals));
  set_visible(find(w::kRailPortfolioTotals), !rail.totals.empty());
  set_label(find(w::kRailPortfolioWarn), lines(rail.warnings));
  set_visible(find(w::kRailPortfolioWarn), !rail.warnings.empty());
  ui::set_text(find(w::kRailPortfolioOpen), rail.open);
}

void GameView::apply_heatmap(const game::Texts& t, std::string_view prefix, const std::optional<game::WhatIfHeatmap>& map, bool shown) {
  const Rid box = find(prefix);
  set_visible(box, shown && map.has_value());
  if (!shown || !map) return;
  GameViewModel& m = vm();
  const auto v = game::heatmap_view(t, *map, m.heatmap_table, m.heatmap_focus_t, m.heatmap_focus_p);
  set_label(find(heatmap_name(prefix, "title")), v.title + "\n" + v.caption);
  const Rid grid = find(heatmap_name(prefix, "grid"));
  set_visible(grid, !v.table);
  Rwidget_set_accessible_name(grid, v.title.c_str());
  Rwidget_set_accessible_description(grid, v.focus.c_str());
  std::vector<game::Line> rows;
  if (v.table) {
    std::string head = t.tr("heatmap.bars") + " \\ " + t.tr("heatmap.price") + ":";
    for (const auto& p : v.prices) head += "  " + p;
    rows.push_back({head, game::Tone::Muted});
    for (std::size_t r = 0; r < v.texts.size(); ++r) {
      std::string line = v.times.at(r) + ":";
      for (const auto& c : v.texts[r]) line += "  " + c;
      rows.push_back({line, game::Tone::Plain});
    }
  }
  heatmap_tables_[std::string(prefix)] = LineList(find(heatmap_name(prefix, "table")), 11);
  heatmap_tables_[std::string(prefix)].set(rows);
  set_label(find(heatmap_name(prefix, "focus")), v.focus);
  set_label(find(heatmap_name(prefix, "legend")), v.legend);
  ui::set_text(find(heatmap_name(prefix, "view")), v.toggle);
  Rwidget_invalidate(grid);
}

void GameView::apply_wave1_builder(const game::Texts& t) {
  GameViewModel& m = vm();
  const bool has = m.builder.whatif() && m.builder.whatif()->ok();
  set_visible(find(w::kBuilderHeatmapToggle), has);
  ui::set_text(find(w::kBuilderHeatmapToggle), "\xE2\x96\xA6 " + t.tr("heatmap.open"));
  apply_heatmap(t, w::kBuilderHeatmap, m.builder_heatmap(), has && m.builder.heatmap_open);
  apply_live(t);
}

void GameView::apply_live(const game::Texts& t) {
  GameViewModel& m = vm();
  const auto combos = m.live_combos();
  set_visible(find(w::kLive), !combos.empty());
  set_label(find(w::kLiveTitle), t.tr("combo.live") + " \xC2\xB7 " + t.tr("roll.button"));
  std::string key;
  for (const auto& c : combos) key += c.id + c.text + "\n";
  if (key != live_key_) {
    live_key_ = key;
    clear_rows(find(w::kLiveCombos), live_rows_);
    for (const auto& c : combos) {
      const Rid row = Rwidget_create(R_WIDGET_WRAP_BOX);
      const Rid label = Rlabel_create(c.text.c_str());
      Rlabel_set_text_size(label, 12);
      Rlabel_set_wrap(label, R_TRUE);
      Rwidget_add_child(row, label);
      const Rid roll = Rbutton_create(("\xE2\x9F\xB3 " + t.tr("roll.button")).c_str());
      Rwidget_set_name(roll, ("game-live-roll-" + c.id).c_str());
      const std::string id = c.id;
      cb_.click(roll, [this, id] { vm().open_roll(id); });
      Rwidget_add_child(row, roll);
      const Rid adjust = Rbutton_create(t.tr("adjust.button").c_str());
      Rwidget_set_name(adjust, ("game-live-adjust-" + c.id).c_str());
      cb_.click(adjust, [this, id] { vm().open_adjust(id); });
      Rwidget_add_child(row, adjust);
      Rwidget_add_child(find(w::kLiveCombos), row);
      live_rows_.push_back(row);
    }
  }
  apply_live_panel(t, m.live_view());
}

void GameView::apply_live_panel(const game::Texts& /*t*/, const GameViewModel::LiveView& v) {
  set_visible(find(w::kLivePanel), v.shown);
  if (!v.shown) return;
  set_label(find(w::kLivePanelTitle), v.title);
  set_label(find(w::kLiveNone), v.none);
  set_visible(find(w::kLiveNone), !v.none.empty());
  const Rid to = find(w::kLiveTo);
  set_visible(to, v.roll && !v.targets.empty());
  if (joined(v.targets) != live_targets_key_) {
    live_targets_key_ = joined(v.targets);
    Rcombo_box_clear_items(to);
    for (const auto& i : v.targets) Rcombo_box_add_item(to, i.c_str());
  }
  select_item(to, v.target_index);
  ui::set_text(find(w::kLiveRecenter), v.recenter);
  ui::set_text(find(w::kLiveLimit), v.limit_ioc);
  set_visible(find(w::kLiveRecenter), v.roll && !v.targets.empty());
  set_visible(find(w::kLiveLimit), v.roll && !v.targets.empty());
  check_box(find(w::kLiveRecenter), v.recenter_on);
  check_box(find(w::kLiveLimit), v.limit_on);
  if (joined(v.held) != live_held_key_) {
    live_held_key_ = joined(v.held);
    clear_rows(find(w::kLiveHeld), live_held_rows_);
    for (std::size_t i = 0; i < v.held.size(); ++i) {
      const Rid row = Rwidget_create(R_WIDGET_WRAP_BOX);
      const Rid label = Rlabel_create(v.held[i].c_str());
      Rlabel_set_text_size(label, 12);
      Rwidget_add_child(row, label);
      const Rid close = Rbutton_create(v.close_leg.c_str());
      Rwidget_set_name(close, ("game-live-close-leg-" + std::to_string(i)).c_str());
      cb_.click(close, [this, i] { vm().adjust_close_leg(i); });
      Rwidget_add_child(row, close);
      Rwidget_add_child(find(w::kLiveHeld), row);
      live_held_rows_.push_back(row);
    }
  }
  set_visible(find(w::kLiveHeld), !v.roll);
  set_visible(find(w::kLiveHedge), !v.roll);
  ui::set_text(find(w::kLiveHedge), v.add_hedge);
  set_label(find(w::kLiveHedgeNone), v.hedge_none);
  set_visible(find(w::kLiveHedgeNone), !v.hedge_none.empty());
  live_orders_ = LineList(find(w::kLiveOrders), 11);
  live_orders_.set(v.orders);
  set_label(find(w::kLiveTotals), kv(v.totals));
  ui::set_text(find(w::kLiveSend), v.send);
  set_enabled(find(w::kLiveSend), v.can_send);
  ui::set_text(find(w::kLiveCancel), vm().texts().tr("orders.linkNo"));
}

void GameView::apply_confirm() {
  const auto v = vm().confirm_view();
  set_label(find(w::kConfirmTitle), v.title);
  set_color(find(w::kConfirmTitle), v.danger ? game::Tone::Down : game::Tone::Attention);
  set_label(find(w::kConfirmText), v.text);
  ui::set_text(find(w::kConfirmNo), v.no);
  ui::set_text(find(w::kConfirmYes), v.yes);
}

void GameView::apply_portfolio(const game::Texts& t) {
  GameViewModel& m = vm();
  const auto v = m.portfolio_view();
  set_label(find(w::kPortfolioTitle), v.title);
  set_label(find(w::kPortfolioEmpty), v.empty);
  set_visible(find(w::kPortfolioEmpty), !v.empty.empty());
  std::vector<game::Line> rows;
  if (!v.rows.empty()) {
    std::string head;
    for (const auto& h : v.header) head += (head.empty() ? "" : " \xC2\xB7 ") + h;
    rows.push_back({head, game::Tone::Muted});
  }
  for (std::size_t i = 0; i < v.rows.size(); ++i) {
    std::string line;
    for (const auto& c : v.rows[i]) line += (line.empty() ? "" : " \xC2\xB7 ") + c;
    rows.push_back({line, v.row_tones.at(i)});
  }
  portfolio_rows_ = LineList(find(w::kPortfolioRows), 12);
  portfolio_rows_.set(rows);
  set_label(find(w::kPortfolioPerUnit), v.per_unit);
  set_label(find(w::kPortfolioTotals), kv(v.totals));
  set_label(find(w::kPortfolioLimitsTitle), v.limits_title);
  std::vector<game::Line> limits;
  if (!v.no_limits.empty()) limits.push_back({v.no_limits, game::Tone::Muted});
  for (const auto& [text, exceeded] : v.limits) limits.push_back({text, exceeded ? game::Tone::Attention : game::Tone::Plain});
  portfolio_limits_ = LineList(find(w::kPortfolioLimits), 12);
  portfolio_limits_.set(limits);
  set_label(find(w::kPortfolioWarnings), lines(v.warnings));
  set_visible(find(w::kPortfolioWarnings), !v.warnings.empty());
  ui::set_text(find(w::kPortfolioHedge), v.hedge_label);
  set_enabled(find(w::kPortfolioHedge), v.can_hedge);
  set_label(find(w::kPortfolioHedgeNote), v.hedge_note);
  ui::set_text(find(w::kPortfolioClose), t.tr("portfolio.close"));
  apply_heatmap(t, w::kPortfolioHeatmap, m.portfolio_heatmap(), !v.rows.empty());
}

// ---- heatmap painting and pointer ----

void GameView::paint_heatmap(RPainter* painter, float width, float height, const std::optional<game::WhatIfHeatmap>& map) {
  if (!map) return;
  GameViewModel& m = vm();
  const auto v = game::heatmap_view(m.texts(), *map, false, m.heatmap_focus_t, m.heatmap_focus_p);
  paint_scene(painter, game::heatmap_scene(v, width, height, m.heatmap_focus_t, m.heatmap_focus_p));
}

void GameView::on_builder_heatmap_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* self) {
  auto* view = static_cast<GameView*>(self);
  view->paint_heatmap(painter, width, height, view->vm().builder_heatmap());
}

void GameView::on_portfolio_heatmap_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* self) {
  auto* view = static_cast<GameView*>(self);
  view->paint_heatmap(painter, width, height, view->vm().portfolio_heatmap());
}

// The hovered or clicked cell is read on the help line; the arrow keys move the focused cell.
int GameView::heatmap_event(Rid widget, const REvent* e, const std::optional<game::WhatIfHeatmap>& map) {
  if (!map || map->pnl.empty()) return R_FALSE;
  GameViewModel& m = vm();
  const auto v = game::heatmap_view(m.texts(), *map, false, 0, 0);
  if (e->type == R_EVENT_MOUSE_MOVE || e->type == R_EVENT_MOUSE_DOWN) {
    float x = 0, y = 0, width = 0, height = 0;
    Rwidget_get_rect(widget, &x, &y, &width, &height);
    const auto hit = game::heatmap_hit(v, width, height, e->local_x, e->local_y);
    if (!hit) return R_FALSE;
    m.heatmap_focus_t = hit->first;
    m.heatmap_focus_p = hit->second;
    m.touch();
    return e->type == R_EVENT_MOUSE_DOWN ? R_TRUE : R_FALSE;
  }
  if (e->type != R_EVENT_KEY_DOWN) return R_FALSE;
  const std::size_t rows = v.cells.size();
  const std::size_t cols = v.cells.front().size();
  if (e->key == R_KEY_LEFT && m.heatmap_focus_p > 0) --m.heatmap_focus_p;
  else if (e->key == R_KEY_RIGHT && m.heatmap_focus_p + 1 < cols) ++m.heatmap_focus_p;
  else if (e->key == R_KEY_UP && m.heatmap_focus_t > 0) --m.heatmap_focus_t;
  else if (e->key == R_KEY_DOWN && m.heatmap_focus_t + 1 < rows) ++m.heatmap_focus_t;
  else return R_FALSE;
  m.touch();
  return R_TRUE;
}

int GameView::on_builder_heatmap_event(Rid widget, const REvent* e, void* self) {
  auto* view = static_cast<GameView*>(self);
  return view->heatmap_event(widget, e, view->vm().builder_heatmap());
}

int GameView::on_portfolio_heatmap_event(Rid widget, const REvent* e, void* self) {
  auto* view = static_cast<GameView*>(self);
  return view->heatmap_event(widget, e, view->vm().portfolio_heatmap());
}

}  // namespace tradelab::desktop::view
