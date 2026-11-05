#include "support/png.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#if defined(TRADELAB_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace tradelab::desktop::testing {

namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(v >> s));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
  put32(out, static_cast<std::uint32_t>(data.size()));
  std::vector<std::uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  put32(out, crc32(body.data(), body.size()));
}

#if defined(TRADELAB_HAVE_ZLIB)
std::vector<std::uint8_t> zlib_stream(const std::vector<std::uint8_t>& raw) {
  uLongf size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::uint8_t> z(size);
  if (compress2(z.data(), &size, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) return {};
  z.resize(size);
  return z;
}
#else
// zlib stream of stored (uncompressed) deflate blocks.
std::vector<std::uint8_t> zlib_stream(const std::vector<std::uint8_t>& raw) {
  std::vector<std::uint8_t> z = {0x78, 0x01};
  std::size_t pos = 0;
  do {
    const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
    const bool last = pos + len == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(static_cast<std::uint8_t>(len & 0xFF));
    z.push_back(static_cast<std::uint8_t>(len >> 8));
    z.push_back(static_cast<std::uint8_t>(~len & 0xFF));
    z.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xFF));
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
    pos += len;
  } while (pos < raw.size());
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t byte : raw) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);
  return z;
}
#endif

}  // namespace

bool write_png(const std::string& path, const std::uint8_t* rgba, int width, int height) {
  if (rgba == nullptr || width <= 0 || height <= 0) return false;
  const std::size_t stride = static_cast<std::size_t>(width) * 4;
  std::vector<std::uint8_t> raw;
  raw.reserve((stride + 1) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);  // filter: none
    const std::uint8_t* row = rgba + stride * static_cast<std::size_t>(y);
    raw.insert(raw.end(), row, row + stride);
  }
  std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<std::uint8_t> ihdr;
  put32(ihdr, static_cast<std::uint32_t>(width));
  put32(ihdr, static_cast<std::uint32_t>(height));
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", zlib_stream(raw));
  chunk(png, "IEND", {});
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace tradelab::desktop::testing
