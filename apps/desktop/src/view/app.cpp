#include "view/app.hpp"

#include <raven/raven_core.h>

#include "view/raven/raven_ui.hpp"

namespace tradelab::desktop {

namespace {

void on_frame(double /*delta*/, void* user_data) { static_cast<AppView*>(user_data)->refresh(); }

}  // namespace

App::App(Catalog catalog, AppViewModel::SeedSource seeds, std::unique_ptr<storage::Storage> storage) {
  engine_ = std::make_unique<EngineHost>([](EngineHost::Task task) { ui::post(std::move(task)); },
                                         [this](std::shared_ptr<const Snapshot> snap) {
                                           if (vm_) vm_->on_snapshot(std::move(snap));
                                         },
                                         std::move(storage));
  vm_ = std::make_unique<AppViewModel>(std::move(catalog), engine_->missions(), *engine_, std::move(seeds));
  vm_->game().attach_prefs(&prefs_);
  view_ = std::make_unique<AppView>(*vm_);
}

App::~App() {
  Rset_frame_callback(nullptr, nullptr);
  view_.reset();
  engine_.reset();  // joins the worker; posted deliveries become no-ops
  vm_.reset();
}

bool App::build(int width, int height) {
  if (!view_->build(width, height)) return false;
  Rset_frame_callback(on_frame, view_.get());
  return true;
}

int App::run() {
  while (Rwait_frame(-1.0)) {
  }
  return Rget_exit_code();
}

}  // namespace tradelab::desktop
