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
  // The tutorial's keys (Tutorial.svelte): Enter / Right next, Left previous, Esc skips. A key pressed
  // on a focused button is the button's (it activates it), so Enter there is left alone.
  static bool tutorial_key(AppViewModel& vm, Rid window, const REvent* e);
  // Once per frame: the focused term's definition on the screen's help line (shell_view_academy.cpp).
  void focus_help(Rid window);

 private:
  [[nodiscard]] Rid find(std::string_view name) const;
  void apply_lines(const WidgetLines& l);
  void apply_table(const WidgetTable& t);
  void apply_items(const WidgetItems& it);
  void apply_flags(const ScreenModel& m);
  void bind_callbacks();
  // Tutorial, catalog, CSV import, assisted practice and the debrief chart (shell_view_flows.cpp).
  void bind_flows();
  void bind_flow_callbacks();
  void apply_values(const ScreenModel& m);
  // Academy, glossary, review prompt, coach steps and marked terms (shell_view_academy.cpp).
  void bind_academy();
  void bind_academy_callbacks();
  void apply_tips(const ScreenModel& m);

  AppViewModel& vm_;
  Rid scope_;
  RetroLook look_;
  std::map<std::string, std::vector<Line>, std::less<>> lines_;
  std::map<std::string, WidgetTable, std::less<>> tables_;
  std::map<std::string, WidgetItems, std::less<>> items_;
};

}  // namespace tradelab::desktop
