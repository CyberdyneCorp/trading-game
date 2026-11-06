#include "view/game/lab_panel.hpp"

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_widget.h>

#include <cmath>

#include "view/game/scene_painter.hpp"
#include "view/raven/raven_ui.hpp"

namespace tradelab::desktop::view {

namespace {

constexpr std::string_view kSuffixes[] = {"price", "time", "iv"};

std::string kv(const std::vector<std::pair<std::string, std::string>>& rows) {
  std::string out;
  for (const auto& [k, v] : rows) out += (out.empty() ? "" : "\n") + k + ": " + v;
  return out;
}

void slider_to(Rid slider, float lo, float hi, float value) {
  if (slider == R_INVALID_ID) return;
  float a = 0, b = 0;
  Rslider_get_range(slider, &a, &b, nullptr);
  if (a != lo || b != hi) Rslider_set_range(slider, lo, hi);
  if (std::fabs(Rslider_get_value(slider) - value) > 0.01f) Rslider_set_value(slider, value);
}

}  // namespace

LabPanel::LabPanel(Rid root, std::string prefix, Callbacks& cb, Model model, std::function<void()> changed)
    : root_(root), prefix_(std::move(prefix)), cb_(&cb), model_(std::move(model)), changed_(std::move(changed)) {}

Rid LabPanel::find(std::string_view suffix) const { return ui::find(root_, prefix_ + std::string(suffix)); }

std::vector<std::string> LabPanel::names(const std::string& prefix) {
  std::vector<std::string> out;
  for (const char* s : {"note", "price", "time", "iv", "price-label", "price-value", "reset", "error", "summary", "payoff",
                        "legend", "table", "expiry-note"}) {
    out.push_back(prefix + s);
  }
  return out;
}

void LabPanel::bind() {
  table_ = LineList(find("table"), 11);
  for (std::size_t i = 0; i < game::kSliders.size(); ++i) {
    const game::Slider which = game::kSliders[i];
    cb_->slide(find(kSuffixes[i]), [this, which](float v) {
      game::LabViewModel& m = model_();
      const int value = static_cast<int>(std::lround(v));
      if (value == (which == game::Slider::Price ? m.shift.price : which == game::Slider::Time ? m.shift.time : m.shift.iv)) return;
      m.slide(which, value);
      changed_();
    });
  }
  cb_->click(find("reset"), [this] {
    model_().reset();
    changed_();
  });
  Rcustom_set_paint_callback(find("payoff"), on_paint, this);
}

void LabPanel::apply(const game::Texts& t) {
  const game::LabView v = model_().view(t);
  set_label(find("note"), v.note);
  for (std::size_t i = 0; i < v.sliders.size(); ++i) {
    const auto& s = v.sliders[i];
    const std::string base(kSuffixes[i]);
    set_label(find(base + "-label"), s.label);
    set_label(find(base + "-value"), s.text);
    const Rid slider = find(base);
    slider_to(slider, static_cast<float>(s.min), static_cast<float>(std::max(s.max, s.min + 1)), static_cast<float>(s.value));
    set_enabled(slider, s.enabled);
    Rwidget_set_accessible_name(slider, s.label.c_str());
    Rwidget_set_accessible_description(slider, s.text.c_str());
  }
  ui::set_text(find("reset"), t.tr("lab.reset"));
  set_label(find("error"), v.error);
  set_visible(find("error"), !v.error.empty());
  set_label(find("summary"), kv(v.summary));
  set_color(find("summary"), game::Tone::Plain);
  set_label(find("expiry-note"), v.at_expiry_note);
  set_visible(find("expiry-note"), !v.at_expiry_note.empty());
  set_visible(find("payoff"), v.has_result);
  Rwidget_set_accessible_name(find("payoff"), v.chart_summary.c_str());
  set_label(find("legend"), v.chart_legend);
  table_.set(v.table);
  Rwidget_invalidate(find("payoff"));
}

void LabPanel::on_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* self) {
  auto* panel = static_cast<LabPanel*>(self);
  const auto& r = panel->model_().result();
  if (!r) return;
  paint_scene(painter, game::payoff_scene(*r, width, height));
}

}  // namespace tradelab::desktop::view
