// Minimal PNG writer for test captures (RGBA8). Compressed with zlib when the build found it, else
// stored deflate blocks (valid, just large).
#pragma once

#include <cstdint>
#include <string>

namespace tradelab::desktop::testing {

bool write_png(const std::string& path, const std::uint8_t* rgba, int width, int height);

}  // namespace tradelab::desktop::testing
