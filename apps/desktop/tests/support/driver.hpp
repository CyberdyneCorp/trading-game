// Drives the real app through Raven's input injection, the way Playwright drives the web: clicks land
// on a widget's laid-out rectangle, keys and text go through the window's event path.
#pragma once

#include <raven/raven_core.h>
#include <raven/raven_layout.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <array>
#include <chrono>
#include <cstdint>
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

  // After a start: the mission's tutorial shows the first time (fresh data folder), skipped here as a
  // player would; then the briefing.
  bool to_briefing() {
    const auto& vm = app_.vm();
    if (!until([&] { return vm.screen() == Screen::Tutorial || vm.screen() == Screen::Briefing; })) return false;
    if (vm.screen() == Screen::Tutorial && !click("tut-skip")) return false;
    return until([&] { return vm.screen() == Screen::Briefing; });
  }

  [[nodiscard]] Rid widget(std::string_view name) const { return app_.view().find(name); }

  // Left click at the centre of the named widget. False when it has no area (not laid out / hidden).
  bool click(std::string_view name) {
    Rwidget_scroll_into_view(widget(name));  // as a player scrolls a panel to reach it
    frames(2);
    float x = 0, y = 0, w = 0, h = 0;
    Rwidget_get_rect(widget(name), &x, &y, &w, &h);
    if (w <= 0 || h <= 0) return false;
    press_release(x + w / 2, y + h / 2);
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

  // Keyboard focus on the named widget, as a Tab would give it (Playwright's focus()).
  void focus(std::string_view name) {
    Rwidget_focus(widget(name));
    frames(1);
  }

  // Picks item `index` of a combo box from the keyboard (focus, then arrows from the first item).
  void choose(std::string_view name, int index) {
    focus(name);
    for (int i = 0; i < 12; ++i) key(R_KEY_UP);
    for (int i = 0; i < index; ++i) key(R_KEY_DOWN);
  }

  // Mouse press at a point in window coordinates, dragged to another, released there.
  void drag(float x0, float y0, float x1, float y1) {
    Rwindow_inject_mouse_move(window_, x0, y0);
    frames(2);
    Rwindow_inject_mouse_button(window_, R_MOUSE_LEFT, R_TRUE);
    frames(1);
    for (int i = 1; i <= 6; ++i) {
      Rwindow_inject_mouse_move(window_, x0 + (x1 - x0) * static_cast<float>(i) / 6, y0 + (y1 - y0) * static_cast<float>(i) / 6);
      frames(1);
    }
    Rwindow_inject_mouse_button(window_, R_MOUSE_LEFT, R_FALSE);
    frames(2);
  }

  // Left click at a point in window coordinates.
  void click_at(float x, float y) { press_release(x, y); }

  // Hover first (a frame to show it), then the move, press and release with no frame between them: a
  // frame polls the system's events, and a real pointer over the window (another app's, or the
  // developer's) would otherwise move the hover between the press and the release, losing the click.
  void press_release(float x, float y) {
    Rwindow_inject_mouse_move(window_, x, y);
    frames(1);
    Rwindow_inject_mouse_move(window_, x, y);
    Rwindow_inject_mouse_button(window_, R_MOUSE_LEFT, R_TRUE);
    Rwindow_inject_mouse_button(window_, R_MOUSE_LEFT, R_FALSE);
    frames(2);
  }

  // Runs frames until `widget` kept its rectangle over three drawn frames: a layout that changes under
  // the pointer (a text that appears and wraps) has settled. A busy machine draws frames late, so they
  // are counted by RFrameStats, not by calls.
  void settle(Rid widget) {
    std::array<float, 4> last{-1, -1, -1, -1};
    std::uint64_t seen = frame_index();
    int stable = 0;
    for (int i = 0; i < 300 && stable < 3; ++i) {
      frames(1);
      const std::uint64_t now = frame_index();
      if (now == seen) continue;
      seen = now;
      std::array<float, 4> r{};
      Rwidget_get_rect(widget, &r[0], &r[1], &r[2], &r[3]);
      stable = r == last ? stable + 1 : 0;
      last = r;
    }
  }

  [[nodiscard]] Rid window() const { return window_; }

 private:
  [[nodiscard]] std::uint64_t frame_index() const {
    RFrameStats s{};
    Rwindow_get_frame_stats(window_, &s);
    return s.frame_index;
  }

  App& app_;
  Rid window_;
};

}  // namespace tradelab::desktop::testing
