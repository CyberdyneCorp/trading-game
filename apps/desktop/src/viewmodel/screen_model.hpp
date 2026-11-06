// What a screen shows, as plain data the view copies into named widgets: texts, screen-reader names,
// lines of toned text (a VBox of labels), tables, list items and visible / enabled / checked flags.
// Viewmodels build it; the view (view/screens) applies it without deciding anything.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tradelab::desktop {

// The web's text classes (up, down, attention, info, muted) and the title accent.
enum class Tone : std::uint8_t { Normal, Muted, Up, Down, Attention, Info, Accent, Title };

std::string_view tone_class(Tone tone);  // theme style class: "", "muted", "up", ...

struct Line {
  std::string text;
  Tone tone = Tone::Normal;
};

struct WidgetText {
  std::string_view widget;
  std::string text;
};

struct WidgetLines {
  std::string_view widget;  // a VBox: one Label per line
  std::vector<Line> lines;
};

struct WidgetTable {
  std::string_view widget;
  std::vector<std::string> columns;
  std::vector<std::vector<std::string>> rows;
};

struct WidgetItems {
  std::string_view widget;  // ListView or ComboBox
  std::vector<std::string> items;
  int selected = -1;
};

struct WidgetFlag {
  std::string_view widget;
  bool on = false;
};

struct WidgetValue {
  std::string_view widget;  // ProgressBar: 0..1
  float value = 0;
};

struct ScreenModel {
  std::vector<WidgetText> texts;
  std::vector<WidgetText> names;  // accessible names (what a screen reader says)
  std::vector<WidgetLines> lines;
  std::vector<WidgetTable> tables;
  std::vector<WidgetItems> items;
  std::vector<WidgetFlag> visible;
  std::vector<WidgetFlag> enabled;
  std::vector<WidgetFlag> checked;
  std::vector<WidgetValue> values;

  void text(std::string_view w, std::string t) { texts.push_back({w, std::move(t)}); }
  void name(std::string_view w, std::string t) { names.push_back({w, std::move(t)}); }
  void show(std::string_view w, bool on) { visible.push_back({w, on}); }
  void enable(std::string_view w, bool on) { enabled.push_back({w, on}); }
  void check(std::string_view w, bool on) { checked.push_back({w, on}); }
  // The text of a named widget ("" when absent): tests read screens through this.
  [[nodiscard]] std::string text_of(std::string_view w) const;
  [[nodiscard]] const WidgetLines* lines_of(std::string_view w) const;
  [[nodiscard]] const WidgetTable* table_of(std::string_view w) const;
  [[nodiscard]] bool is_visible(std::string_view w, bool fallback = true) const;
  [[nodiscard]] bool is_enabled(std::string_view w, bool fallback = true) const;
  [[nodiscard]] const WidgetItems* items_of(std::string_view w) const;
};

}  // namespace tradelab::desktop
