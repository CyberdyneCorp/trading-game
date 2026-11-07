#include "view/app_view.hpp"

#include <chrono>

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_draw.h>
#include <raven/raven_layout.h>
#include <raven/raven_locale.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>
#include <raven/raven_xml.h>

#include <cstdio>
#include <string>

#include "model/resources.hpp"
#include "view/chart_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/game/builder_widgets.hpp"
#include "viewmodel/screen_names.hpp"
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop {

namespace {

void on_chart_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* user_data) {
  const auto* vm = static_cast<const AppViewModel*>(user_data);
  paint_chart(painter, vm->chart(width, height), width, height);
}

int on_window_event(Rid /*window*/, const REvent* e, void* user_data) {
  return static_cast<AppView*>(user_data)->on_key(e) ? R_TRUE : R_FALSE;
}

void on_qty_changed(Rid /*input*/, const char* text, void* user_data) {
  static_cast<AppViewModel*>(user_data)->set_qty_text(text ? text : "");
}

void on_mission_selected(Rid /*list*/, int index, void* user_data) {
  static_cast<AppViewModel*>(user_data)->select(index);
}

void on_mission_activated(Rid /*list*/, int index, void* user_data) {
  auto* vm = static_cast<AppViewModel*>(user_data);
  vm->select(index);
  vm->start_selected();
}

// The first backend error of Rinit names the cause ("the Vulkan library was not found", "no surface
// extension"); the last one only says that Rinit failed.
std::string& first_backend_error() {
  static std::string first;
  return first;
}

void on_raven_error(RError code, RSeverity severity, const char* detail) {
  if (severity == R_SEVERITY_WEAK_WARNING) return;
  if (code == R_ERROR_BACKEND && first_backend_error().empty() && detail != nullptr) first_backend_error() = detail;
  std::fprintf(stderr, "[Raven] %s (%s): %s\n", Rerror_to_string(code), Rseverity_to_string(severity),
               detail ? detail : "");
}

void common_settings() {
  // Views come from memory: never pick up stray *.csv / *.xml from PATH or the current folder.
  Rxml_set_auto_load(R_FALSE);
  Rxml_set_live_reload(R_FALSE);
  const std::string_view csv = resource_text("i18n/strings.csv");
  Rlocale_load_csv_from_memory(csv.data(), csv.size());
}

}  // namespace

bool init_headless() {
  Rerror_set_callback(on_raven_error);
  RInitDesc desc{};
  desc.backend = R_BACKEND_NULL;
  desc.app_name = "TradeLab";
  desc.headless = R_TRUE;
  desc.worker_threads = -1;
  if (!Rinit_ex(&desc)) return false;
  common_settings();
  return true;
}

bool init_vulkan(std::string& detail) {
  Rerror_set_callback(on_raven_error);
  RInitDesc desc{};
  desc.backend = R_BACKEND_VULKAN;
  desc.app_name = "TradeLab";
  first_backend_error().clear();
  if (!Rinit_ex(&desc)) {
    detail = first_backend_error().empty() ? ui::last_error() : first_backend_error();
    return false;
  }
  common_settings();
  return true;
}

bool AppView::build(int width, int height) {
  window_ = Rwindow_create("TradeLab", width, height, R_WINDOW_DEFAULT);
  if (window_ == R_INVALID_ID) return false;
  Rwindow_set_min_size(window_, 1024, 768);
  Rwindow_set_clear_color(window_, 0x0B / 255.0f, 0x10 / 255.0f, 0x10 / 255.0f, 1.0f);
  switcher_ = Rwidget_create(R_WIDGET_SWITCHER);
  for (std::size_t i = 0; i < names::kViews.size(); ++i) {
    screens_[i] = ui::widget_from_xml(resource_text(names::kViews[i]));
    if (screens_[i] == R_INVALID_ID) return false;
    Rwidget_add_child(switcher_, screens_[i]);
  }
  // The shell screens' glue and the retro look: theme, fonts, CRT overlay stacked over the screens.
  shell_ = std::make_unique<ShellView>(vm_, switcher_);
  Rwindow_set_content(window_, shell_->look().create(switcher_));
  RAccessibilitySettings system{};
  Raccessibility_get_system_settings(&system);
  vm_.set_reduce_motion(system.reduce_motion != 0);
  Raccessibility_set_focus_always_visible(R_TRUE);  // keyboard-only play: the focus is always shown
  bind_actions();
  bind_widgets();
  game_view_ = std::make_unique<view::GameView>(vm_, screens_[static_cast<std::size_t>(Screen::Game)], window_);
  game_view_->bind();
  coach_view_ = std::make_unique<view::CoachView>(vm_, switcher_, window_);
  lab_screen_ = std::make_unique<view::LabScreen>(vm_, screens_[static_cast<std::size_t>(Screen::Lab)]);
  lab_screen_->bind();
  refresh();
  return true;
}

bool AppView::on_key(const REvent* e) {
  if (ShellView::capture_key(vm_, e)) return true;  // Settings waits for a new shortcut key
  if (ShellView::tutorial_key(vm_, window_, e)) return true;
  if (coach_view_) {
    if (const auto used = coach_view_->on_key(e)) return *used;  // Esc skips; the callout's keys are its own
  }
  if (vm_.screen() != Screen::Game || !game_view_) return false;
  return game_view_->on_key(e);
}

Rid AppView::find(std::string_view name) const { return ui::find(switcher_, name); }

void AppView::bind_actions() {
  AppViewModel& vm = vm_;
  ui::bind_action(names::kActStart, [&vm] { vm.start_selected(); });
  ui::bind_action(names::kActLanguage, [&vm] { vm.toggle_locale(); });
  ui::bind_action(names::kActBriefGo, [&vm] { vm.open_terminal(); });
  ui::bind_action(names::kActBriefBack, [&vm] { vm.back_to_menu(); });
  ui::bind_action(names::kActAdvance, [&vm] { vm.advance(1); });
  ui::bind_action(names::kActBuy, [&vm] { vm.buy(); });
  ui::bind_action(names::kActSell, [&vm] { vm.sell(); });
  ui::bind_action(names::kActGameMenu, [&vm] { vm.back_to_menu(); });
  ui::bind_action(names::kActDebriefMenu, [&vm] { vm.back_to_menu(); });
  ui::bind_action(names::kActAgain, [&vm] { vm.play_again(); });
  ui::bind_action("tl.menu.lab", [&vm] { vm.open_lab(); });
  shell_->bind();
}

void AppView::bind_widgets() {
  Rcustom_set_paint_callback(find(names::kGameChart), on_chart_paint, &vm_);
  Rtext_input_set_changed_callback(find(names::kGameQty), on_qty_changed, &vm_);
  const Rid list = find(names::kMenuList);
  Rlist_view_set_selection_changed_callback(list, on_mission_selected, &vm_);
  Rlist_view_set_activated_callback(list, on_mission_activated, &vm_);
  Rwindow_set_event_hook(window_, on_window_event, this);
}

void AppView::apply_bindings() {
  shell_->apply(vm_.screen_model());
  const Settings& s = vm_.settings();
  shell_->look().apply(s.colorblind, s.text_scale, vm_.crt_state());
}

void AppView::refresh() {
  const auto now = std::chrono::steady_clock::now();
  vm_.tick_audio(std::chrono::duration<double>(now - last_frame_).count());
  last_frame_ = now;
  vm_.tick_coach();
  if (game_view_) game_view_->refresh();
  shell_->focus_help(window_);
  if (coach_view_) coach_view_->refresh();
  if (lab_screen_) lab_screen_->refresh();
  if (vm_.revision() == shown_revision_) return;
  shown_revision_ = vm_.revision();
  Rswitcher_set_index(switcher_, static_cast<int>(vm_.screen()));
  if (vm_.screen() == Screen::Game) ui::set_text(find(names::kGameQty), vm_.qty_text());
  apply_bindings();
  Rwidget_set_enabled(find(names::kMenuStart), vm_.waiting() ? R_FALSE : R_TRUE);
  Rwidget_invalidate(find(names::kGameChart));
  if (vm_.screen() == Screen::Debrief) Rwidget_invalidate(find(names::kDebriefChart));
  if (vm_.screen() == Screen::Lesson) Rwidget_invalidate(find(names::kLessonPayoff));
  if (coach_view_) coach_view_->refresh();
}

void AppView::missing_actions(std::span<const std::string_view> actions, std::vector<std::string>& missing) {
  for (std::string_view a : actions) {
    if (!ui::action_bound(a)) missing.push_back("action " + std::string(a));
  }
}

// The CRT overlay is not under the screens' Switcher: RetroLook owns it.
void AppView::missing_widgets(std::span<const std::string_view> widgets, std::vector<std::string>& missing) const {
  for (std::string_view w : widgets) {
    if (w != names::kCrtOverlay && find(w) == R_INVALID_ID) missing.push_back("widget " + std::string(w));
  }
}

std::vector<std::string> AppView::missing_names() const {
  std::vector<std::string> missing;
  missing_widgets(names::kWidgets, missing);
  missing_widgets(names::kShellWidgets, missing);
  missing_widgets(names::kFlowWidgets, missing);
  missing_widgets(names::kAcademyWidgets, missing);
  if (shell_->look().overlay() == R_INVALID_ID) missing.push_back("widget " + std::string(names::kCrtOverlay));
  missing_actions(names::kActions, missing);
  missing_actions(names::kShellActions, missing);
  missing_actions(names::kFlowActions, missing);
  missing_actions(names::kAcademyActions, missing);
  if (game_view_) {
    for (auto& m : game_view_->missing_names()) missing.push_back(std::move(m));
  }
  if (lab_screen_) {
    for (auto& m : lab_screen_->missing_names()) missing.push_back(std::move(m));
  }
  if (find(names::builder::kMenuLab) == R_INVALID_ID) missing.push_back("widget " + std::string(names::builder::kMenuLab));
  return missing;
}

}  // namespace tradelab::desktop
