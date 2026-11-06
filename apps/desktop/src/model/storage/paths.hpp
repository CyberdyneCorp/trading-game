// Where the desktop client keeps its data, and the single-instance lock (design "Storage").
//   macOS  ~/Library/Application Support/TradeLab/
//   Linux  $XDG_DATA_HOME/tradelab/ (an absolute XDG_DATA_HOME only, per the XDG spec), else ~/.local/share/tradelab/
// TRADELAB_DATA_DIR overrides both (tests, portable installs).
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace tradelab::desktop::storage {

enum class Os : unsigned char { Linux, MacOS };

constexpr Os kHostOs =
#if defined(__APPLE__)
    Os::MacOS;
#else
    Os::Linux;
#endif

using GetEnv = std::function<std::optional<std::string>(const char*)>;

// The process environment (std::getenv); empty values read as unset.
std::optional<std::string> process_env(const char* name);

// nullopt when no home folder is known (no HOME and no override).
std::optional<std::filesystem::path> user_data_dir(Os os = kHostOs, const GetEnv& env = process_env);

// An exclusive advisory lock on <dir>/tradelab.lock (flock): a second instance on the same folder gets
// held() == false and must not open the database. Released by the destructor (and by the OS on a crash).
class InstanceLock {
 public:
  explicit InstanceLock(const std::filesystem::path& dir);
  ~InstanceLock();
  InstanceLock(const InstanceLock&) = delete;
  InstanceLock& operator=(const InstanceLock&) = delete;

  [[nodiscard]] bool held() const { return fd_ >= 0; }

 private:
  int fd_ = -1;
};

}  // namespace tradelab::desktop::storage
