#include "view/app.hpp"

#include <raven/raven_core.h>
#include <raven/raven_window.h>
#include <tradelab/c_api.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <optional>
#include <utility>

#include "model/json.hpp"
#include "model/progress_json.hpp"
#include "view/raven/raven_ui.hpp"

namespace tradelab::desktop {

namespace {

// This engine's versions, stamped on exported run files.
EngineStamp engine_stamp() {
  const json::Value v = json::parse(tl_version()).value_or(json::Value{});
  return EngineStamp{static_cast<int>(v["executionPolicy"].as_int()), static_cast<int>(tl_abi_version()), v["engine"].as_string()};
}

json::Value parse_or_null(const std::optional<std::string>& text) {
  if (!text) return {};
  auto v = json::parse(*text);
  return v ? *v : json::Value{};
}

// Where the pack catalog manifest is read: $TRADELAB_CATALOG_URL (tests), else the build's
// TRADELAB_CATALOG_URL (the web's static catalog in this repository by default).
std::string catalog_url() {
  const char* url = std::getenv("TRADELAB_CATALOG_URL");
  return url != nullptr && *url != '\0' ? url : TRADELAB_CATALOG_URL;
}

void on_frame(double /*delta*/, void* user_data) { static_cast<AppView*>(user_data)->refresh(); }

// The run left unfinished by an earlier launch, read before the engine worker owns the storage: free
// play, a campaign mission (offered while its campaign still waits on it: AppViewModel), a pack run or an
// assisted attempt.
std::optional<SavedRun> saved_run(storage::Storage* storage) {
  if (storage == nullptr) return std::nullopt;
  try {
    const auto active = storage->store().active_run();
    if (!active || active->record.status != "active" || active->actions.empty()) return std::nullopt;
    const storage::RunRecord& r = active->record;
    SavedRun out{r.mission_id, r.seed, r.run_id, std::nullopt, r.pack_digest.value_or(""), std::nullopt,
                 r.equity.value_or(r.options.starting_balance)};
    if (r.campaign) out.campaign = progress_json::link_from(parse_or_null(r.campaign));
    if (r.assisted) out.assisted = progress_json::assisted_from(parse_or_null(r.assisted));
    return out;
  } catch (const std::exception&) {
    return std::nullopt;  // an unreadable store offers nothing; the engine reports it on use
  }
}

}  // namespace

// Progress and settings live in the same database (its own connection on this thread); an earlier
// build's prefs.json is imported once. A database that cannot be opened leaves them for the session.
void App::open_profile(const std::filesystem::path& dir) {
  try {
    profile_ = std::make_unique<storage::ProfileStore>(dir / "tradelab.db");
  } catch (const std::exception&) {
    return;
  }
  profile_->import_prefs_file(dir / "prefs.json");
  progress_store_ = std::make_unique<storage::ProgressStore>(*profile_);
  settings_store_ = std::make_unique<storage::SettingsStore>(*profile_);
  prefs_store_ = std::make_unique<storage::GamePrefsStore>(*profile_);
}

App::App(Catalog catalog, AppViewModel::SeedSource seeds, std::unique_ptr<storage::Storage> storage) {
  std::optional<SavedRun> saved = saved_run(storage.get());  // before the engine worker owns the storage
  const bool stored = storage != nullptr;
  if (storage) open_profile(storage->dir());
  engine_ = std::make_unique<EngineHost>([](EngineHost::Task task) { ui::post(std::move(task)); },
                                         [this](std::shared_ptr<const Snapshot> snap) {
                                           if (vm_) vm_->on_snapshot(std::move(snap));
                                         },
                                         std::move(storage));
  const auto& missions = engine_->missions();
  const auto built_in = [&](const std::string& id) {
    return std::any_of(missions.begin(), missions.end(), [&](const MissionInfo& m) { return m.id == id; });
  };
  if (saved && saved->pack_digest.empty() && !built_in(saved->mission)) saved.reset();
  vm_ = std::make_unique<AppViewModel>(std::move(catalog), engine_->missions(), *engine_, std::move(seeds));
  if (profile_) {
    vm_->attach_repositories(progress_store_.get(), settings_store_.get());
    vm_->game().attach_prefs(prefs_store_.get());
  }
  vm_->offer_saved_run(saved);  // after the progress: a campaign mission is checked against it
  if (stored) {
    std::unique_ptr<packs::HttpClient> http = packs::CurlHttpClient::load();
    packs_ = std::make_unique<packs::PackService>(
        *engine_, std::move(http), catalog_url(), [](packs::PackService::Task t) { ui::post(std::move(t)); },
        [this](const packs::PackEvent& e) {
          if (vm_) vm_->on_pack_event(e);
        });
    vm_->attach_packs(packs_.get());
    // Run files (add-run-sharing-and-journal-stats): stamped with this engine's versions.
    run_files_ = std::make_unique<runfiles::RunFileService>(
        *engine_, engine_stamp(),[](runfiles::RunFileService::Task t) { ui::post(std::move(t)); },
        [this](const runfiles::RunFileEvent& e) {
          if (vm_) vm_->on_run_file_event(e);
        });
    vm_->attach_run_files(run_files_.get());
  }
  audio_ = platform::MiniaudioOut::from_environment(TRADELAB_AUDIO_SOURCE_DIR);
  vm_->attach_audio(audio_.get());
  view_ = std::make_unique<AppView>(*vm_);
}

App::~App() {
  Rset_frame_callback(nullptr, nullptr);
  updates_.reset();  // joins a running check or download; its late reports do nothing
  view_.reset();
  if (packs_) packs_->stop_downloads();  // a download in progress stops at its next chunk
  engine_.reset();  // joins the worker; posted deliveries become no-ops
  packs_.reset();
  run_files_.reset();
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
