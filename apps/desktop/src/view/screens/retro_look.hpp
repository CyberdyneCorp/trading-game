// The retro look (design "Retro look"): the TradeLab theme (and its colorblind palette), the bundled
// fonts (IBM Plex Mono for text and numbers, VT323 for titles at integer multiples of its 12.5 px
// pixel grid), the text scale, and the CRT post-process: a full-window overlay Border running
// resources/shaders/crt.spv on what is drawn behind it, with each effect toggled from Settings.
#pragma once

#include <raven/raven_core.h>

#include "viewmodel/shell_state.hpp"

namespace tradelab::desktop {

// VT323 draws one font pixel per 0.08 em (cap height: 7 pixels of 80/1000 em), so 12.5 px is one
// screen pixel per font pixel; titles use 25, 37.5 and 50.
inline constexpr float kPixelFontGrid = 12.5f;
inline constexpr float kPixelTitleSize = 25.0f;

class RetroLook {
 public:
  // Loads themes and fonts (once per Raven session) and creates the CRT overlay, which the caller
  // stacks over the screens (Overlay: screens first, this last).
  Rid create(Rid screens_root);
  // Applies palette, text scale and CRT toggles when they changed.
  void apply(bool colorblind, float text_scale, const CrtState& crt);
  // Gives VT323 to every Label of class "title" under `root` (and to `root` itself).
  void title_fonts(Rid root) const;
  [[nodiscard]] Rid pixel_font() const { return pixel_font_; }
  [[nodiscard]] Rid overlay() const { return overlay_; }
  [[nodiscard]] bool shader_ready() const { return shader_ != R_INVALID_ID; }

 private:
  Rid overlay_ = R_INVALID_ID;
  Rid shader_ = R_INVALID_ID;
  Rid pixel_font_ = R_INVALID_ID;
  Rid text_font_ = R_INVALID_ID;
  int palette_ = -1;  // -1: not applied yet
  float text_scale_ = 0;
  CrtState crt_;
  bool crt_applied_ = false;
};

}  // namespace tradelab::desktop
