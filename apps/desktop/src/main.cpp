// TradeLab desktop (Raven, Linux + macOS). Guest-only and offline in this release.
//   tradelab [--seed N]               N: fixed market seed (reproducible sessions, like the web's ?seed=)
//   tradelab --smoke-test REPORT      packaged-build check: opens the window, renders the menu, writes
//                                     REPORT (JSON: menu shown, missing names, Vulkan library) and exits

#include <raven/raven_core.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <random>
#include <string>

#include "model/i18n.hpp"
#include "model/storage/json_writer.hpp"
#include "model/update/updater.hpp"
#include "model/storage/paths.hpp"
#include "model/storage/storage.hpp"
#include "platform/platform.hpp"
#include "view/app.hpp"
#include "viewmodel/vulkan_message.hpp"

namespace td = tradelab::desktop;

namespace {

td::AppViewModel::SeedSource seed_source(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--seed") == 0) {
      const auto seed = static_cast<std::uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
      return [seed] { return seed; };
    }
  }
  return [] {
    static std::mt19937 gen{std::random_device{}()};
    return static_cast<std::uint32_t>(gen() % 1'000'000'000u);
  };
}

// Saved data (model/storage) in the platform's user-data folder, held by one instance at a time. A
// second instance shows "already open" and exits; a folder or database that cannot be opened leaves this
// session unsaved (stderr says why) rather than blocking play.
struct SavedData {
  std::optional<std::filesystem::path> storage_dir;
  std::unique_ptr<td::storage::InstanceLock> lock;
  std::unique_ptr<td::storage::Storage> storage;
  bool already_running = false;
};

SavedData open_saved_data(const td::Catalog& catalog) {
  SavedData out;
  const auto dir = td::storage::user_data_dir();
  if (!dir) return out;
  out.storage_dir = dir;
  out.lock = std::make_unique<td::storage::InstanceLock>(*dir);
  if (!out.lock->held()) {
    out.already_running = true;
    return out;
  }
  try {
    out.storage = std::make_unique<td::storage::Storage>(*dir);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", catalog.tr("desktop.storageUnavailable", {{"detail", e.what()}}).c_str());
  }
  return out;
}

const char* arg_value(int argc, char** argv, const char* name) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  }
  return nullptr;
}

// --smoke-test: the report a package smoke test (scripts/smoke_*.sh) reads; exit 0 when the menu showed
// complete.
int write_smoke_report(td::App& app, const char* path, const std::string& bundled_vulkan) {
  const td::App::SmokeResult r = app.smoke(30);
  td::storage::JsonWriter w;
  w.begin_object().field("menu", r.menu).field("vulkan", td::platform::loaded_vulkan_library());
  w.field("bundledVulkan", bundled_vulkan).field("executable", td::platform::executable_path());
  w.key("missing").begin_array();
  for (const auto& m : r.missing) w.string(m);
  w.end_array();
  const std::string text = w.end_object().text();
  std::FILE* f = std::fopen(path, "w");
  if (f == nullptr) return 1;
  std::fputs(text.c_str(), f);
  std::fclose(f);
  return r.menu && r.missing.empty() ? 0 : 1;
}

// A release the player chose to install (menu) is staged in <data>/updates: it replaces this copy by
// rename and starts instead. A refused one is dropped and this version goes on.
void apply_staged_update(const td::Catalog& catalog, char** argv) {
  const auto dir = td::storage::user_data_dir();
  if (!dir) return;
  const td::update::Applied applied = td::update::apply_pending(*dir / "updates");
  if (applied.applied) {
    ::execv(applied.executable.c_str(), argv);  // returns only on failure
  }
  if (!applied.error.empty() || applied.applied) {
    const std::string reason = applied.error.empty() ? "exec" : applied.error;
    std::fprintf(stderr, "%s\n", catalog.tr("desktop.update.applyFailed", {{"version", applied.version}, {"reason", reason}}).c_str());
  }
}

constexpr td::HostOs kHostOs =
#if defined(__APPLE__)
    td::HostOs::MacOS;
#else
    td::HostOs::Linux;
#endif

}  // namespace

int main(int argc, char** argv) {
  td::Catalog catalog = td::builtin_catalog();
  catalog.set_locale(td::locale_from_system(td::platform::system_language()));

  const char* smoke_report = arg_value(argc, argv, "--smoke-test");
  if (smoke_report == nullptr) apply_staged_update(catalog, argv);
  const std::string bundled_vulkan = td::platform::preload_bundled_vulkan();
  std::string detail;
  if (!td::init_vulkan(detail)) {
    const td::AlertText alert = td::vulkan_missing_message(catalog, kHostOs, detail);
    td::platform::show_error_alert(alert.title, alert.body);
    return td::kExitNoVulkan;
  }
  SavedData saved = open_saved_data(catalog);
  if (saved.already_running) {
    td::platform::show_error_alert(catalog.tr("desktop.alreadyRunning.title"), catalog.tr("desktop.alreadyRunning.body"));
    Rshutdown();
    return 1;
  }
  int code = 1;
  {
    td::App app(catalog, seed_source(argc, argv), std::move(saved.storage));
    if (app.build()) {
      const auto installed = td::update::installed_target(td::platform::executable_path());
      if (smoke_report == nullptr && installed && saved.storage_dir) app.enable_updates(*saved.storage_dir / "updates", *installed);
      code = smoke_report != nullptr ? write_smoke_report(app, smoke_report, bundled_vulkan) : app.run();
    }
  }
  Rshutdown();
  return code;
}
