#include "support/reference.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "support/png.hpp"

#if defined(TRADELAB_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace tradelab::desktop::testing {

namespace {

std::uint32_t get32(const std::vector<std::uint8_t>& b, std::size_t at) {
  return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) | (std::uint32_t{b[at + 2]} << 8) | b[at + 3];
}

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

// Undoes one row's PNG filter in place (bpp 4); `prev` is the previous unfiltered row or empty.
bool unfilter(std::uint8_t type, std::uint8_t* row, const std::uint8_t* prev, std::size_t stride) {
  for (std::size_t i = 0; i < stride; ++i) {
    const int a = i >= 4 ? row[i - 4] : 0;
    const int b = prev != nullptr ? prev[i] : 0;
    const int c = prev != nullptr && i >= 4 ? prev[i - 4] : 0;
    int add = 0;
    switch (type) {
      case 0: add = 0; break;
      case 1: add = a; break;
      case 2: add = b; break;
      case 3: add = (a + b) / 2; break;
      case 4: add = paeth(a, b, c); break;
      default: return false;
    }
    row[i] = static_cast<std::uint8_t>(row[i] + add);
  }
  return true;
}

}  // namespace

Image downscale(const std::uint8_t* rgba, int width, int height, int out_width, int out_height) {
  Image out{out_width, out_height, std::vector<std::uint8_t>(static_cast<std::size_t>(out_width) * out_height * 4)};
  for (int oy = 0; oy < out_height; ++oy) {
    const int y0 = oy * height / out_height, y1 = std::max(y0 + 1, (oy + 1) * height / out_height);
    for (int ox = 0; ox < out_width; ++ox) {
      const int x0 = ox * width / out_width, x1 = std::max(x0 + 1, (ox + 1) * width / out_width);
      std::array<long, 4> sum{};
      for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
          const std::uint8_t* p = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
          for (int k = 0; k < 4; ++k) sum[k] += p[k];
        }
      }
      const long n = static_cast<long>(y1 - y0) * (x1 - x0);
      std::uint8_t* q = &out.rgba[(static_cast<std::size_t>(oy) * out_width + ox) * 4];
      for (int k = 0; k < 4; ++k) q[k] = static_cast<std::uint8_t>(sum[k] / n);
    }
  }
  return out;
}

std::optional<Image> read_png(const std::string& path) {
#if defined(TRADELAB_HAVE_ZLIB)
  std::ifstream in(path, std::ios::binary);
  const std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (b.size() < 33 || b[1] != 'P' || b[2] != 'N' || b[3] != 'G') return std::nullopt;
  Image img;
  std::vector<std::uint8_t> z;
  for (std::size_t at = 8; at + 8 <= b.size();) {
    const std::uint32_t len = get32(b, at);
    const std::string type(b.begin() + static_cast<std::ptrdiff_t>(at + 4), b.begin() + static_cast<std::ptrdiff_t>(at + 8));
    if (at + 12 + len > b.size()) return std::nullopt;
    const std::size_t data = at + 8;
    if (type == "IHDR") {
      img.width = static_cast<int>(get32(b, data));
      img.height = static_cast<int>(get32(b, data + 4));
      if (b[data + 8] != 8 || b[data + 9] != 6 || b[data + 12] != 0) return std::nullopt;  // RGBA8, no interlace
    } else if (type == "IDAT") {
      z.insert(z.end(), b.begin() + static_cast<std::ptrdiff_t>(data), b.begin() + static_cast<std::ptrdiff_t>(data + len));
    }
    at += 12 + len;
  }
  const std::size_t stride = static_cast<std::size_t>(img.width) * 4;
  std::vector<std::uint8_t> raw((stride + 1) * static_cast<std::size_t>(img.height));
  uLongf size = static_cast<uLongf>(raw.size());
  if (img.width <= 0 || uncompress(raw.data(), &size, z.data(), static_cast<uLong>(z.size())) != Z_OK || size != raw.size()) {
    return std::nullopt;
  }
  img.rgba.resize(stride * static_cast<std::size_t>(img.height));
  for (int y = 0; y < img.height; ++y) {
    std::uint8_t* row = &img.rgba[stride * static_cast<std::size_t>(y)];
    std::copy_n(&raw[(stride + 1) * static_cast<std::size_t>(y) + 1], stride, row);
    if (!unfilter(raw[(stride + 1) * static_cast<std::size_t>(y)], row, y > 0 ? row - stride : nullptr, stride)) return std::nullopt;
  }
  return img;
#else
  (void)path;
  return std::nullopt;
#endif
}

ImageDiff compare(const Image& a, const Image& b, int channel) {
  ImageDiff d;
  d.same_size = a.width == b.width && a.height == b.height && a.rgba.size() == b.rgba.size();
  if (!d.same_size || a.rgba.empty()) return d;
  long differ = 0;
  d.worst = 0;
  for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
    int px = 0;
    for (std::size_t k = 0; k < 3; ++k) px = std::max(px, std::abs(int{a.rgba[i + k]} - int{b.rgba[i + k]}));
    d.worst = std::max(d.worst, px);
    if (px > channel) ++differ;
  }
  d.share = static_cast<double>(differ) / static_cast<double>(a.rgba.size() / 4);
  return d;
}

ReferenceResult check_reference(const std::string& reference_dir, const std::string& capture_dir, const std::string& name,
                                const std::uint8_t* rgba, int width, int height, int channel, double share) {
  namespace fs = std::filesystem;
  ReferenceResult r;
  const Image small = downscale(rgba, width, height, kReferenceWidth, kReferenceHeight);
  fs::create_directories(fs::path(capture_dir) / "reference");
  write_png((fs::path(capture_dir) / "reference" / (name + ".png")).string(), small.rgba.data(), small.width, small.height);
  const fs::path ref = fs::path(reference_dir) / (name + ".png");
  const char* update = std::getenv("TRADELAB_UPDATE_REFERENCES");
  if (update != nullptr && *update == '1') {
    fs::create_directories(reference_dir);
    write_png(ref.string(), small.rgba.data(), small.width, small.height);
    r.message = "wrote " + ref.string();
    return r;
  }
  const auto want = read_png(ref.string());
  if (!want) {
    r.message = "cannot read " + ref.string() + " (missing, or a build without zlib)";
    return r;
  }
  r.compared = true;
  r.diff = compare(small, *want, channel);
  r.message = name + ": " + std::to_string(r.diff.share * 100) + "% of pixels differ by more than " +
              std::to_string(channel) + " (worst " + std::to_string(r.diff.worst) + ")";
  r.compared = r.diff.same_size;
  if (r.compared && r.diff.share > share) r.message += "; allowed " + std::to_string(share * 100) + "%";
  return r;
}

}  // namespace tradelab::desktop::testing
