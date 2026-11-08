#include "view/screens/lesson_lab_view.hpp"

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include <string>
#include <utility>
#include <variant>

#include "view/game/scene_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/academy_names.hpp"

namespace tradelab::desktop::view {

namespace {

std::string kv(const std::vector<std::pair<std::string, std::string>>& rows) {
  std::string out;
  for (const auto& [k, v] : rows) out += (out.empty() ? "" : "\n") + k + ": " + v;
  return out;
}

std::string template_button(std::size_t i) { return std::string(names::kLessonBuilderTemplatePrefix) + std::to_string(i); }

}  // namespace

LessonLabView::LessonLabView(AppViewModel& app, Rid root) : app_(app), root_(root) {}

Rid LessonLabView::find(std::string_view name) const { return ui::find(root_, name); }

std::vector<std::string> LessonLabView::missing_names() const {
  std::vector<std::string> missing;
  auto all = LabPanel::names(std::string(names::kLessonLabPrefix));
  for (std::size_t i = 0; i < game::kTemplates.size(); ++i) all.push_back(template_button(i));
  for (const auto& w : all) {
    if (find(w) == R_INVALID_ID) missing.push_back("widget " + w);
  }
  return missing;
}

void LessonLabView::bind() {
  panel_ = std::make_unique<LabPanel>(root_, std::string(names::kLessonLabPrefix), cb_,
                                      [this]() -> game::LabViewModel& { return app_.lesson_lab(); },
                                      [this] { app_.lesson_lab_changed(); });
  panel_->bind();
  for (std::size_t i = 0; i < game::kTemplates.size(); ++i) {
    const game::Template id = game::kTemplates[i];
    cb_.click(find(template_button(i)), [this, id] { app_.lesson_builder_pick(id); });
  }
  cb_.check(find(names::kLessonBuilderShort), [this](bool on) { app_.lesson_builder_short(on); });
  table_ = LineList(find(names::kLessonBuilderTable), 11);
  Rcustom_set_paint_callback(find(names::kLessonBuilderPayoff), on_paint, this);
}

void LessonLabView::refresh() {
  if (app_.screen() != Screen::Lesson || app_.revision() == shown_) return;
  shown_ = app_.revision();
  const LessonStep* step = app_.shell().lesson.step();
  if (step == nullptr || app_.shell().lesson.finished()) return;
  const game::Texts t = app_.game().texts();
  if (std::holds_alternative<WhatIfWidget>(step->widget)) panel_->apply(t);
  if (std::holds_alternative<BuilderWidget>(step->widget)) apply_builder(t);
}

void LessonLabView::apply_builder(const game::Texts& t) {
  const academy::LessonBuilderView v = app_.lesson_builder().view(t);
  Rwidget_set_accessible_name(find(names::kLessonBuilderTemplates), t.tr("builder.templates").c_str());
  for (std::size_t i = 0; i < v.templates.size(); ++i) {
    const Rid button = find(template_button(i));
    const bool on = v.templates[i].selected;
    ui::set_text(button, (on ? "\xE2\x96\xB8 " : "") + v.templates[i].label);                     // ▸
    Rwidget_set_accessible_name(button, (v.templates[i].label + (on ? " \xE2\x9C\x93" : "")).c_str());  // ✓
  }
  const Rid box = find(names::kLessonBuilderShort);
  ui::set_text(box, v.short_label);
  set_enabled(box, v.short_enabled);
  if ((Rcheckbox_is_checked(box) != 0) != app_.lesson_builder().short_side()) {
    Rcheckbox_set_checked(box, app_.lesson_builder().short_side() ? R_TRUE : R_FALSE);
  }
  set_label(find(names::kLessonBuilderNote), v.note);
  set_label(find(names::kLessonBuilderError), v.error);
  set_visible(find(names::kLessonBuilderError), !v.error.empty());
  set_label(find(names::kLessonBuilderPick), v.pick);
  set_visible(find(names::kLessonBuilderPick), !v.pick.empty());
  set_label(find(names::kLessonBuilderAnalysis), kv(v.analysis));
  set_visible(find(names::kLessonBuilderAnalysis), v.has_result);
  const Rid payoff = find(names::kLessonBuilderPayoff);
  set_visible(payoff, v.has_result);
  Rwidget_set_accessible_name(payoff, v.chart_summary.c_str());
  set_label(find(names::kLessonBuilderLegend), v.chart_legend);
  set_visible(find(names::kLessonBuilderLegend), v.has_result);
  table_.set(v.table);
  Rwidget_invalidate(payoff);
}

void LessonLabView::on_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* self) {
  const auto& r = static_cast<LessonLabView*>(self)->app_.lesson_builder().result();
  if (r) paint_scene(painter, game::payoff_scene(*r, width, height));
}

}  // namespace tradelab::desktop::view
