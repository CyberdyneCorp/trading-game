#include "view/raven/raven_ui.hpp"

#include <raven/raven_controls.h>
#include <raven/raven_widget.h>
#include <raven/raven_xml.h>

#include <map>
#include <memory>

namespace tradelab::desktop::ui {

namespace {

std::map<std::string, std::unique_ptr<std::function<void()>>, std::less<>>& actions() {
  static std::map<std::string, std::unique_ptr<std::function<void()>>, std::less<>> map;
  return map;
}

void run_action(Rid /*sender*/, const char* /*action*/, void* user_data) {
  (*static_cast<std::function<void()>*>(user_data))();
}

void run_posted(void* user_data) {
  std::unique_ptr<Task> task(static_cast<Task*>(user_data));
  (*task)();
}

}  // namespace

void post(Task task) { Rtask_post(run_posted, new Task(std::move(task))); }

void bind_action(std::string_view name, std::function<void()> fn) {
  auto& slot = actions()[std::string(name)];
  slot = std::make_unique<std::function<void()>>(std::move(fn));
  Raction_bind(std::string(name).c_str(), run_action, slot.get());
}

bool action_bound(std::string_view name) { return actions().contains(name); }

void fire_action(std::string_view name) { Raction_fire(std::string(name).c_str(), R_INVALID_ID); }

Rid widget_from_xml(std::string_view xml) { return Rwidget_create_from_xml(xml.data(), xml.size()); }

Rid find(Rid scope, std::string_view name) { return Rwidget_find(scope, std::string(name).c_str()); }

void set_text(Rid widget, const std::string& text) {
  switch (Rwidget_get_type(widget)) {
    case R_WIDGET_LABEL: {
      const char* cur = Rlabel_get_text(widget);
      if (cur == nullptr || text != cur) Rlabel_set_text(widget, text.c_str());
      break;
    }
    case R_WIDGET_BUTTON: Rbutton_set_text(widget, text.c_str()); break;
    case R_WIDGET_CHECKBOX: Rcheckbox_set_text(widget, text.c_str()); break;
    case R_WIDGET_TEXT_INPUT: {
      const char* cur = Rtext_input_get_text(widget);
      if (cur == nullptr || text != cur) Rtext_input_set_text(widget, text.c_str());
      break;
    }
    default: Rwidget_set_attribute(widget, "Text", text.c_str()); break;
  }
}

std::string text_of(Rid widget) {
  const char* text = nullptr;
  if (Rwidget_get_type(widget) == R_WIDGET_LABEL) text = Rlabel_get_text(widget);
  if (Rwidget_get_type(widget) == R_WIDGET_TEXT_INPUT) text = Rtext_input_get_text(widget);
  return text ? text : "";
}

std::string last_error() {
  const char* detail = nullptr;
  const RError e = Rerror_get_last(&detail, nullptr);
  if (e == R_NO_ERROR) return {};
  return std::string(Rerror_to_string(e)) + ": " + (detail ? detail : "");
}

}  // namespace tradelab::desktop::ui
