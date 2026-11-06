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
  set_visible(find(b::kUseLong), !v.use_long.empty());
  ui::set_text(find(b::kUseLong), v.use_long);
  check(find(b::kUseLong), m.builder.use_long);
  set_label(find(b::kProblem), v.problem);
  set_visible(find(b::kProblem), !v.problem.empty());
  set_label(find(b::kNoAnalysis), v.no_analysis);
  set_visible(find(b::kNoAnalysis), !v.no_analysis.empty());
  builder_preview_.set(v.preview);
  set_label(find(b::kTotals), kv(v.totals));
  set_label(find(b::kAnalysis), kv(v.analysis));
  set_visible(find(b::kPayoff), v.has_whatif);
  builder_table_.set(v.table);
  set_visible(find(b::kOpenLab), v.has_whatif);
  ui::set_text(find(b::kOpenLab), "\xE2\x9A\x97 " + t.tr("builder.openLab"));
  set_label(find(b::kLegging), v.legging);
  ui::set_text(find(b::kSubmit), t.tr("builder.submit"));
  set_enabled(find(b::kSubmit), v.can_submit);
  Rwidget_invalidate(find(b::kPayoff));
}

// One row per leg: kind, side, strike (options), quantity, remove; and its no-bid warning. Rebuilt only
// when the rows' shape changes, so a quantity being typed keeps its field.
void GameView::build_leg_rows(const game::BuilderView& v) {
  std::string key = std::to_string(static_cast<int>(vm().texts().locale())) + "|" + joined(v.strikes);
  for (const auto& l : v.legs) key += std::to_string(l.kind == 2 ? 2 : 0);
  if (key == legs_key_ && leg_rows_.size() == v.legs.size()) return;
  legs_key_ = key;
  const Rid box = find(b::kLegs);
  for (const Rid leg : leg_rows_) {
    for (int r = 0; r < Rwidget_get_child_count(leg); ++r) {
      const Rid row = Rwidget_get_child(leg, r);
      for (int i = 0; i < Rwidget_get_child_count(row); ++i) cb_.erase(Rwidget_get_child(row, i));
    }
    Rwidget_destroy(leg);
  }
  leg_rows_.clear();
  for (std::size_t i = 0; i < v.legs.size(); ++i) {
    const Rid leg = Rwidget_create(R_WIDGET_VBOX);
    const Rid row = Rwidget_create(R_WIDGET_HBOX);
    Rwidget_add_child(leg, row);
    const auto combo = [&](const std::vector<std::string>& items, const char* name, float width) {
      const Rid c = Rwidget_create(R_WIDGET_COMBO_BOX);
      fill(c, items);
      Rwidget_set_accessible_name(c, name);
      if (width > 0) Rwidget_set_size(c, width, -1);
      Rwidget_add_child(row, c);
      return c;
    };
    const game::Texts t = vm().texts();
    const Rid kind = combo(v.kinds, t.tr("lab.kind").c_str(), 64);
    const Rid side = combo(v.sides, t.tr("lab.side").c_str(), 84);
    cb_.choose(kind, [this, i](int k) {
      if (vm().state() == nullptr) return;
      vm().builder.set_kind(*vm().state(), i, k == 0 ? game::LegKind::Call : (k == 1 ? game::LegKind::Put : game::LegKind::Perp));
      vm().builder_changed();
    });
    cb_.choose(side, [this, i](int s) {
      vm().builder.set_side(i, s == 0);
      vm().builder_changed();
    });
    if (v.legs[i].kind != 2) {
      const Rid strike = combo(v.strikes, t.tr("opt.strike").c_str(), 0);
      Rslot_set_fill(strike, 1);
      cb_.choose(strike, [this, i](int s) {
        if (vm().state() == nullptr) return;
        vm().builder.set_strike(*vm().state(), i, s);
        vm().builder_changed();
      });
    } else {
      const Rid spacer = Rwidget_create(R_WIDGET_SPACER);
      Rwidget_add_child(row, spacer);
      Rslot_set_fill(spacer, 1);
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
    const Rid warn = Rlabel_create(t.tr("builder.noBid").c_str());
    Rlabel_set_wrap(warn, R_TRUE);
    Rlabel_set_text_size(warn, 11);
    set_color(warn, game::Tone::Attention);
    const Rid warn_row = Rwidget_create(R_WIDGET_HBOX);
    Rwidget_add_child(warn_row, warn);
    Rslot_set_fill(warn, 1);
    Rwidget_add_child(leg, warn_row);
    Rwidget_add_child(box, leg);
    leg_rows_.push_back(leg);
  }
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
    set_visible(Rwidget_get_child(leg, 1), l.no_bid);
  }
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
