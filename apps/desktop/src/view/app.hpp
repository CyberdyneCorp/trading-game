// Wires the layers together: EngineHost (model, worker thread) -> AppViewModel -> AppView (Raven).
// Snapshots reach the viewmodel on the UI thread through Rtask_post. Raven must be initialized first
// (init_vulkan or init_headless).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "model/engine_host.hpp"
#include "model/i18n.hpp"
#include "model/packs/pack_service.hpp"
#include "model/storage/profile_store.hpp"
#include "model/update/update_service.hpp"
#include "view/app_view.hpp"
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

class App {
 public:
  // `storage` (optional) persists runs on this device: model/storage, opened by main from the user-data dir.
  App(Catalog catalog, AppViewModel::SeedSource seeds, std::unique_ptr<storage::Storage> storage = nullptr);
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  bool build(int width = 1280, int height = 800);
  // Runs Raven frames until the window closes; returns the exit code.
  int run();
  // Packaged-build smoke test (--smoke-test): `frames` frames, then whether the menu shows with every
  // named widget and action in place.
  // Starts the update check of an installed release build (main decides when): the offer reaches the
  // menu; nothing is installed without the player's click.
  void enable_updates(const std::filesystem::path& staging, const update::Target& target);

  struct SmokeResult {
    bool menu = false;
    std::vector<std::string> missing;
  };
  SmokeResult smoke(int frames);

  AppViewModel& vm() { return *vm_; }
  AppView& view() { return *view_; }
  EngineHost& engine() { return *engine_; }

 private:
  void open_profile(const std::filesystem::path& dir);

  // Progress and settings in SQLite (UI-thread connection), with the repositories the viewmodels use.
  std::unique_ptr<storage::ProfileStore> profile_;
  std::unique_ptr<storage::ProgressStore> progress_store_;
  std::unique_ptr<storage::SettingsStore> settings_store_;
  std::unique_ptr<storage::GamePrefsStore> prefs_store_;
  std::unique_ptr<EngineHost> engine_;
  std::unique_ptr<packs::PackService> packs_;
  std::unique_ptr<AppViewModel> vm_;
  std::unique_ptr<AppView> view_;
  std::unique_ptr<update::UpdateService> updates_;
};

}  // namespace tradelab::desktop
