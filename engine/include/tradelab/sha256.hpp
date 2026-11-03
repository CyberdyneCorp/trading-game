#pragma once

// SHA-256 (FIPS 180-4) for state and event digests (design D7). Not constant-time; not for secrets.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace tradelab {

using Digest = std::array<std::uint8_t, 32>;

class Sha256 {
 public:
  Sha256();
  void update(std::span<const std::uint8_t> data);
  Digest finish();

 private:
  void compress(const std::uint8_t* block);
  std::array<std::uint32_t, 8> h_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_len_ = 0;
  std::uint64_t total_len_ = 0;
};

Digest sha256(std::span<const std::uint8_t> data);
std::string to_hex(const Digest& d);

}  // namespace tradelab
