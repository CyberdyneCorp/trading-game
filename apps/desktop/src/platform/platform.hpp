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

// macOS app bundle: loads Contents/Frameworks/libMoltenVK.dylib by absolute path before Raven looks for
// Vulkan, so Raven's dlopen("libMoltenVK.dylib") resolves to the bundled copy (dyld matches the leaf name
// of a loaded image) whether or not Homebrew has one. Returns the path loaded, "" when not in a bundle
// (or on Linux, where the system's libvulkan.so.1 and GPU driver are used).
std::string preload_bundled_vulkan();

// The file that provides vkGetInstanceProcAddr to Raven (smoke test report); "" when none is loaded.
std::string loaded_vulkan_library();

// Absolute path of the running executable ("" when unknown).
std::string executable_path();

}  // namespace tradelab::desktop::platform
