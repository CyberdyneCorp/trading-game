#include "view/screens/lab_screen.hpp"

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_layout.h>
#include <raven/raven_widget.h>

#include <cmath>

#include "view/raven/raven_ui.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/builder_widgets.hpp"

namespace tradelab::desktop::view {

namespace b = names::builder;
using game::AcademyLeg;

namespace {

// The editable number fields of a leg, in row order after the kind and side boxes.
struct Field {
  const char* label;
  double AcademyLeg::*member;
  float width;
  int digits;
};
constexpr Field kFields[] = {{"opt.strike", &AcademyLeg::strike, 70, 2},       {"opt.qty", &AcademyLeg::qty, 50, 1},
                             {"lab.daysToExpiry", &AcademyLeg::days, 50, 0},   {"lab.ivPct", &AcademyLeg::iv_pct, 50, 1},
                             {"lab.entry", &AcademyLeg::entry, 70, 2}};
constexpr int kFirstField = 2;

void select(Rid combo, int index) {
  if (combo != R_INVALID_ID && Rcombo_box_get_selected(combo) != index) Rcombo_box_set_selected(combo, index);
}

int kind_index(game::LegKind k) {
  if (k == game::LegKind::Call) return 0;
  return k == game::LegKind::Put ? 1 : 2;
}

}  // namespace

LabScreen::LabScreen(AppViewModel& app, Rid root) : app_(app), root_(root) {}

Rid LabScreen::find(std::string_view name) const { return ui::find(root_, name); }

std::vector<std::string> LabScreen::missing_names() const {
  std::vector<std::string> missing;
  auto all = LabPanel::names(std::string(b::kScreenPrefix));
  for (const auto w : {b::kScreenTitle, b::kScreenBack, b::kScreenIntro, b::kScreenTag, b::kScreenLegsCaption, b::kScreenLegs,
                       b::kScreenAddOption, b::kScreenAddPerp, b::kScreenPriceModel}) {
    all.emplace_back(w);
  }
  for (const auto& w : all) {
    if (find(w) == R_INVALID_ID) missing.push_back("widget " + w);
  }
  return missing;
}

void LabScreen::bind() {
  panel_ = std::make_unique<LabPanel>(root_, std::string(b::kScreenPrefix), cb_,
                                      [this]() -> game::LabViewModel& { return app_.academy_lab(); },
                                      [this] { app_.academy_lab_changed(); });
  panel_->bind();
  cb_.click(find(b::kScreenAddOption), [this] {
    app_.academy_lab().add_leg(game::LegKind::Call);
    edited();
  });
  cb_.click(find(b::kScreenAddPerp), [this] {
    app_.academy_lab().add_leg(game::LegKind::Perp);
    edited();
  });
  cb_.click(find(b::kScreenPriceModel), [this] { app_.academy_lab_price_at_model(); });
}

void LabScreen::edited() { app_.academy_lab_changed(); }

void LabScreen::refresh() {
  if (app_.screen() != Screen::Lab || app_.revision() == shown_) return;
  shown_ = app_.revision();
  const game::Texts t = app_.game().texts();
  set_label(find(b::kScreenTitle), t.tr("lab.title"));
  ui::set_text(find(b::kScreenBack), "\xE2\x97\x80 " + t.tr("top.menu"));  // ◀
  set_label(find(b::kScreenIntro), t.tr("lab.intro"));
  set_label(find(b::kScreenTag), t.tr("lab.illustrative"));
  set_label(find(b::kScreenLegsCaption), t.tr("lab.legsCaption"));
  ui::set_text(find(b::kScreenAddOption), "+ " + t.tr("lab.addOption"));
  ui::set_text(find(b::kScreenAddPerp), "+ " + t.tr("lab.addPerp"));
  ui::set_text(find(b::kScreenPriceModel), t.tr("lab.priceAtModel"));
  build_rows(t);
  apply_rows();
  panel_->apply(t);
}

namespace {

Rid add_combo(Rid row, std::initializer_list<std::string> items, const std::string& name) {
  const Rid c = Rwidget_create(R_WIDGET_COMBO_BOX);
  for (const auto& item : items) Rcombo_box_add_item(c, item.c_str());
  Rwidget_set_accessible_name(c, name.c_str());
  Rwidget_set_size(c, 120, -1);
  Rwidget_add_child(row, c);
  return c;
}

game::LegKind kind_at(int k) {
  if (k == 0) return game::LegKind::Call;
  return k == 1 ? game::LegKind::Put : game::LegKind::Perp;
}

}  // namespace

// A row per leg: kind, side, strike, quantity, days, IV, entry and remove; rebuilt when the number of
// legs or the locale changes, so a field being typed in keeps its focus.
void LabScreen::build_rows(const game::Texts& t) {
  const auto& legs = app_.academy_lab().academy_legs;
  const std::string key = std::to_string(static_cast<int>(t.locale())) + "|" + std::to_string(legs.size());
  if (key == rows_key_) return;
  rows_key_ = key;
  for (const Rid row : rows_) {
    for (int i = 0; i < Rwidget_get_child_count(row); ++i) cb_.erase(Rwidget_get_child(row, i));
    Rwidget_destroy(row);
  }
  rows_.clear();
  for (std::size_t i = 0; i < legs.size(); ++i) add_row(t, i);
}

void LabScreen::add_row(const game::Texts& t, std::size_t i) {
  const Rid row = Rwidget_create(R_WIDGET_WRAP_BOX);
  cb_.choose(add_combo(row, {"Call", "Put", t.tr("lab.perp")}, t.tr("lab.kind")), [this, i](int k) {
    app_.academy_lab().academy_legs.at(i).kind = kind_at(k);
    edited();
  });
  cb_.choose(add_combo(row, {t.tr("opt.buy"), t.tr("opt.sell")}, t.tr("lab.side")), [this, i](int s) {
    app_.academy_lab().academy_legs.at(i).buy = s == 0;
    edited();
  });
  for (const auto& f : kFields) {
    const Rid label = Rlabel_create(t.tr(f.label).c_str());
    Rlabel_set_text_size(label, 11);
    set_color(label, game::Tone::Muted);
    Rwidget_add_child(row, label);
    const Rid input = Rwidget_create(R_WIDGET_TEXT_INPUT);
    Rwidget_set_size(input, f.width, -1);
    Rwidget_set_accessible_name(input, t.tr(f.label).c_str());
    Rwidget_add_child(row, input);
    double AcademyLeg::*member = f.member;
    cb_.text(input, [this, i, member](const std::string& text) {
      const double v = fmt::parse_decimal(text);
      if (std::isfinite(v)) app_.academy_lab().academy_legs.at(i).*member = v;
    });
    cb_.commit(input, [this] { edited(); });
  }
  const Rid remove = Rbutton_create("\xE2\x9C\x95");  // ✕
  Rwidget_set_accessible_name(remove, t.tr("lab.remove").c_str());
  Rwidget_add_child(row, remove);
  cb_.click(remove, [this, i] {
    app_.academy_lab().remove_leg(i);
    edited();
  });
  Rwidget_add_child(find(b::kScreenLegs), row);
  rows_.push_back(row);
}

void LabScreen::apply_rows() {
  const auto& legs = app_.academy_lab().academy_legs;
  const Locale L = app_.game().texts().locale();
  for (std::size_t i = 0; i < legs.size() && i < rows_.size(); ++i) {
    const Rid row = rows_[i];
    select(Rwidget_get_child(row, 0), kind_index(legs[i].kind));
    select(Rwidget_get_child(row, 1), legs[i].buy ? 0 : 1);
    int child = kFirstField;
    for (const auto& f : kFields) {
      const Rid input = Rwidget_get_child(row, child + 1);
      child += 2;
      if (Rwidget_is_focused(input) != 0) continue;
      ui::set_text(input, fmt::fmt_num(legs[i].*f.member, L, f.digits));
      // A perpetual leg has no strike, expiry or IV: only its quantity and entry mean something.
      const bool option_only = f.member == &AcademyLeg::strike || f.member == &AcademyLeg::days || f.member == &AcademyLeg::iv_pct;
      Rwidget_set_enabled(input, (option_only && legs[i].kind == game::LegKind::Perp) ? R_FALSE : R_TRUE);
    }
  }
}

}  // namespace tradelab::desktop::view
