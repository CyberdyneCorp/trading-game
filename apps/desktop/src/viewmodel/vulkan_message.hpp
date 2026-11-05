// Text of the "no Vulkan driver" alert (design "No Vulkan driver"): shown natively before exiting with
// status 3, since no Raven window can exist without a driver.
#pragma once

#include <string>
#include <string_view>

#include "model/i18n.hpp"

namespace tradelab::desktop {

inline constexpr int kExitNoVulkan = 3;

enum class HostOs { Linux, MacOS };

struct AlertText {
  std::string title;
  std::string body;  // reason, platform fix and the library's detail, separated by blank lines
};

AlertText vulkan_missing_message(const Catalog& catalog, HostOs os, std::string_view detail);

}  // namespace tradelab::desktop
