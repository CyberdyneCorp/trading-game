#include "view/screens/retro_look.hpp"

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_resources.h>
#include <raven/raven_shader.h>
#include <raven/raven_style.h>
#include <raven/raven_widget.h>

#include "model/resources.hpp"
#include "viewmodel/screen_names.hpp"

namespace tradelab::desktop {

namespace {

constexpr const char* kTheme = "TradeLab";
constexpr const char* kThemeColorblind = "TradeLab CB";

Rid font_from(std::string_view path) {
  const std::string_view data = resource_text(path);
  return data.empty() ? R_INVALID_ID : Rfont_create(data.data(), data.size());
}

void load_themes() {
  for (std::string_view path : {"theme/tradelab.theme.xml", "theme/tradelab-cb.theme.xml"}) {
    const std::string_view xml = resource_text(path);
    Rtheme_load_from_memory(xml.data(), xml.size());
  }
}

}  // namespace

Rid RetroLook::create(Rid screens_root) {
  load_themes();
  pixel_font_ = font_from("fonts/VT323-Regular.ttf");
  text_font_ = font_from("fonts/IBMPlexMono-Regular.ttf");
  const std::string_view spv = resource_text("shaders/crt.spv");
  shader_ = Rshader_create(spv.data(), spv.size(), R_SHADER_BACKDROP);
  if (shader_ != R_INVALID_ID && !Rshader_is_valid(shader_)) shader_ = R_INVALID_ID;
  const Rid stack = Rwidget_create(R_WIDGET_OVERLAY);
  Rwidget_add_child(stack, screens_root);
  overlay_ = Rwidget_create(R_WIDGET_BORDER);
  Rwidget_set_name(overlay_, std::string(names::kCrtOverlay).c_str());
  Rwidget_set_hit_testable(overlay_, R_FALSE);
  Rwidget_set_focusable(overlay_, R_FALSE);
  Rwidget_set_accessible_hidden(overlay_, R_TRUE);  // decoration
  Rwidget_set_visibility(overlay_, R_COLLAPSED);
  Rwidget_add_child(stack, overlay_);
  title_fonts(screens_root);
  return stack;
}

void RetroLook::apply(bool colorblind, float text_scale, const CrtState& crt) {
  const int palette = colorblind ? 1 : 0;
  if (palette != palette_) {
    Rtheme_use(colorblind ? kThemeColorblind : kTheme);
    if (text_font_ != R_INVALID_ID) Rtheme_set_font(text_font_);
    palette_ = palette;
  }
  if (text_scale != text_scale_) {
    Raccessibility_set_text_scale(text_scale);
    text_scale_ = text_scale;
  }
  if (crt_applied_ && crt == crt_) return;
  crt_ = crt;
  crt_applied_ = true;
  // Without a shader (null backend, or MoltenVK refusing it) the overlay stays collapsed: never a
  // plain rectangle over the screens.
  const bool shown = crt.overlay && shader_ != R_INVALID_ID;
  Rwidget_set_visibility(overlay_, shown ? R_VISIBLE : R_COLLAPSED);
  if (!shown) return;
  RShaderLook look;
  Rshader_look_init(&look);
  look.color[3] = 0.0f;
  look.params[0] = crt.scanlines ? 1.0f : 0.0f;
  look.params[1] = crt.bloom ? 1.0f : 0.0f;
  look.params[2] = crt.noise ? 1.0f : 0.0f;
  look.radius = 0;
  look.animate = R_FALSE;
  look.pointer = R_FALSE;
  Rborder_set_effect(overlay_, shader_, &look);
}

void RetroLook::title_fonts(Rid root) const {
  if (root == R_INVALID_ID || pixel_font_ == R_INVALID_ID) return;
  if (Rwidget_get_type(root) == R_WIDGET_LABEL && Rwidget_has_class(root, "title")) Rlabel_set_font(root, pixel_font_);
  const int n = Rwidget_get_child_count(root);
  for (int i = 0; i < n; ++i) title_fonts(Rwidget_get_child(root, i));
}

}  // namespace tradelab::desktop
