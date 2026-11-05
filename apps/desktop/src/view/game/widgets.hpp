// Small Raven helpers for the terminal view: callbacks kept per widget (so rows built at run time can
// carry their own action), tone colours, visibility, and lists of coloured text lines.
#pragma once

#include <raven/raven_core.h>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "viewmodel/game/texts.hpp"

namespace tradelab::desktop::view {

// The terminal-ui palette (apps/web/src/app.css) as "#RRGGBB".
std::string_view tone_color(game::Tone tone);

void set_visible(Rid widget, bool visible);
void set_color(Rid label, game::Tone tone);
void set_enabled(Rid widget, bool enabled);
void set_label(Rid label, const std::string& text);

// Owns the C callbacks of the widgets it wires; erase() before a widget is destroyed.
class Callbacks {
 public:
  void click(Rid button, std::function<void()> fn);
  void text(Rid input, std::function<void(const std::string&)> fn);
  void commit(Rid input, std::function<void()> fn);
  void check(Rid checkbox, std::function<void(bool)> fn);
  void choose(Rid combo, std::function<void(int)> fn);
  void slide(Rid slider, std::function<void(float)> fn);
  void erase(Rid widget);

 private:
  static void on_click(Rid w, void* self);
  static void on_text(Rid w, const char* text, void* self);
  static void on_commit(Rid w, const char* text, void* self);
  static void on_check(Rid w, int checked, void* self);
  static void on_choose(Rid w, int index, void* self);
  static void on_slide(Rid w, float value, void* self);

  std::unordered_map<Rid, std::function<void()>> clicks_;
  std::unordered_map<Rid, std::function<void(const std::string&)>> texts_;
  std::unordered_map<Rid, std::function<void()>> commits_;
  std::unordered_map<Rid, std::function<void(bool)>> checks_;
  std::unordered_map<Rid, std::function<void(int)>> chooses_;
  std::unordered_map<Rid, std::function<void(float)>> slides_;
};

// A VBox of labels showing lines in their tone; labels are reused as the lines change.
class LineList {
 public:
  LineList() = default;
  LineList(Rid box, float text_size) : box_(box), size_(text_size) {}
  void set(const std::vector<game::Line>& lines);
  [[nodiscard]] std::size_t size() const { return labels_.size(); }

 private:
  Rid box_ = R_INVALID_ID;
  float size_ = 12;
  std::vector<Rid> labels_;
};

}  // namespace tradelab::desktop::view
