#include "view/game/widgets.hpp"

#include <raven/raven_controls.h>
#include <raven/raven_widget.h>
#include <raven/raven_xml.h>

#include "view/raven/raven_ui.hpp"

namespace tradelab::desktop::view {

std::string_view tone_color(game::Tone tone) {
  switch (tone) {
    case game::Tone::Ok:
    case game::Tone::Up: return "#39FF88";
    case game::Tone::Error:
    case game::Tone::Down: return "#FF5F7A";
    case game::Tone::Warn:
    case game::Tone::Attention: return "#FFC857";
    case game::Tone::Info: return "#4FD8FF";
    case game::Tone::Muted: return "#8FB3A1";
    case game::Tone::Plain: break;
  }
  return "#E3F5EA";
}

void set_visible(Rid widget, bool visible) {
  if (widget == R_INVALID_ID) return;
  const RVisibility want = visible ? R_VISIBLE : R_COLLAPSED;
  if (Rwidget_get_visibility(widget) != want) Rwidget_set_visibility(widget, want);
}

void set_color(Rid label, game::Tone tone) {
  if (label != R_INVALID_ID) Rwidget_set_attribute(label, "Color", std::string(tone_color(tone)).c_str());
}

void set_enabled(Rid widget, bool enabled) {
  if (widget == R_INVALID_ID) return;
  if ((Rwidget_is_enabled(widget) != 0) != enabled) Rwidget_set_enabled(widget, enabled ? R_TRUE : R_FALSE);
}

void set_label(Rid label, const std::string& text) {
  if (label != R_INVALID_ID) ui::set_text(label, text);
}

void Callbacks::click(Rid button, std::function<void()> fn) {
  if (button == R_INVALID_ID) return;
  clicks_[button] = std::move(fn);
  Rbutton_set_clicked_callback(button, on_click, this);
}

void Callbacks::text(Rid input, std::function<void(const std::string&)> fn) {
  if (input == R_INVALID_ID) return;
  texts_[input] = std::move(fn);
  Rtext_input_set_changed_callback(input, on_text, this);
}

void Callbacks::commit(Rid input, std::function<void()> fn) {
  if (input == R_INVALID_ID) return;
  commits_[input] = std::move(fn);
  Rtext_input_set_committed_callback(input, on_commit, this);
}

void Callbacks::check(Rid checkbox, std::function<void(bool)> fn) {
  if (checkbox == R_INVALID_ID) return;
  checks_[checkbox] = std::move(fn);
  Rcheckbox_set_changed_callback(checkbox, on_check, this);
}

void Callbacks::choose(Rid combo, std::function<void(int)> fn) {
  if (combo == R_INVALID_ID) return;
  chooses_[combo] = std::move(fn);
  Rcombo_box_set_changed_callback(combo, on_choose, this);
}

void Callbacks::slide(Rid slider, std::function<void(float)> fn) {
  if (slider == R_INVALID_ID) return;
  slides_[slider] = std::move(fn);
  Rslider_set_changed_callback(slider, on_slide, this);
}

void Callbacks::erase(Rid widget) {
  clicks_.erase(widget);
  texts_.erase(widget);
  commits_.erase(widget);
  checks_.erase(widget);
  chooses_.erase(widget);
  slides_.erase(widget);
}

namespace {
template <class Map, class... Args>
void call(Map& map, Rid w, Args&&... args) {
  const auto it = map.find(w);
  if (it == map.end()) return;
  auto fn = it->second;  // the callback may rebuild the rows (and erase itself)
  fn(std::forward<Args>(args)...);
}
}  // namespace

void Callbacks::on_click(Rid w, void* self) { call(static_cast<Callbacks*>(self)->clicks_, w); }
void Callbacks::on_text(Rid w, const char* text, void* self) {
  call(static_cast<Callbacks*>(self)->texts_, w, std::string(text != nullptr ? text : ""));
}
void Callbacks::on_commit(Rid w, const char* /*text*/, void* self) { call(static_cast<Callbacks*>(self)->commits_, w); }
void Callbacks::on_check(Rid w, int checked, void* self) { call(static_cast<Callbacks*>(self)->checks_, w, checked != 0); }
void Callbacks::on_choose(Rid w, int index, void* self) { call(static_cast<Callbacks*>(self)->chooses_, w, index); }
void Callbacks::on_slide(Rid w, float value, void* self) { call(static_cast<Callbacks*>(self)->slides_, w, value); }

void LineList::set(const std::vector<game::Line>& lines) {
  if (box_ == R_INVALID_ID) return;
  while (labels_.size() < lines.size()) {
    const Rid label = Rlabel_create("");
    Rlabel_set_wrap(label, R_TRUE);
    Rlabel_set_text_size(label, size_);
    Rwidget_add_child(box_, label);
    labels_.push_back(label);
  }
  while (labels_.size() > lines.size()) {
    Rwidget_destroy(labels_.back());
    labels_.pop_back();
  }
  for (std::size_t i = 0; i < lines.size(); ++i) {
    ui::set_text(labels_[i], lines[i].text);
    set_color(labels_[i], lines[i].tone);
  }
}

}  // namespace tradelab::desktop::view
