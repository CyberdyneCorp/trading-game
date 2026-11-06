#include "model/packs/files.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <tradelab/sha256.hpp>

namespace tradelab::desktop::packs {

namespace {

bool write_all(int fd, std::span<const std::uint8_t> bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
    if (n <= 0) return false;
    done += static_cast<std::size_t>(n);
  }
  return true;
}

bool write_and_sync(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, int flags) {
  const int fd = ::open(path.c_str(), flags, 0644);
  if (fd < 0) return false;
  const bool ok = write_all(fd, bytes) && ::fsync(fd) == 0;
  return ::close(fd) == 0 && ok;
}

void sync_dir(const std::filesystem::path& dir) {
  const int fd = ::open(dir.c_str(), O_RDONLY);
  if (fd < 0) return;
  ::fsync(fd);
  ::close(fd);
}

}  // namespace

bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  if (!write_and_sync(tmp, bytes, O_WRONLY | O_CREAT | O_TRUNC)) {
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return false;
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
  sync_dir(path.parent_path());
  return true;
}

bool append_file_durable(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  return write_and_sync(path, bytes, O_WRONLY | O_CREAT | O_APPEND);
}

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::vector<std::uint8_t> out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) return std::nullopt;
  return out;
}

std::string sha256_hex(std::span<const std::uint8_t> bytes) { return tradelab::to_hex(tradelab::sha256(bytes)); }

}  // namespace tradelab::desktop::packs
