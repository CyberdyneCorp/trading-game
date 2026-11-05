#include "platform/platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace tradelab::desktop::platform {

namespace {

const char* env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0' ? v : nullptr;
}

std::string env_language() {
  for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    if (const char* v = env(name)) return v;
  }
  return {};
}

#if defined(__APPLE__)

std::string cf_string(CFStringRef s) {
  char buf[256] = {};
  return CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8) ? buf : "";
}

void native_alert(const std::string& title, const std::string& body) {
  CFStringRef t = CFStringCreateWithCString(nullptr, title.c_str(), kCFStringEncodingUTF8);
  CFStringRef b = CFStringCreateWithCString(nullptr, body.c_str(), kCFStringEncodingUTF8);
  CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, nullptr, nullptr, nullptr, t, b, nullptr,
                                 nullptr, nullptr, nullptr);
  CFRelease(t);
  CFRelease(b);
}

#else

bool on_path(const std::string& program) {
  const char* path = env("PATH");
  if (path == nullptr) return false;
  std::string_view rest(path);
  while (!rest.empty()) {
    const auto colon = rest.find(':');
    const std::string dir(rest.substr(0, colon));
    const std::string full = dir + "/" + program;
    struct stat st {};
    if (!dir.empty() && stat(full.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) != 0) return true;
    rest = colon == std::string_view::npos ? std::string_view{} : rest.substr(colon + 1);
  }
  return false;
}

bool run_dialog(const char* const* argv) {
  pid_t pid = 0;
  if (posix_spawnp(&pid, argv[0], nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) return false;
  int status = 0;
  waitpid(pid, &status, 0);
  return true;
}

void native_alert(const std::string& title, const std::string& body) {
  if (env("DISPLAY") == nullptr && env("WAYLAND_DISPLAY") == nullptr) return;
  if (on_path("zenity")) {
    const std::string t = "--title=" + title;
    const std::string b = "--text=" + body;
    const char* argv[] = {"zenity", "--error", "--no-markup", t.c_str(), b.c_str(), nullptr};
    run_dialog(argv);
  } else if (on_path("kdialog")) {
    const char* argv[] = {"kdialog", "--title", title.c_str(), "--error", body.c_str(), nullptr};
    run_dialog(argv);
  }
}

#endif

}  // namespace

std::string system_language() {
#if defined(__APPLE__)
  if (std::string lang = env_language(); !lang.empty()) return lang;
  std::string out;
  if (CFArrayRef langs = CFLocaleCopyPreferredLanguages()) {
    if (CFArrayGetCount(langs) > 0) out = cf_string(static_cast<CFStringRef>(CFArrayGetValueAtIndex(langs, 0)));
    CFRelease(langs);
  }
  return out;
#else
  return env_language();
#endif
}

void show_error_alert(const std::string& title, const std::string& body) {
  std::fprintf(stderr, "%s\n\n%s\n", title.c_str(), body.c_str());
  std::fflush(stderr);
  if (env("TRADELAB_NO_DIALOG") == nullptr) native_alert(title, body);
}

}  // namespace tradelab::desktop::platform
