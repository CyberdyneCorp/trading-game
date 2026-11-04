#include <algorithm>
#include <array>

#include "pack_codec.hpp"

namespace tradelab::detail {
namespace {

constexpr std::size_t kBlock = 512;
constexpr std::size_t kMaxName = 64;
using Header = std::span<const std::uint8_t, kBlock>;

bool name_char_ok(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; }

bool name_ok(const std::string& n) {
  if (n.empty() || n.size() > kMaxName || n == "." || n == "..") return false;
  return std::all_of(n.begin(), n.end(), name_char_ok);
}

// Octal field: digits, optionally followed by NUL/space padding. Returns false on anything else.
bool parse_octal(std::span<const std::uint8_t> field, std::uint64_t* out) {
  std::uint64_t v = 0;
  std::size_t i = 0;
  while (i < field.size() && field[i] == ' ') ++i;  // leading spaces are legal in old writers
  const std::size_t start = i;
  for (; i < field.size() && field[i] >= '0' && field[i] <= '7'; ++i) {
    if (v > (std::uint64_t{1} << 40U)) return false;
    v = v * 8 + (field[i] - '0');
  }
  if (i == start) return false;
  for (; i < field.size(); ++i) {
    if (field[i] != 0 && field[i] != ' ') return false;
  }
  *out = v;
  return true;
}

bool all_zero(std::span<const std::uint8_t> s) {
  return std::all_of(s.begin(), s.end(), [](std::uint8_t b) { return b == 0; });
}

bool checksum_ok(Header h) {
  std::uint64_t stored = 0;
  if (!parse_octal(h.subspan(148, 8), &stored)) return false;
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < kBlock; ++i) sum += (i >= 148 && i < 156) ? ' ' : h[i];
  return sum == stored;
}

std::string header_name(Header h) {
  const auto field = h.subspan(0, 100);
  const auto end = std::find(field.begin(), field.end(), std::uint8_t{0});
  return {field.begin(), end};
}

bool regular_file(Header h) {
  const bool type_ok = h[156] == '0' || h[156] == 0;
  return type_ok && all_zero(h.subspan(345, 155));  // no ustar prefix: names stay flat
}

void put_octal(std::array<std::uint8_t, kBlock>& h, std::size_t at, std::size_t width, std::uint64_t v) {
  for (std::size_t i = width - 1; i-- > 0;) {  // width - 1 digits, then NUL
    h[at + i] = static_cast<std::uint8_t>('0' + (v & 7U));
    v >>= 3U;
  }
  h[at + width - 1] = 0;
}

void put_text(std::array<std::uint8_t, kBlock>& h, std::size_t at, std::string_view s) {
  std::copy(s.begin(), s.end(), h.begin() + static_cast<std::ptrdiff_t>(at));
}

}  // namespace

TarStatus tar_read(std::span<const std::uint8_t> archive, std::size_t max_files, std::vector<TarEntry>* out) {
  out->clear();
  std::size_t pos = 0;
  while (pos + kBlock <= archive.size()) {
    const Header h = archive.subspan(pos).first<kBlock>();
    if (all_zero(h)) return TarStatus::Ok;  // end-of-archive marker
    std::uint64_t size = 0;
    if (!checksum_ok(h) || !regular_file(h) || !parse_octal(h.subspan(124, 12), &size)) return TarStatus::Malformed;
    TarEntry e{header_name(h), {}};
    if (!name_ok(e.name)) return TarStatus::BadName;
    pos += kBlock;
    if (size > archive.size() - pos) return TarStatus::Malformed;
    e.data = archive.subspan(pos, static_cast<std::size_t>(size));
    pos += static_cast<std::size_t>((size + kBlock - 1) / kBlock * kBlock);
    const bool dup = std::any_of(out->begin(), out->end(), [&e](const TarEntry& x) { return x.name == e.name; });
    if (dup) return TarStatus::Duplicate;
    if (out->size() >= max_files) return TarStatus::TooManyFiles;
    out->push_back(e);
  }
  return TarStatus::Malformed;  // no end-of-archive marker
}

std::vector<std::uint8_t> tar_write(const std::vector<TarFile>& files) {
  std::vector<std::uint8_t> out;
  for (const TarFile& f : files) {
    std::array<std::uint8_t, kBlock> h{};
    put_text(h, 0, f.name);
    put_text(h, 100, "0000644");
    put_octal(h, 108, 8, 0);
    put_octal(h, 116, 8, 0);
    put_octal(h, 124, 12, f.data.size());
    put_octal(h, 136, 12, 0);
    h[156] = '0';
    put_text(h, 257, "ustar");
    put_text(h, 263, "00");
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i) sum += (i >= 148 && i < 156) ? ' ' : h[i];
    put_octal(h, 148, 7, sum);
    h[155] = ' ';
    out.insert(out.end(), h.begin(), h.end());
    out.insert(out.end(), f.data.begin(), f.data.end());
    out.resize((out.size() + kBlock - 1) / kBlock * kBlock, 0);
  }
  out.resize(out.size() + 2 * kBlock, 0);
  return out;
}

}  // namespace tradelab::detail
