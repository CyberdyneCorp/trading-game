#pragma once

// Container and compression codecs for scenario packs (design D1): an uncompressed ustar archive
// of flat file names, and zstd frames for data files. Both readers treat input as hostile.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tradelab::detail {

// ---- zstd ----
enum class ZstdStatus : std::uint8_t { Ok, Malformed, TooLarge };

// Decompresses one or more zstd frames into exactly `declared_size` bytes. Output larger than the
// declaration (or than `hard_cap`) aborts with TooLarge before it is produced (decompression-bomb
// guard); a different exact size or a corrupt frame is Malformed.
ZstdStatus zstd_decompress(std::span<const std::uint8_t> in, std::size_t declared_size, std::size_t hard_cap,
                           std::vector<std::uint8_t>* out);

// A valid zstd frame holding `in` in raw (stored) blocks with its content size: what the engine
// writes for private imports, where compression is not worth an encoder in every client.
std::vector<std::uint8_t> zstd_store(std::span<const std::uint8_t> in);

// ---- tar (ustar) ----
struct TarEntry {
  std::string name;
  std::span<const std::uint8_t> data;  // view into the archive bytes
};

enum class TarStatus : std::uint8_t { Ok, Malformed, TooManyFiles, BadName, Duplicate };

// Regular files only, flat names ([a-z0-9._-], at most 64 chars), checksummed headers.
TarStatus tar_read(std::span<const std::uint8_t> archive, std::size_t max_files, std::vector<TarEntry>* out);

struct TarFile {
  std::string name;
  std::vector<std::uint8_t> data;
};

// Deterministic archive: fixed mode, zero uid/gid/mtime, files in the given order.
std::vector<std::uint8_t> tar_write(const std::vector<TarFile>& files);

}  // namespace tradelab::detail
