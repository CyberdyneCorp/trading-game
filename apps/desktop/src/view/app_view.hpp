// The Raven side of the app: one window whose content is a Switcher holding one XML view per screen
// (resources/views/*.xml), the actions bound to viewmodel commands, the chart painter and the
// keyboard hook. refresh() copies viewmodel changes into the widgets once per frame.
#pragma once

#include <raven/raven_core.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "view/game/game_view.hpp"
#include "view/screens/shell_view.hpp"
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

class AppView {
 public:
  explicit AppView(AppViewModel& vm) : vm_(vm) {}

  // Creates the window and loads every screen; false when a view fails to load (see ui::last_error()).
  bool build(int width = 1280, int height = 800);
  // Pushes viewmodel changes into the widgets; cheap when nothing changed.
  void refresh();

  [[nodiscard]] Rid window() const { return window_; }
  [[nodiscard]] Rid screen_root(Screen s) const { return screens_[static_cast<std::size_t>(s)]; }
  [[nodiscard]] Rid find(std::string_view name) const;
  // Named widgets and actions of widget_names.hpp that are missing from the views or unbound.
  [[nodiscard]] std::vector<std::string> missing_names() const;
  // The terminal screen (view/game); keys reach it through the window hook while it shows.
  [[nodiscard]] view::GameView& game_view() { return *game_view_; }
  bool on_key(const REvent* e);
  [[nodiscard]] ShellView& shell() { return *shell_; }

 private:
  static void missing_actions(std::span<const std::string_view> actions, std::vector<std::string>& missing);
  void missing_widgets(std::span<const std::string_view> widgets, std::vector<std::string>& missing) const;
  void bind_actions();
  void bind_widgets();
  void apply_bindings();

  AppViewModel& vm_;
  Rid window_ = R_INVALID_ID;
  Rid switcher_ = R_INVALID_ID;
  std::array<Rid, 8> screens_{};
  std::unique_ptr<ShellView> shell_;
  std::uint64_t shown_revision_ = 0;
  std::unique_ptr<view::GameView> game_view_;
};

// Starts Raven on the null platform and backend (no window system, no GPU): headless tests.
bool init_headless();
// Starts Raven with Vulkan; false when no Vulkan driver could be used (`detail` gets Raven's reason).
bool init_vulkan(std::string& detail);

}  // namespace tradelab::desktop
