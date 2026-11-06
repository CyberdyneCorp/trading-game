#include "view/app.hpp"

#include <raven/raven_core.h>
#include <raven/raven_window.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <optional>
#include <utility>

#include "view/raven/raven_ui.hpp"

namespace tradelab::desktop {

namespace {

void on_frame(double /*delta*/, void* user_data) { static_cast<AppView*>(user_data)->refresh(); }

// The run left unfinished by an earlier launch, read before the engine worker owns the storage. Only a
// free-play run of a built-in mission is offered: campaign progress and pack texts do not outlive the
// session yet (tasks 1.14).
std::optional<SavedRun> saved_run(storage::Storage* storage) {
  if (storage == nullptr) return std::nullopt;
  try {
    const auto active = storage->store().active_run();
    if (!active || active->record.status != "active" || active->actions.empty()) return std::nullopt;
    const storage::RunRecord& r = active->record;
    if (r.campaign || r.pack_digest || r.assisted) return std::nullopt;
    return SavedRun{r.mission_id, r.seed};
  } catch (const std::exception&) {
    return std::nullopt;  // an unreadable store offers nothing; the engine reports it on use
  }
}

}  // namespace

App::App(Catalog catalog, AppViewModel::SeedSource seeds, std::unique_ptr<storage::Storage> storage) {
  std::optional<SavedRun> saved = saved_run(storage.get());  // before the engine worker owns the storage
  engine_ = std::make_unique<EngineHost>([](EngineHost::Task task) { ui::post(std::move(task)); },
                                         [this](std::shared_ptr<const Snapshot> snap) {
                                           if (vm_) vm_->on_snapshot(std::move(snap));
                                         },
                                         std::move(storage));
  const auto& missions = engine_->missions();
  if (saved && std::none_of(missions.begin(), missions.end(), [&](const MissionInfo& m) { return m.id == saved->mission; })) {
    saved.reset();
  }
  vm_ = std::make_unique<AppViewModel>(std::move(catalog), engine_->missions(), *engine_, std::move(seeds));
  vm_->offer_saved_run(saved);
  vm_->game().attach_prefs(&prefs_);
  view_ = std::make_unique<AppView>(*vm_);
}

App::~App() {
  Rset_frame_callback(nullptr, nullptr);
  updates_.reset();  // joins a running check or download; its late reports do nothing
  view_.reset();
  engine_.reset();  // joins the worker; posted deliveries become no-ops
  vm_.reset();
}

bool App::build(int width, int height) {
  if (!view_->build(width, height)) return false;
  Rset_frame_callback(on_frame, view_.get());
  return true;
}

void App::enable_updates(const std::filesystem::path& staging, const update::Target& target) {
  const auto key = update::public_key_from_hex(TRADELAB_UPDATE_PUBLIC_KEY);
  if (!key) return;  // a build without the release key never checks
  auto http = packs::CurlHttpClient::load();
  if (!http) return;  // no libcurl: offline, like the pack catalog
  update::Config config;
  const char* url = std::getenv("TRADELAB_UPDATE_MANIFEST_URL");
  config.manifest_url = url != nullptr && *url != '\0' ? url : TRADELAB_UPDATE_MANIFEST_URL;
  config.public_key = *key;
  config.current_version = TRADELAB_DESKTOP_VERSION;
  updates_ = std::make_unique<update::UpdateService>(
      std::move(http), std::move(config), staging, target, [](update::UpdateService::Task t) { ui::post(std::move(t)); },
      [this](const update::UpdateState& s) { vm_->on_update(s); });
  vm_->attach_updates(updates_.get());
  updates_->check();
}

App::SmokeResult App::smoke(int frames) {
  for (int i = 0; i < frames; ++i) {
    Rwindow_request_redraw(view_->window());
    Rwait_frame(0.01);
  }
  return SmokeResult{vm_->screen() == Screen::Menu, view_->missing_names()};
}

int App::run() {
  while (Rwait_frame(-1.0)) {
  }
  return Rget_exit_code();
}

}  // namespace tradelab::desktop
