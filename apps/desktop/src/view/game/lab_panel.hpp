// The what-if lab's widgets (web WhatIfLab.svelte: sliders, summary, payoff chart, table), shared by the
// terminal's lab dialog ("game-lab-*" in views/game.xml) and the Academy lab screen ("lab-*" in
// views/lab.xml). Glue only: the sliders move game::LabViewModel and `changed` asks the engine again;
// every text and the chart's display list come from the viewmodel.
#pragma once

#include <raven/raven_core.h>
#include <raven/raven_draw.h>

#include <functional>
#include <string>
#include <string_view>

#include "view/game/widgets.hpp"
#include "viewmodel/game/lab_vm.hpp"

namespace tradelab::desktop::view {

class LabPanel {
 public:
  using Model = std::function<game::LabViewModel&()>;
  // `prefix`: "game-lab-" or "lab-"; `changed`: a slider moved or the shift was reset.
  LabPanel(Rid root, std::string prefix, Callbacks& cb, Model model, std::function<void()> changed);

  void bind();
  void apply(const game::Texts& t);
  // The widget names this panel needs (headless test).
  [[nodiscard]] static std::vector<std::string> names(const std::string& prefix);

 private:
  [[nodiscard]] Rid find(std::string_view suffix) const;
  static void on_paint(Rid widget, RPainter* painter, float width, float height, void* self);

  Rid root_;
  std::string prefix_;
  Callbacks* cb_;
  Model model_;
  std::function<void()> changed_;
  LineList table_;
};

}  // namespace tradelab::desktop::view
