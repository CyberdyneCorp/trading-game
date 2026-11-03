#include "tradelab/serialize.hpp"

namespace tradelab {

void Writer::put_le(std::uint64_t v, int width) {
  for (int i = 0; i < width; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8U * static_cast<unsigned>(i))));
}

void Writer::u8(std::uint8_t v) { buf_.push_back(v); }
void Writer::u16(std::uint16_t v) { put_le(v, 2); }
void Writer::u32(std::uint32_t v) { put_le(v, 4); }
void Writer::u64(std::uint64_t v) { put_le(v, 8); }

void Writer::bytes(std::span<const std::uint8_t> v) {
  u32(static_cast<std::uint32_t>(v.size()));
  buf_.insert(buf_.end(), v.begin(), v.end());
}

void Writer::str(std::string_view v) {
  u32(static_cast<std::uint32_t>(v.size()));
  for (const char c : v) buf_.push_back(static_cast<std::uint8_t>(c));
}

Result<std::uint64_t> Reader::get_le(int width) {
  const auto w = static_cast<std::size_t>(width);
  if (data_.size() - pos_ < w) return ErrorCode::TruncatedInput;
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < w; ++i) v |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8U * i);
  pos_ += w;
  return v;
}

Result<std::uint8_t> Reader::u8() {
  auto r = get_le(1);
  if (!r.ok()) return r.error();
  return static_cast<std::uint8_t>(r.value());
}

Result<std::uint16_t> Reader::u16() {
  auto r = get_le(2);
  if (!r.ok()) return r.error();
  return static_cast<std::uint16_t>(r.value());
}

Result<std::uint32_t> Reader::u32() {
  auto r = get_le(4);
  if (!r.ok()) return r.error();
  return static_cast<std::uint32_t>(r.value());
}

Result<std::uint64_t> Reader::u64() { return get_le(8); }

Result<std::int64_t> Reader::i64() {
  auto r = get_le(8);
  if (!r.ok()) return r.error();
  return static_cast<std::int64_t>(r.value());
}

Result<bool> Reader::boolean() {
  auto r = u8();
  if (!r.ok()) return r.error();
  if (r.value() > 1) return ErrorCode::NonCanonicalEncoding;
  return r.value() == 1;
}

Result<std::vector<std::uint8_t>> Reader::bytes() {
  auto len = u32();
  if (!len.ok()) return len.error();
  if (data_.size() - pos_ < len.value()) return ErrorCode::TruncatedInput;
  const auto first = data_.begin() + static_cast<std::ptrdiff_t>(pos_);
  std::vector<std::uint8_t> out(first, first + static_cast<std::ptrdiff_t>(len.value()));
  pos_ += len.value();
  return out;
}

Result<std::string> Reader::str() {
  auto b = bytes();
  if (!b.ok()) return b.error();
  return std::string(b.value().begin(), b.value().end());
}

}  // namespace tradelab
