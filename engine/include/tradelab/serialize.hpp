#pragma once

// Canonical binary encoding (design D7): little-endian fixed-width integers,
// u32 length prefixes, no padding, no floating point. The byte stream is the
// input to state digests, so field order is part of the execution policy.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tradelab/fixed.hpp"
#include "tradelab/result.hpp"
#include "tradelab/sha256.hpp"

namespace tradelab {

class Writer {
 public:
  void u8(std::uint8_t v);
  void u16(std::uint16_t v);
  void u32(std::uint32_t v);
  void u64(std::uint64_t v);
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void boolean(bool v) { u8(v ? 1 : 0); }
  void bytes(std::span<const std::uint8_t> v);  // u32 length + bytes
  void str(std::string_view v);                 // u32 length + UTF-8 bytes

  template <typename Tag, int D>
  void fixed(Fixed<Tag, D> v) {
    i64(v.raw);
  }

  [[nodiscard]] const std::vector<std::uint8_t>& data() const { return buf_; }
  [[nodiscard]] Digest digest() const { return sha256(buf_); }

 private:
  void put_le(std::uint64_t v, int width);
  std::vector<std::uint8_t> buf_;
};

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

  Result<std::uint8_t> u8();
  Result<std::uint16_t> u16();
  Result<std::uint32_t> u32();
  Result<std::uint64_t> u64();
  Result<std::int64_t> i64();
  Result<bool> boolean();
  Result<std::vector<std::uint8_t>> bytes();
  Result<std::string> str();

  template <typename F>
  Result<F> fixed() {
    auto r = i64();
    if (!r.ok()) return r.error();
    return F{r.value()};
  }

  [[nodiscard]] bool at_end() const { return pos_ == data_.size(); }

 private:
  Result<std::uint64_t> get_le(int width);
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

}  // namespace tradelab
