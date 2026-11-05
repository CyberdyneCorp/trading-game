// TradeLab desktop (Raven, Linux + macOS). Guest-only and offline in this release.
//   tradelab_desktop [--seed N]     N: fixed market seed (reproducible sessions, like the web's ?seed=)

#include <raven/raven_core.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <random>
#include <string>

#include "model/i18n.hpp"
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
  std::unique_ptr<td::storage::InstanceLock> lock;
  std::unique_ptr<td::storage::Storage> storage;
  bool already_running = false;
};

SavedData open_saved_data(const td::Catalog& catalog) {
  SavedData out;
  const auto dir = td::storage::user_data_dir();
  if (!dir) return out;
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
    if (app.build()) code = app.run();
  }
  Rshutdown();
  return code;
}
