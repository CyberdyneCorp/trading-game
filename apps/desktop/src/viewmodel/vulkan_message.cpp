#include "viewmodel/vulkan_message.hpp"

namespace tradelab::desktop {

AlertText vulkan_missing_message(const Catalog& catalog, HostOs os, std::string_view detail) {
  AlertText out;
  out.title = catalog.tr("desktop.vulkanMissing.title");
  out.body = catalog.tr("desktop.vulkanMissing.body") + "\n\n" +
             catalog.tr(os == HostOs::MacOS ? "desktop.vulkanMissing.macos" : "desktop.vulkanMissing.linux");
  if (!detail.empty()) out.body += "\n\n" + catalog.tr("desktop.vulkanMissing.detail", {{"detail", std::string(detail)}});
  return out;
}

}  // namespace tradelab::desktop
