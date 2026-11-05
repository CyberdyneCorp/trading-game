#include <doctest/doctest.h>

#include <string>

#include "../src/pack_codec.hpp"
#include "pack_fixtures.hpp"

using namespace tradelab;
using namespace tradelab::detail;

namespace {

constexpr std::size_t kMiB = 1024 * 1024;

// zstd -19 of "tradelab pack data " x 64 (1216 bytes, content checksum on).
const char* const kHelloZst = "28b52ffd64c003dd00009874726164656c6162207061636b206461746120010055b17f86016da2f7d1";

// zstd -19 of 9 MiB of zeros (310 bytes, content size in the header): a decompression bomb.
const char* const kZerosZst =
    "28b52ffd8468000090004c000008000100fcff3910020200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "1000030010006e709734";

// The same bomb without the content size: only the output bound can stop it.
const char* const kZerosNoSizeZst =
    "28b52ffd04684c000008000100fcff391002020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200"
    "100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000200100002001000020010000300"
    "10006e709734";

std::string repeat(std::string_view s, int n) {
  std::string out;
  for (int i = 0; i < n; ++i) out += s;
  return out;
}

}  // namespace

TEST_CASE("zstd: decodes a reference frame made by the zstd CLI") {
  std::vector<std::uint8_t> out;
  const std::string expected = repeat("tradelab pack data ", 64);
  REQUIRE(zstd_decompress(test::hex_bytes(kHelloZst), expected.size(), kMiB, &out) == ZstdStatus::Ok);
  CHECK(std::string(out.begin(), out.end()) == expected);
}

TEST_CASE("zstd: stored frames round-trip, including empty and multi-block input") {
  for (const std::size_t n : {std::size_t{0}, std::size_t{5}, std::size_t{128 * 1024}, std::size_t{300'001}}) {
    std::vector<std::uint8_t> in(n);
    for (std::size_t i = 0; i < n; ++i) in[i] = static_cast<std::uint8_t>((i * 131) % 251);
    std::vector<std::uint8_t> out;
    REQUIRE(zstd_decompress(zstd_store(in), n, kMiB, &out) == ZstdStatus::Ok);
    CHECK(out == in);
  }
}

TEST_CASE("zstd: decompression guard stops bombs at the declared size") {
  std::vector<std::uint8_t> out;
  const auto bomb = test::hex_bytes(kZerosZst);
  CHECK(zstd_decompress(bomb, 1000, 8 * kMiB, &out) == ZstdStatus::TooLarge);       // header says 9 MiB
  CHECK(zstd_decompress(bomb, 9 * kMiB, 8 * kMiB, &out) == ZstdStatus::TooLarge);    // declared above the cap
  CHECK(zstd_decompress(test::hex_bytes(kZerosNoSizeZst), 4096, 8 * kMiB, &out) == ZstdStatus::TooLarge);  // no size header
  CHECK(out.empty());
  REQUIRE(zstd_decompress(bomb, 9 * kMiB, 16 * kMiB, &out) == ZstdStatus::Ok);  // honest declaration, generous cap
  CHECK(out.size() == 9 * kMiB);
}

TEST_CASE("zstd: corrupt or mis-declared frames are malformed") {
  std::vector<std::uint8_t> out;
  auto bad = test::hex_bytes(kHelloZst);
  bad[bad.size() - 2] ^= 0x40U;  // content checksum no longer matches
  CHECK(zstd_decompress(bad, 1216, kMiB, &out) == ZstdStatus::Malformed);
  CHECK(zstd_decompress(test::hex_bytes(kHelloZst), 2000, kMiB, &out) == ZstdStatus::Malformed);  // declared too big
  CHECK(zstd_decompress(std::vector<std::uint8_t>{1, 2, 3}, 10, kMiB, &out) == ZstdStatus::Malformed);
}

TEST_CASE("tar: deterministic round-trip and hostile headers") {
  const std::vector<TarFile> files = {{"manifest.json", {'{', '}'}}, {"bars.bin", std::vector<std::uint8_t>(700, 7)}};
  const auto archive = tar_write(files);
  CHECK(archive == tar_write(files));
  CHECK(archive.size() % 512 == 0);
  std::vector<TarEntry> entries;
  REQUIRE(tar_read(archive, 8, &entries) == TarStatus::Ok);
  REQUIRE(entries.size() == 2);
  CHECK(entries[1].name == "bars.bin");
  CHECK(entries[1].data.size() == 700);

  CHECK(tar_read(archive, 1, &entries) == TarStatus::TooManyFiles);
  auto corrupt = archive;
  corrupt[3] ^= 1U;  // name byte: checksum fails
  CHECK(tar_read(corrupt, 8, &entries) == TarStatus::Malformed);
  CHECK(tar_read(std::span(archive).first(600), 8, &entries) == TarStatus::Malformed);  // truncated
  CHECK(tar_read(tar_write({{"../evil", {1}}}), 8, &entries) == TarStatus::BadName);
  CHECK(tar_read(tar_write({{"a.bin", {1}}, {"a.bin", {2}}}), 8, &entries) == TarStatus::Duplicate);
}
