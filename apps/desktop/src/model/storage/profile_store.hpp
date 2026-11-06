// The player's progress and settings in SQLite (spec desktop-client "Local storage and offline"; the
// web keeps the same data in IndexedDB `meta.progress`, the runs store and localStorage):
//
//   meta "progress"            storage::Progress (web Progress v2): tutorials seen, advisor points and
//                              settled runs, perks introduced, Academy lessons, campaign + history, assessed
//   meta "setting:display"     language, CRT effects, colorblind palette, text scale
//   meta "setting:keymap"      the shortcut map (Settings and the terminal share it)
//   runs                       the shell's run list: journal and campaign link columns are written here,
//                              status/equity by the simulation worker; completed missions are the runs
//                              finished outside packs and assisted practice
//
// It opens its own connection on the UI thread (WAL lets it work beside the worker's). Every call
// catches storage errors: a failed read gives defaults, a failed write keeps the values for the session
// and durable() turns false.
//
// prefs.json (earlier builds) is imported once (import_prefs_file) and renamed to prefs.json.imported.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "model/game_prefs.hpp"
#include "model/progress.hpp"
#include "model/storage/run_store.hpp"

namespace tradelab::desktop::storage {

class ProfileStore {
 public:
  // Throws storage::Error when the database cannot be opened.
  explicit ProfileStore(const std::filesystem::path& db_file);

  // Merges an earlier build's prefs.json into the database and renames it; false when there is none.
  bool import_prefs_file(const std::filesystem::path& file);

  [[nodiscard]] desktop::Progress load_progress();
  void save_progress(const desktop::Progress& p);
  [[nodiscard]] std::optional<Settings> load_settings();
  void save_settings(const Settings& s);
  [[nodiscard]] GamePrefs load_prefs();
  bool save_prefs(const GamePrefs& p);

  [[nodiscard]] bool durable() const { return durable_; }
  [[nodiscard]] RunStore& store() { return store_; }

 private:
  struct StoredColumns {
    std::string journal;
    std::string campaign;
    std::string status;
  };
  template <class Fn>
  bool guarded(Fn&& fn);
  [[nodiscard]] std::vector<std::string> finished();
  void save_run(const desktop::RunRecord& r);
  static RunPatch shell_patch(const desktop::RunRecord& r, const StoredColumns& stored);

  RunStore store_;
  std::map<std::string, StoredColumns> stored_;  // what the runs table holds, per run id
  bool durable_ = true;
};

// The repositories the viewmodels use, over one ProfileStore.
class ProgressStore final : public ProgressRepository {
 public:
  explicit ProgressStore(ProfileStore& p) : p_(p) {}
  [[nodiscard]] desktop::Progress load() override { return p_.load_progress(); }
  void save(const desktop::Progress& progress) override { p_.save_progress(progress); }

 private:
  ProfileStore& p_;
};

class SettingsStore final : public SettingsRepository {
 public:
  explicit SettingsStore(ProfileStore& p) : p_(p) {}
  [[nodiscard]] std::optional<Settings> load() override { return p_.load_settings(); }
  void save(const Settings& settings) override { p_.save_settings(settings); }

 private:
  ProfileStore& p_;
};

class GamePrefsStore final : public PrefsStore {
 public:
  explicit GamePrefsStore(ProfileStore& p) : p_(p) {}
  [[nodiscard]] GamePrefs load() override { return p_.load_prefs(); }
  bool save(const GamePrefs& prefs) override { return p_.save_prefs(prefs); }

 private:
  ProfileStore& p_;
};

}  // namespace tradelab::desktop::storage
