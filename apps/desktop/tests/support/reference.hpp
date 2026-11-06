// Reference images with a tolerance (tasks 1.17): a window capture is reduced to 320x200 by box
// averaging (so the display scale, 1x on a hosted runner or 2x on a Retina Mac, and anti-aliasing
// details do not matter) and compared pixel by pixel with tests/windowed/reference/<name>.png. A pixel
// differs when a channel is off by more than `channel`; the image matches while at most `share` of its
// pixels differ. TRADELAB_UPDATE_REFERENCES=1 writes the references instead (review them, then commit).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tradelab::desktop::testing {

struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
};

inline constexpr int kReferenceWidth = 320;
inline constexpr int kReferenceHeight = 200;

Image downscale(const std::uint8_t* rgba, int width, int height, int out_width, int out_height);
// 8-bit RGBA PNGs as write_png writes them (any row filter); none without zlib or on any other format.
std::optional<Image> read_png(const std::string& path);

struct ImageDiff {
  bool same_size = false;
  double share = 1;  // of pixels that differ
  int worst = 255;   // largest channel difference
};
ImageDiff compare(const Image& a, const Image& b, int channel);

struct ReferenceResult {
  bool compared = false;  // false: written (update mode) or no zlib to read PNGs
  ImageDiff diff;
  std::string message;
};
// `rgba` is the full capture. The reduced capture is also saved as <capture dir>/reference/<name>.png.
ReferenceResult check_reference(const std::string& reference_dir, const std::string& capture_dir, const std::string& name,
                                const std::uint8_t* rgba, int width, int height, int channel = 40, double share = 0.02);

}  // namespace tradelab::desktop::testing
