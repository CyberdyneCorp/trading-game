#include <algorithm>

#include "pack_codec.hpp"
#include "zstd.h"
#include "zstd_errors.h"

namespace tradelab::detail {

ZstdStatus zstd_decompress(std::span<const std::uint8_t> in, std::size_t declared_size, std::size_t hard_cap,
                           std::vector<std::uint8_t>* out) {
  if (declared_size > hard_cap) return ZstdStatus::TooLarge;
  const unsigned long long frame_size = ZSTD_getFrameContentSize(in.data(), in.size());
  if (frame_size == ZSTD_CONTENTSIZE_ERROR) return ZstdStatus::Malformed;
  if (frame_size != ZSTD_CONTENTSIZE_UNKNOWN && frame_size > declared_size) return ZstdStatus::TooLarge;
  // The destination is exactly the declared size: the decoder stops with dstSize_tooSmall instead
  // of writing past it, so a bomb never allocates more than the declaration.
  out->assign(std::max<std::size_t>(declared_size, 1), 0);
  const std::size_t n = ZSTD_decompress(out->data(), declared_size, in.data(), in.size());
  if (ZSTD_isError(n) != 0U) {
    out->clear();
    return ZSTD_getErrorCode(n) == ZSTD_error_dstSize_tooSmall ? ZstdStatus::TooLarge : ZstdStatus::Malformed;
  }
  out->resize(n);
  return n == declared_size ? ZstdStatus::Ok : ZstdStatus::Malformed;
}

std::vector<std::uint8_t> zstd_store(std::span<const std::uint8_t> in) {
  constexpr std::size_t kMaxBlock = std::size_t{128} << 10U;
  std::vector<std::uint8_t> out = {0x28, 0xB5, 0x2F, 0xFD};  // magic, little-endian
  out.push_back(0xE0);  // frame header: single segment, 8-byte content size, no checksum, no dict
  const std::uint64_t size = in.size();
  for (unsigned k = 0; k < 8; ++k) out.push_back(static_cast<std::uint8_t>(size >> (8U * k)));
  std::size_t pos = 0;
  do {
    const std::size_t len = std::min(kMaxBlock, in.size() - pos);
    const bool last = pos + len == in.size();
    const std::uint32_t header = (last ? 1U : 0U) | (static_cast<std::uint32_t>(len) << 3U);  // block type 0 = raw
    for (unsigned k = 0; k < 3; ++k) out.push_back(static_cast<std::uint8_t>(header >> (8U * k)));
    out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(pos), in.begin() + static_cast<std::ptrdiff_t>(pos + len));
    pos += len;
  } while (pos < in.size());
  return out;
}

}  // namespace tradelab::detail
