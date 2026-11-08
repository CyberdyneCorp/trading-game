// A lesson step's engine-valued widgets on Raven (resources/views/lesson.xml; web LessonWhatIf.svelte and
// LessonBuilder.svelte): the shared lab panel ("lesson-lab-*") over AppViewModel::lesson_lab(), and the
// builder's templates, analysis, payoff and table from AppViewModel::lesson_builder(). Glue only.
#pragma once

#include <raven/raven_core.h>
#include <raven/raven_draw.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "view/game/lab_panel.hpp"
#include "view/game/widgets.hpp"
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop::view {

class LessonLabView {
 public:
  LessonLabView(AppViewModel& app, Rid root);
  void bind();
  // Pushes the step's lab or builder into the widgets when they changed (the Lesson screen only).
  void refresh();
  [[nodiscard]] std::vector<std::string> missing_names() const;

 private:
  [[nodiscard]] Rid find(std::string_view name) const;
  void apply_builder(const game::Texts& t);
  static void on_paint(Rid widget, RPainter* painter, float width, float height, void* self);

  AppViewModel& app_;
  Rid root_;
  Callbacks cb_;
  std::unique_ptr<LabPanel> panel_;
  LineList table_;
  std::uint64_t shown_ = 0;
};

}  // namespace tradelab::desktop::view
