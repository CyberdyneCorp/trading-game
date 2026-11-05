// Drives the real app through Raven's input injection, the way Playwright drives the web: clicks land
// on a widget's laid-out rectangle, keys and text go through the window's event path.
#pragma once

#include <raven/raven_core.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <chrono>
#include <string>
#include <string_view>

#include "view/app.hpp"
#include "view/raven/raven_ui.hpp"

namespace tradelab::desktop::testing {

class Driver {
 public:
  explicit Driver(App& app) : app_(app), window_(app.view().window()) {}

  void frames(int n) {
    for (int i = 0; i < n; ++i) {
      Rwindow_request_redraw(window_);
      Rwait_frame(0.005);
    }
  }

  // Runs frames until `done()`; waits for the engine worker without spinning.
  template <class Pred>
  bool until(Pred done, double timeout_seconds = 30.0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_seconds);
    while (!done()) {
      if (std::chrono::steady_clock::now() > deadline) return false;
      Rwait_frame(0.02);
    }
    frames(2);  // let refresh() push the change into the widgets and lay them out
    return true;
  }

  bool idle() {
    return until([this] { return !app_.vm().waiting(); });
  }

  [[nodiscard]] Rid widget(std::string_view name) const { return app_.view().find(name); }

  // Left click at the centre of the named widget. False when it has no area (not laid out / hidden).
  bool click(std::string_view name) {
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(widget(name), &x, &y, &w, &h);
    if (w <= 0 || h <= 0) return false;
    Rwindow_inject_mouse_move(window_, x + w / 2, y + h / 2);
    frames(1);
    Rwindow_inject_mouse_button(window_, R_MOUSE_LEFT, R_TRUE);
    frames(1);
    Rwindow_inject_mouse_button(window_, R_MOUSE_LEFT, R_FALSE);
    frames(2);
    return true;
  }

  void key(RKey k, Ruint modifiers = 0) {
    Rwindow_inject_key(window_, k, R_TRUE, modifiers);
    frames(1);
    Rwindow_inject_key(window_, k, R_FALSE, modifiers);
    frames(1);
  }

  // Replaces the content of the named text field by typing, as a player would.
  void type_into(std::string_view name, std::string_view text) {
    click(name);
    const Rid w = widget(name);
    for (std::size_t n = ui::text_of(w).size(); n > 0; --n) key(R_KEY_BACKSPACE);
    key(R_KEY_END);
    for (std::size_t n = ui::text_of(w).size(); n > 0; --n) key(R_KEY_BACKSPACE);
    Rwindow_inject_text(window_, std::string(text).c_str());
    frames(2);
  }

  [[nodiscard]] std::string text(std::string_view name) const { return ui::text_of(widget(name)); }

 private:
  App& app_;
  Rid window_;
};

}  // namespace tradelab::desktop::testing
