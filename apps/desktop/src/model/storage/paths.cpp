#include "model/storage/paths.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdlib>
#include <system_error>

namespace tradelab::desktop::storage {

std::optional<std::string> process_env(const char* name) {
  const char* v = std::getenv(name);
  if (v == nullptr || *v == '\0') return std::nullopt;
  return std::string(v);
}

std::optional<std::filesystem::path> user_data_dir(Os os, const GetEnv& env) {
  if (auto dir = env("TRADELAB_DATA_DIR")) return std::filesystem::path(*dir);
  if (os == Os::Linux) {
    const auto xdg = env("XDG_DATA_HOME");
    if (xdg && std::filesystem::path(*xdg).is_absolute()) return std::filesystem::path(*xdg) / "tradelab";
  }
  const auto home = env("HOME");
  if (!home) return std::nullopt;
  if (os == Os::MacOS) return std::filesystem::path(*home) / "Library" / "Application Support" / "TradeLab";
  return std::filesystem::path(*home) / ".local" / "share" / "tradelab";
}

InstanceLock::InstanceLock(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path path = dir / "tradelab.lock";
  fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd_ < 0) return;
  if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

InstanceLock::~InstanceLock() {
  if (fd_ >= 0) ::close(fd_);  // closing releases the lock
}

}  // namespace tradelab::desktop::storage
