#include "view/app_view.hpp"

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
#include "viewmodel/widget_names.hpp"

namespace tradelab::desktop {

namespace {

void on_chart_paint(Rid /*widget*/, RPainter* painter, float width, float height, void* user_data) {
  const auto* vm = static_cast<const AppViewModel*>(user_data);
  paint_chart(painter, vm->chart(width, height), width, height);
}

int on_window_event(Rid window, const REvent* e, void* user_data) {
  if (e->type != R_EVENT_KEY_DOWN || e->key != R_KEY_SPACE) return R_FALSE;
  auto* vm = static_cast<AppViewModel*>(user_data);
  const Key key = (e->modifiers & R_MOD_SHIFT) != 0 ? Key::ShiftSpace : Key::Space;
  return vm->handle_key(key, Rwindow_wants_text_input(window) != 0) ? R_TRUE : R_FALSE;
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
  Rwindow_set_content(window_, switcher_);
  bind_actions();
  bind_widgets();
  refresh();
  return true;
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
}

void AppView::bind_widgets() {
  Rcustom_set_paint_callback(find(names::kGameChart), on_chart_paint, &vm_);
  Rtext_input_set_changed_callback(find(names::kGameQty), on_qty_changed, &vm_);
  const Rid list = find(names::kMenuList);
  Rlist_view_set_selection_changed_callback(list, on_mission_selected, &vm_);
  Rlist_view_set_activated_callback(list, on_mission_activated, &vm_);
  Rwindow_set_event_hook(window_, on_window_event, &vm_);
}

void AppView::fill_mission_list() {
  const Rid list = find(names::kMenuList);
  if (list_filled_ && list_locale_ == vm_.locale()) return;
  Rlist_view_clear_items(list);
  for (const auto& title : vm_.mission_titles()) Rlist_view_add_item(list, title.c_str());
  Rlist_view_set_selected(list, vm_.selected());
  list_filled_ = true;
  list_locale_ = vm_.locale();
}

void AppView::apply_bindings() {
  for (const Binding& b : vm_.bindings()) {
    const Rid w = find(b.widget);
    if (w != R_INVALID_ID) ui::set_text(w, b.text);
  }
}

void AppView::refresh() {
  if (vm_.revision() == shown_revision_) return;
  shown_revision_ = vm_.revision();
  Rswitcher_set_index(switcher_, static_cast<int>(vm_.screen()));
  if (vm_.screen() == Screen::Menu) fill_mission_list();
  if (vm_.screen() == Screen::Game) ui::set_text(find(names::kGameQty), vm_.qty_text());
  apply_bindings();
  Rwidget_set_enabled(find(names::kMenuStart), vm_.waiting() ? R_FALSE : R_TRUE);
  Rwidget_invalidate(find(names::kGameChart));
}

std::vector<std::string> AppView::missing_names() const {
  std::vector<std::string> missing;
  for (std::string_view w : names::kWidgets) {
    if (find(w) == R_INVALID_ID) missing.push_back("widget " + std::string(w));
  }
  for (std::string_view a : names::kActions) {
    if (!ui::action_bound(a)) missing.push_back("action " + std::string(a));
  }
  return missing;
}

}  // namespace tradelab::desktop
