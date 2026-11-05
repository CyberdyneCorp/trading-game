// OS services outside Raven: the user's language and a native error alert that works when no Raven
// window can be created (no Vulkan driver).
#pragma once

#include <string>

namespace tradelab::desktop::platform {

// The user's preferred language code ("pt-BR", "en-US", "en_GB.UTF-8"...); "" when unknown.
// macOS: the first preferred language; Linux: LC_ALL, LC_MESSAGES, then LANG.
std::string system_language();

// Shows a blocking error alert (macOS: CFUserNotificationDisplayAlert; Linux: zenity or kdialog when on
// PATH and a display is available) and always prints the text to stderr. TRADELAB_NO_DIALOG=1 keeps
// it to stderr (tests, CI).
void show_error_alert(const std::string& title, const std::string& body);

}  // namespace tradelab::desktop::platform
