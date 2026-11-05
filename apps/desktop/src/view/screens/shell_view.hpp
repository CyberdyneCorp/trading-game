// View glue of the shell screens (menu, briefing, debrief, curriculum map, journal, lesson, settings):
// binds their actions and widget callbacks to AppViewModel commands and copies a ScreenModel into the
// named widgets (texts, screen-reader names, toned lines, tables, list items, flags). It decides
// nothing: what to show is the viewmodel's.
#pragma once

#include <raven/raven_core.h>

#include <map>
#include <string>
#include <vector>

#include "view/screens/retro_look.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/screen_model.hpp"

struct REvent;

namespace tradelab::desktop {

class ShellView {
 public:
  ShellView(AppViewModel& vm, Rid scope) : vm_(vm), scope_(scope) {}

  void bind();
  // Copies the model into the widgets; unchanged lines, tables and items are left alone.
  void apply(const ScreenModel& m);
  [[nodiscard]] RetroLook& look() { return look_; }

  // While Settings waits for a new shortcut key, the key press goes there. True when consumed.
  static bool capture_key(AppViewModel& vm, const REvent* e);

 private:
  [[nodiscard]] Rid find(std::string_view name) const;
  void apply_lines(const WidgetLines& l);
  void apply_table(const WidgetTable& t);
  void apply_items(const WidgetItems& it);
  void apply_flags(const ScreenModel& m);
  void bind_callbacks();

  AppViewModel& vm_;
  Rid scope_;
  RetroLook look_;
  std::map<std::string, std::vector<Line>, std::less<>> lines_;
  std::map<std::string, WidgetTable, std::less<>> tables_;
  std::map<std::string, WidgetItems, std::less<>> items_;
};

}  // namespace tradelab::desktop
