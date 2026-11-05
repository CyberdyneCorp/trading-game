// Thin C++ adapter over Raven (design: "API churn in a Raven bump is absorbed in one folder").
// Everything here runs on the UI thread except post(), which any thread may call.
#pragma once

#include <raven/raven_core.h>

#include <functional>
#include <string>
#include <string_view>

namespace tradelab::desktop::ui {

using Task = std::function<void()>;

// Runs `task` on the UI thread during the next frame (Rtask_post). Thread-safe.
void post(Task task);

// Binds an action name to a function for the life of the process (Raction_bind).
void bind_action(std::string_view name, std::function<void()> fn);
[[nodiscard]] bool action_bound(std::string_view name);
void fire_action(std::string_view name);

// Builds a widget tree from an XML document; R_INVALID_ID (after reporting) when it does not parse.
Rid widget_from_xml(std::string_view xml);

[[nodiscard]] Rid find(Rid scope, std::string_view name);
// Sets what the widget shows as its text (Label, Button, TextInput; other widgets: the Text attribute).
void set_text(Rid widget, const std::string& text);
[[nodiscard]] std::string text_of(Rid widget);

// The last Raven error as "CODE: detail", or "" when none was recorded.
std::string last_error();

}  // namespace tradelab::desktop::ui
