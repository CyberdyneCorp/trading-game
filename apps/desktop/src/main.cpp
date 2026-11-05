// TradeLab desktop (Raven, Linux + macOS). Guest-only and offline in this release.
//   tradelab_desktop [--seed N]     N: fixed market seed (reproducible sessions, like the web's ?seed=)

#include <raven/raven_core.h>

#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "model/i18n.hpp"
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
  int code = 1;
  {
    td::App app(catalog, seed_source(argc, argv));
    if (app.build()) code = app.run();
  }
  Rshutdown();
  return code;
}
