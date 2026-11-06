// Checks that depend on the window's exact size (reference images, a text cut by its panel) hold only at
// the size the test asked for. A screen smaller than the window makes the system shrink it: the hosted
// macOS runners have a 1024x768 display, so every window there is 1024x645 (menu bar, Dock and title bar
// taken), below the app's own 1024x768 minimum, whatever size the test asks for. With
// TRADELAB_SMALL_SCREEN=1 (CI's macOS job) such checks are then reported and skipped; anywhere else a
// shrunk window fails the test, so a check is never skipped by surprise. Checks of behaviour (orders,
// drags, flows) run at any size.
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_window.h>

#include <cstdlib>
#include <string>

namespace tradelab::desktop::testing {

inline bool small_screen_allowed() {
  const char* v = std::getenv("TRADELAB_SMALL_SCREEN");
  return v != nullptr && std::string(v) == "1";
}

// True when `window` has the logical size `width` x `height`, so size-dependent checks apply.
inline bool window_has_size(Rid window, int width, int height) {
  int w = 0;
  int h = 0;
  Rwindow_get_size(window, &w, &h);
  if (w == width && h == height) return true;
  const std::string why = "the window is " + std::to_string(w) + "x" + std::to_string(h) + ", not " + std::to_string(width) + "x" +
                          std::to_string(height) + ": the screen is smaller than the window";
  REQUIRE_MESSAGE(small_screen_allowed(), why << " (TRADELAB_SMALL_SCREEN=1 skips the size-dependent checks)");
  MESSAGE(why << "; size-dependent check skipped (TRADELAB_SMALL_SCREEN=1)");
  return false;
}

}  // namespace tradelab::desktop::testing
