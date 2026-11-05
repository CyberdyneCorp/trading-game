#include "model/update/update_service.hpp"

#include <utility>

namespace tradelab::desktop::update {

namespace {

UpdateState state_of(const CheckResult& r) {
  UpdateState s;
  switch (r.status) {
    case CheckStatus::UpToDate: s.status = UpdateState::Status::UpToDate; break;
    case CheckStatus::Available: s.status = UpdateState::Status::Available; break;
    case CheckStatus::Refused: s.status = UpdateState::Status::Failed; break;
    case CheckStatus::Unavailable: s.status = UpdateState::Status::Unavailable; break;
  }
  if (r.release) {
    s.version = r.release->version;
    s.notes_pt = r.release->notes_pt;
    s.notes_en = r.release->notes_en;
  }
  s.reason = r.reason;
  return s;
}

}  // namespace

UpdateService::UpdateService(std::unique_ptr<packs::HttpClient> http, Config config, std::filesystem::path staging,
                             Target target, Post post, Sink sink)
    : http_(std::move(http)),
      updater_(std::move(config), *http_, std::move(staging)),
      target_(std::move(target)),
      post_(std::move(post)),
      sink_(std::move(sink)) {}

UpdateService::~UpdateService() {
  *alive_ = false;  // UI thread: tasks posted from now on do nothing
  std::lock_guard lock(mu_);
  if (worker_.joinable()) worker_.join();
}

void UpdateService::run(std::function<void()> job) {
  std::lock_guard lock(mu_);
  if (worker_.joinable()) worker_.join();  // one job at a time (a check, then maybe one install)
  worker_ = std::thread(std::move(job));
}

void UpdateService::report(UpdateState s) {
  post_([alive = alive_, sink = sink_, s = std::move(s)] {
    if (*alive) sink(s);
  });
}

void UpdateService::check() {
  report(UpdateState{UpdateState::Status::Checking, {}, {}, {}, {}});
  run([this] {
    const CheckResult r = updater_.check();
    offered_ = r.release;
    report(state_of(r));
  });
}

void UpdateService::install() {
  run([this] {
    if (!offered_) return;
    const Release release = *offered_;
    report(UpdateState{UpdateState::Status::Downloading, release.version, release.notes_pt, release.notes_en, {}});
    const Staged staged = updater_.download(release);
    UpdateState done{UpdateState::Status::Scheduled, release.version, release.notes_pt, release.notes_en, {}};
    if (staged.error != DownloadError::None) {
      done.status = UpdateState::Status::Failed;
      done.reason = std::string(download_error_name(staged.error));
    } else if (!updater_.schedule(release, staged, target_)) {
      done.status = UpdateState::Status::Failed;
      done.reason = "storage";
    }
    report(std::move(done));
  });
}

}  // namespace tradelab::desktop::update
