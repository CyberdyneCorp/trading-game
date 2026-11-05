// Wires the layers together: EngineHost (model, worker thread) -> AppViewModel -> AppView (Raven).
// Snapshots reach the viewmodel on the UI thread through Rtask_post. Raven must be initialized first
// (init_vulkan or init_headless).
#pragma once

#include <memory>

#include "model/engine_host.hpp"
#include "model/i18n.hpp"
#include "view/app_view.hpp"
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

class App {
 public:
  App(Catalog catalog, AppViewModel::SeedSource seeds);
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  bool build(int width = 1280, int height = 800);
  // Runs Raven frames until the window closes; returns the exit code.
  int run();

  AppViewModel& vm() { return *vm_; }
  AppView& view() { return *view_; }
  EngineHost& engine() { return *engine_; }

 private:
  std::unique_ptr<EngineHost> engine_;
  std::unique_ptr<AppViewModel> vm_;
  std::unique_ptr<AppView> view_;
};

}  // namespace tradelab::desktop
