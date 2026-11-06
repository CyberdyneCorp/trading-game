// The Academy's options lab screen on Raven (resources/views/lab.xml, web LabScreen.svelte and
// AcademyLegs.svelte): the editable legs, then the shared lab panel (sliders, summary, payoff, table).
// Glue only; the legs and every number live in AppViewModel::academy_lab().
#pragma once

#include <raven/raven_core.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "view/game/lab_panel.hpp"
#include "view/game/widgets.hpp"
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop::view {

class LabScreen {
 public:
  LabScreen(AppViewModel& app, Rid root);
  void bind();
  // Pushes the lab's state into the widgets when it changed (the Lab screen only).
  void refresh();
  [[nodiscard]] std::vector<std::string> missing_names() const;

 private:
  [[nodiscard]] Rid find(std::string_view name) const;
  void build_rows(const game::Texts& t);
  void apply_rows();
  void edited();

  AppViewModel& app_;
  Rid root_;
  Callbacks cb_;
  std::unique_ptr<LabPanel> panel_;
  std::vector<Rid> rows_;
  std::string rows_key_;
  std::uint64_t shown_ = 0;
};

}  // namespace tradelab::desktop::view
