// Everything persisted on this device, opened from one folder (paths.hpp: user_data_dir()):
//   <dir>/tradelab.db (+ -wal, -shm)   SQLite: runs, action log, checkpoints, progress, settings, packs index
//   <dir>/packs/<digest>.tlpack          verified pack files (<digest>.part while downloading)
// Owned by the simulation worker (EngineHost); the UI opens its own RunStore to read when it needs to.
#pragma once

#include <filesystem>
#include <memory>

#include "model/packs/pack_library.hpp"
#include "model/storage/run_store.hpp"

namespace tradelab::desktop::storage {

class Storage {
 public:
  // Throws storage::Error (or SchemaTooNew) when the database cannot be opened or migrated.
  explicit Storage(const std::filesystem::path& dir)
      : dir_(dir), store_(prepare(dir) / "tradelab.db"), packs_(store_, dir / "packs") {}

  [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }
  [[nodiscard]] RunStore& store() { return store_; }
  [[nodiscard]] packs::PackLibrary& packs() { return packs_; }

 private:
  static std::filesystem::path prepare(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    return dir;
  }
  std::filesystem::path dir_;
  RunStore store_;
  packs::PackLibrary packs_;
};

}  // namespace tradelab::desktop::storage
