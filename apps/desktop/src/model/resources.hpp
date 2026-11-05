// Files of apps/desktop/resources embedded in the binary at configure time (cmake/EmbedResources.cmake),
// so the app runs from any folder and release builds read no loose files.
#pragma once

#include <span>
#include <string_view>

namespace tradelab::desktop {

struct Resource {
  std::string_view path;  // relative to resources/, e.g. "views/menu.xml"
  std::string_view data;
};

std::span<const Resource> resources();

// The resource's bytes, or an empty view when there is none at `path`.
std::string_view resource_text(std::string_view path);

}  // namespace tradelab::desktop
