// GameView: the strategy builder tab, the incomplete-combo notice and the lab dialog (web
// StrategyBuilder.svelte, ComboNotice.svelte, WhatIfLab.svelte). Glue only: game::BuilderView and
// game::LabView carry every text, the engine's numbers and the payoff chart's display list.
#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_layout.h>
#include <raven/raven_widget.h>

#include <algorithm>

#include "view/game/game_view.hpp"
#include "view/game/scene_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/game/builder_widgets.hpp"
#include "viewmodel/game/game_widgets.hpp"

namespace tradelab::desktop::view {

namespace b = names::builder;
using game::GameViewModel;
using game::Modal;

namespace {

void select(Rid combo, int index) {
  if (combo != R_INVALID_ID && Rcombo_box_get_selected(combo) != index) Rcombo_box_set_selected(combo, index);
}

void check(Rid box, bool on) {
  if (box != R_INVALID_ID && (Rcheckbox_is_checked(box) != 0) != on) Rcheckbox_set_checked(box, on ? R_TRUE : R_FALSE);
}

std::string kv(const std::vector<std::pair<std::string, std::string>>& rows) {
  std::string out;
  for (const auto& [k, v] : rows) out += (out.empty() ? "" : "\n") + k + ": " + v;
  return out;
}

void fill(Rid combo, const std::vector<std::string>& items) {
  Rcombo_box_clear_items(combo);
  for (const auto& i : items) Rcombo_box_add_item(combo, i.c_str());
}

std::string joined(const std::vector<std::string>& items) {
  std::string out;
  for (const auto& i : items) out += i + "\n";
  return out;
}

Rid row_child(Rid leg, int row, int index) { return Rwidget_get_child(Rwidget_get_child(leg, row), index); }

}  // namespace

void GameView::bind_builder() {
  builder_preview_ = LineList(find(b::kPreview), 12);
  builder_table_ = LineList(find(b::kTable), 11);
  const auto changed = [this](const std::function<void(GameViewModel&, const GameState&)>& edit) {
    if (vm().state() == nullptr) return;
    edit(vm(), *vm().state());
    vm().builder_changed();
  };
  cb_.click(find(b::kTab), [this] { vm().set_tab(game::Tab::Builder); });
  cb_.choose(find(b::kExpiry), [changed](int i) { changed([i](GameViewModel& m, const GameState& g) { m.builder.set_expiry_index(g, i); }); });
  cb_.choose(find(b::kFar), [changed](int i) { changed([i](GameViewModel& m, const GameState& g) { m.builder.set_far_index(g, i); }); });
  cb_.choose(find(b::kTif), [changed](int i) {
    changed([i](GameViewModel& m, const GameState&) { m.builder.tif_choice = game::kTifChoices.at(static_cast<std::size_t>(i)); });
  });
  cb_.text(find(b::kTifBars), [changed](const std::string& text) {
    changed([text](GameViewModel& m, const GameState&) { m.builder.tif_bars = text; });
  });
  cb_.text(find(b::kQty), [this](const std::string& text) { vm().builder.qty = text; });
  cb_.commit(find(b::kQty), [this, changed] {
    changed([this](GameViewModel& m, const GameState&) { m.builder.apply_qty(vm().texts().locale()); });
  });
  cb_.check(find(b::kShort), [this, changed](bool on) {
    if (on == vm().builder.short_side) return;
    changed([this](GameViewModel& m, const GameState& g) { m.builder.toggle_short(g, vm().texts().locale()); });
  });
  for (std::size_t i = 0; i < game::kTemplates.size(); ++i) {
    const game::Template id = game::kTemplates[i];
    cb_.click(find(std::string(b::kTemplatePrefix) + std::to_string(i)), [this, changed, id] {
      changed([this, id](GameViewModel& m, const GameState& g) { m.builder.pick(g, id, vm().texts().locale()); });
    });
  }
  cb_.click(find(b::kAddLeg), [this, changed] {
    changed([this](GameViewModel& m, const GameState& g) { m.builder.add_leg(g, vm().texts().locale()); });
  });
  cb_.check(find(b::kUseLong), [changed](bool on) { changed([on](GameViewModel& m, const GameState&) { m.builder.use_long = on; }); });
  cb_.text(find(b::kSizeRisk), [this](const std::string& text) {
    vm().builder.risk_pct = text;
    vm().builder_changed();
  });
  cb_.click(find(b::kSizeApply), [changed, this] {
    changed([this](GameViewModel& m, const GameState& g) { m.builder.apply_size(g, vm().texts().locale()); });
  });
  cb_.click(find(b::kOpenLab), [this] { vm().open_lab_builder(); });
  cb_.click(find(b::kLabPositions), [this] { vm().open_lab_positions(); });
  cb_.click(find(b::kSubmit), [this] { vm().builder_submit(); });
  Rcustom_set_paint_callback(find(b::kPayoff), on_builder_paint, this);
  cb_.click(find(b::kNoticeKeep), [this] { vm().combo_keep(); });
  cb_.click(find(b::kNoticeClose), [this] { vm().combo_close(); });
  cb_.click(find(b::kCloseNo), [this] { vm().combo_close_confirm(false); });
  cb_.click(find(b::kCloseYes), [this] { vm().combo_close_confirm(true); });
  cb_.click(find(b::kLabClose), [this] { vm().close_modal(); });
  lab_ = std::make_unique<LabPanel>(root_, std::string(b::kLabPrefix), cb_, [this]() -> game::LabViewModel& { return vm().lab; },
                                    [this] { vm().lab_changed(); });
  lab_->bind();
}

void GameView::on_builder_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* self) {
  const auto& w = static_cast<GameView*>(self)->vm().builder.whatif();
  if (w && w->ok()) paint_scene(painter, game::payoff_scene(*w->value, width, height));
}

void GameView::apply_builder(const game::Texts& t, const GameState& g) {
  GameViewModel& m = vm();
  ui::set_text(find(b::kTab), std::string(m.tab() == game::Tab::Builder ? "\xE2\x96\xB8 " : "") + t.tr("builder.tab"));
  if (m.tab() != game::Tab::Builder || !g.options.enabled) return;
  const game::BuilderView v = m.builder_view();
  set_label(find(b::kTitle), v.title);
  set_visible(find(b::kLabPositions), !v.lab_positions.empty());
  ui::set_text(find(b::kLabPositions), v.lab_positions);
  set_visible(find(b::kNone), !v.available);
  set_label(find(b::kNone), v.available ? std::string() : v.problem);
  set_visible(find(b::kBody), v.available);
  if (!v.available) return;
  set_label(find(b::kExpiryLabel), t.tr("builder.expiry"));
  if (joined(v.expiries) != expiries_key_) {
    expiries_key_ = joined(v.expiries);
    fill(find(b::kExpiry), v.expiries);
  }
  select(find(b::kExpiry), v.expiry_index);
  apply_builder_expiries(t, v);
  set_label(find(b::kQtyLabel), t.tr("opt.qty"));
  ui::set_text(find(b::kQty), m.builder.qty);
  ui::set_text(find(b::kShort), t.tr("builder.short"));
  check(find(b::kShort), m.builder.short_side);
  for (std::size_t i = 0; i < v.templates.size(); ++i) {
    const auto& tpl = v.templates[i];
    const Rid button = find(std::string(b::kTemplatePrefix) + std::to_string(i));
    ui::set_text(button, (tpl.selected ? "\xE2\x96\xB8 " : "") + tpl.label);
    set_enabled(button, tpl.enabled);
    Rwidget_set_accessible_description(button, tpl.enabled ? "" : v.write_locked.c_str());
  }
  set_label(find(b::kWriteLocked), v.write_locked);
  set_visible(find(b::kWriteLocked), !v.write_locked.empty());
  set_label(find(b::kCaption), v.legs.empty() ? std::string() : v.caption);
  build_leg_rows(v);
  apply_leg_rows(v);
  ui::set_text(find(b::kAddLeg), "+ " + t.tr("builder.addLeg"));
  set_enabled(find(b::kAddLeg), v.can_add_leg);
  set_visible(find("game-builder-use-long-row"), !v.use_long.empty());
  set_label(find("game-builder-use-long-text"), v.use_long);
  Rwidget_set_accessible_name(find(b::kUseLong), v.use_long.c_str());
  check(find(b::kUseLong), m.builder.use_long);
  set_label(find(b::kProblem), v.problem);
  set_visible(find(b::kProblem), !v.problem.empty());
  set_label(find(b::kNoAnalysis), v.no_analysis);
  set_visible(find(b::kNoAnalysis), !v.no_analysis.empty());
  builder_preview_.set(v.preview);
  set_label(find(b::kTotals), kv(v.totals));
  set_label(find(b::kAnalysis), kv(v.analysis));
  set_label(find(b::kHorizon), v.horizon);
  set_visible(find(b::kHorizon), !v.horizon.empty());
  set_label(find(b::kHorizonNote), v.horizon_note);
  set_visible(find(b::kHorizonNote), !v.horizon_note.empty());
  apply_sizing(t, v);
  set_visible(find(b::kPayoff), v.has_whatif);
  Rwidget_set_accessible_name(find(b::kPayoff), v.chart_summary.c_str());
  set_label(find(b::kLegend), v.chart_legend);
  set_visible(find(b::kLegend), v.has_whatif);
  builder_table_.set(v.table);
  set_visible(find(b::kOpenLab), v.has_whatif);
  ui::set_text(find(b::kOpenLab), "\xE2\x9A\x97 " + t.tr("builder.openLab"));
  set_label(find(b::kLegging), v.legging);
  ui::set_text(find(b::kSubmit), t.tr("builder.submit"));
  set_enabled(find(b::kSubmit), v.can_submit);
  Rwidget_invalidate(find(b::kPayoff));
}

// D3: the far expiry; D2: the combo's time in force when a leg is a limit.
void GameView::apply_builder_expiries(const game::Texts& /*t*/, const game::BuilderView& v) {
  set_visible(find(b::kFarRow), !v.far_expiries.empty());
  set_label(find(b::kFarLabel), v.far_label);
  Rwidget_set_accessible_name(find(b::kFar), v.far_label.c_str());
  if (joined(v.far_expiries) != far_key_) {
    far_key_ = joined(v.far_expiries);
    fill(find(b::kFar), v.far_expiries);
  }
  select(find(b::kFar), v.far_index);
  set_visible(find(b::kLimitTif), v.has_limit);
  if (!v.has_limit) return;
  set_label(find(b::kLimitNote), v.limit_note);
  set_label(find(b::kTifLabel), v.tif.label);
  Rwidget_set_accessible_name(find(b::kTif), v.tif.label.c_str());
  if (joined(v.tif.items) != tif_key_) {
    tif_key_ = joined(v.tif.items);
    fill(find(b::kTif), v.tif.items);
  }
  select(find(b::kTif), v.tif.index);
  set_visible(find(b::kTifBarsRow), v.tif.bars);
  set_label(find(b::kTifBarsLabel), v.tif.bars_label);
  Rwidget_set_accessible_name(find(b::kTifBars), v.tif.bars_label.c_str());
  const Rid bars = find(b::kTifBars);
  if (Rwidget_is_focused(bars) == 0) ui::set_text(bars, vm().builder.tif_bars);
  set_label(find(b::kTifHint), v.tif.hint);
  set_color(find(b::kTifHint), v.tif.error ? game::Tone::Down : game::Tone::Muted);
  set_label(find(b::kTifNote), v.tif_note);
}

namespace {

Rid add_combo(Rid row, const std::vector<std::string>& items, const std::string& name, float width) {
  const Rid c = Rwidget_create(R_WIDGET_COMBO_BOX);
  fill(c, items);
  Rwidget_set_accessible_name(c, name.c_str());
  if (width > 0) Rwidget_set_size(c, width, -1);
  Rwidget_add_child(row, c);
  return c;
}

Rid add_filler(Rid row) {
  const Rid spacer = Rwidget_create(R_WIDGET_SPACER);
  Rwidget_add_child(row, spacer);
  Rslot_set_fill(spacer, 1);
  return spacer;
}

game::LegKind kind_at(int k) {
  if (k == 0) return game::LegKind::Call;
  return k == 1 ? game::LegKind::Put : game::LegKind::Perp;
}

}  // namespace

// One row per leg: kind, side, strike (options), quantity, remove; and its no-bid warning. Rebuilt only
// when the rows' shape changes, so a quantity being typed keeps its field.
void GameView::build_leg_rows(const game::BuilderView& v) {
  std::string key = std::to_string(static_cast<int>(vm().texts().locale())) + "|" + joined(v.leg_expiries);
  for (const auto& l : v.legs) key += std::to_string(l.kind == 2 ? 2 : 0) + joined(l.strikes) + (l.limit ? "L" : "M");
  if (key == legs_key_ && leg_rows_.size() == v.legs.size()) return;
  legs_key_ = key;
  clear_leg_rows();
  for (std::size_t i = 0; i < v.legs.size(); ++i) add_leg_row(v, i);
}

void GameView::clear_leg_rows() {
  for (const Rid leg : leg_rows_) {
    for (int r = 0; r < Rwidget_get_child_count(leg); ++r) {
      const Rid row = Rwidget_get_child(leg, r);
      for (int i = 0; i < Rwidget_get_child_count(row); ++i) cb_.erase(Rwidget_get_child(row, i));
    }
    Rwidget_destroy(leg);
  }
  leg_rows_.clear();
}

void GameView::add_leg_row(const game::BuilderView& v, std::size_t i) {
  const game::Texts t = vm().texts();
  const Rid leg = Rwidget_create(R_WIDGET_VBOX);
  const Rid row = Rwidget_create(R_WIDGET_HBOX);
  Rwidget_add_child(leg, row);
  cb_.choose(add_combo(row, v.kinds, t.tr("lab.kind"), 64), [this, i](int k) {
    if (vm().state() == nullptr) return;
    vm().builder.set_kind(*vm().state(), i, kind_at(k));
    vm().builder_changed();
  });
  cb_.choose(add_combo(row, v.sides, t.tr("lab.side"), 84), [this, i](int s) {
    vm().builder.set_side(i, s == 0);
    vm().builder_changed();
  });
  if (v.legs[i].kind == 2) {
    add_filler(row);
  } else {
    const Rid strike = add_combo(row, v.legs[i].strikes, t.tr("opt.strike"), 0);
    Rslot_set_fill(strike, 1);
    cb_.choose(strike, [this, i](int s) {
      if (vm().state() == nullptr) return;
      vm().builder.set_strike(*vm().state(), i, s);
      vm().builder_changed();
    });
  }
  const Rid qty = Rwidget_create(R_WIDGET_TEXT_INPUT);
  Rwidget_set_size(qty, 52, -1);
  Rwidget_set_accessible_name(qty, t.tr("opt.qty").c_str());
  Rwidget_add_child(row, qty);
  cb_.text(qty, [this, i](const std::string& text) { vm().builder.set_leg_qty(i, text); });
  cb_.commit(qty, [this] { vm().builder_changed(); });
  const Rid remove = Rbutton_create("\xE2\x9C\x95");  // ✕
  Rwidget_set_accessible_name(remove, t.tr("lab.remove").c_str());
  Rwidget_add_child(row, remove);
  cb_.click(remove, [this, i] {
    vm().builder.remove_leg(i);
    vm().builder_changed();
  });
  add_leg_extra(v, i, leg);
  const Rid warn = Rlabel_create(t.tr("builder.noBid").c_str());
  Rlabel_set_wrap(warn, R_TRUE);
  Rlabel_set_text_size(warn, 11);
  set_color(warn, game::Tone::Attention);
  const Rid warn_row = Rwidget_create(R_WIDGET_HBOX);
  Rwidget_add_child(warn_row, warn);
  Rslot_set_fill(warn, 1);
  Rwidget_add_child(leg, warn_row);
  Rwidget_add_child(find(b::kLegs), leg);
  leg_rows_.push_back(leg);
}

// D2/D3: the leg's expiry (two at most) and market or limit, on a line of their own (row 1).
void GameView::add_leg_extra(const game::BuilderView& v, std::size_t i, Rid leg) {
  const game::Texts t = vm().texts();
  const Rid row = Rwidget_create(R_WIDGET_HBOX);
  Rwidget_add_child(leg, row);
  const Rid expiry = add_combo(row, v.leg_expiries, t.tr("builder.legExpiry"), 0);
  Rslot_set_fill(expiry, 1);
  cb_.choose(expiry, [this, i](int e) {
    if (vm().state() == nullptr) return;
    vm().builder.set_leg_expiry(*vm().state(), i, e);
    vm().builder_changed();
  });
  cb_.choose(add_combo(row, v.leg_types, t.tr("builder.legType"), 96), [this, i](int k) {
    if (vm().state() == nullptr) return;
    vm().builder.set_leg_limit(*vm().state(), i, k == 1);
    vm().builder_changed();
  });
  const Rid limit = Rwidget_create(R_WIDGET_TEXT_INPUT);
  Rwidget_set_size(limit, 64, -1);
  Rwidget_set_accessible_name(limit, t.tr("builder.limitPremium").c_str());
  Rwidget_add_child(row, limit);
  cb_.text(limit, [this, i](const std::string& text) { vm().builder.set_leg_limit_text(i, text); });
  cb_.commit(limit, [this] { vm().builder_changed(); });
}

void GameView::apply_leg_rows(const game::BuilderView& v) {
  for (std::size_t i = 0; i < v.legs.size() && i < leg_rows_.size(); ++i) {
    const auto& l = v.legs[i];
    const Rid leg = leg_rows_[i];
    select(row_child(leg, 0, 0), l.kind);
    select(row_child(leg, 0, 1), l.side);
    if (l.kind != 2) select(row_child(leg, 0, 2), l.strike);
    const Rid qty = row_child(leg, 0, 3);
    if (Rwidget_is_focused(qty) == 0) ui::set_text(qty, l.qty);
    const Rid extra = Rwidget_get_child(leg, 1);
    set_visible(extra, l.kind != 2);
    set_visible(row_child(leg, 1, 0), v.leg_expiries.size() > 1);
    select(row_child(leg, 1, 0), l.expiry);
    select(row_child(leg, 1, 1), l.limit ? 1 : 0);
    const Rid limit = row_child(leg, 1, 2);
    set_visible(limit, l.limit);
    if (Rwidget_is_focused(limit) == 0) ui::set_text(limit, l.limit_text);
    set_visible(Rwidget_get_child(leg, 2), l.no_bid);
  }
}

void GameView::apply_sizing(const game::Texts& t, const game::BuilderView& v) {
  set_label(find(b::kSizeHeld), v.size_held);
  set_visible(find(b::kSizeHeld), !v.size_held.empty());
  set_visible(find(b::kSizing), v.sizing);
  Rwidget_set_accessible_name(find(b::kSizing), t.tr("builder.sizeTitle").c_str());
  set_label(find(b::kSizeLabel), v.size_label);
  Rwidget_set_accessible_name(find(b::kSizeRisk), v.size_label.c_str());
  const Rid risk = find(b::kSizeRisk);
  if (Rwidget_is_focused(risk) == 0) ui::set_text(risk, vm().builder.risk_pct);
  ui::set_text(find(b::kSizeApply), v.size_apply);
  set_enabled(find(b::kSizeApply), v.can_size);
  set_label(find(b::kSizeHint), v.size_hint);
  set_visible(find(b::kSizeHint), !v.size_hint.empty());
}

void GameView::apply_combo_notice() {
  const auto n = vm().combo_notice();
  set_visible(find(b::kNotice), n.has_value());
  if (!n) return;
  const game::Texts t = vm().texts();
  set_label(find(b::kNoticeTitle), n->title);
  set_label(find(b::kNoticeOpen), n->open_legs);
  set_label(find(b::kNoticeCancelled), n->cancelled_legs);
  set_label(find(b::kNoticeRisk), n->risk);
  set_visible(find(b::kNoticeRisk), !n->risk.empty());
  set_label(find(b::kNoticeLegging), n->legging);
  ui::set_text(find(b::kNoticeKeep), t.tr("combo.keep"));
  ui::set_text(find(b::kNoticeClose), t.tr("combo.close"));
}

void GameView::apply_lab_dialog(const game::Texts& t) {
  const Modal m = vm().modal();
  Rwidget_set_size(find("game-modal-box"), m == Modal::Lab ? 880.0F : 600.0F, -1);
  if (m == Modal::Lab) {
    set_label(find(b::kLabTitle), t.tr("lab.title"));
    ui::set_text(find(b::kLabClose), t.tr("lab.close"));
    lab_->apply(t);
  }
  if (m == Modal::ConfirmCombo) {
    set_label(find(b::kCloseTitle), t.tr("combo.closeTitle"));
    set_label(find(b::kCloseText), vm().combo_close_text());
    ui::set_text(find(b::kCloseNo), t.tr("combo.keep"));
    ui::set_text(find(b::kCloseYes), t.tr("combo.closeYes"));
  }
}

}  // namespace tradelab::desktop::view
