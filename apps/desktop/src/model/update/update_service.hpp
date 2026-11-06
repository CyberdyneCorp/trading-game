// Runs the Updater off the UI thread and reports its state there (like EngineHost: a Post function hands
// results to the UI thread, Rtask_post in the app). check() runs once at start; install() only on the
// player's click in the menu (AppViewModel::install_update), never by itself.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "model/packs/http.hpp"
#include "model/update/updater.hpp"

namespace tradelab::desktop::update {

struct UpdateState {
  enum class Status : std::uint8_t { Off, Checking, UpToDate, Available, Downloading, Scheduled, Failed, Unavailable };
  Status status = Status::Off;
  std::string version;
  std::string notes_pt;
  std::string notes_en;
  std::string reason;  // Failed / Unavailable

  bool operator==(const UpdateState&) const = default;
};

// What the viewmodel may ask (UpdateService in the app, fakes in tests).
class UpdateCommands {
 public:
  virtual ~UpdateCommands() = default;
  // Download, verify and schedule the offered release for the next start.
  virtual void install() = 0;
};

class UpdateService final : public UpdateCommands {
 public:
  using Task = std::function<void()>;
  using Post = std::function<void(Task)>;
  using Sink = std::function<void(const UpdateState&)>;

  UpdateService(std::unique_ptr<packs::HttpClient> http, Config config, std::filesystem::path staging, Target target,
                Post post, Sink sink);
  ~UpdateService() override;
  UpdateService(const UpdateService&) = delete;
  UpdateService& operator=(const UpdateService&) = delete;

  void check();
  void install() override;

 private:
  void run(std::function<void()> job);
  void report(UpdateState s);

  std::unique_ptr<packs::HttpClient> http_;
  Updater updater_;
  Target target_;
  Post post_;
  Sink sink_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // read and cleared on the UI thread
  std::mutex mu_;
  std::thread worker_;
  std::optional<Release> offered_;  // worker thread only (jobs never overlap)
};

}  // namespace tradelab::desktop::update
