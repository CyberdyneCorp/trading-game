#include "model/engine_host.hpp"

#include <utility>

#include "model/storage/run_recorder.hpp"

namespace tradelab::desktop {

EngineHost::EngineHost(Post post, Sink sink, std::unique_ptr<storage::Storage> storage)
    : missions_(engine_missions()),
      post_(std::move(post)),
      mailbox_(std::make_shared<Mailbox>()),
      storage_(std::move(storage)) {
  if (storage_) recorder_ = std::make_unique<storage::RunRecorder>(*storage_);
  mailbox_->sink = std::move(sink);
  worker_ = std::thread([this] { worker_loop(); });
}

EngineHost::~EngineHost() {
  {
    std::lock_guard lock(mu_);
    quit_ = true;
  }
  cv_.notify_all();
  worker_.join();
  std::lock_guard lock(mailbox_->mu);
  mailbox_->closed = true;
}

void EngineHost::start(const std::string& mission, std::uint32_t seed) {
  enqueue(Command{CommandKind::Start, mission, seed, 0, Side::Buy, {}, {}, {}});
}

void EngineHost::advance(int steps) { enqueue(Command{CommandKind::Advance, {}, 0, steps, Side::Buy, {}, {}, {}}); }

void EngineHost::market(Side side, const std::string& qty) {
  enqueue(Command{CommandKind::Market, {}, 0, 0, side, qty, {}, {}});
}

void EngineHost::resume() { enqueue(Command{CommandKind::Resume, {}, 0, 0, Side::Buy, {}, {}, {}}); }

void EngineHost::start_pack(const std::string& digest, std::uint32_t seed) {
  enqueue(Command{CommandKind::StartPack, {}, seed, 0, Side::Buy, {}, digest, {}});
}

void EngineHost::post_storage_job(StorageJob job) {
  // Not a run command: Start's kind with no mission marks it; execute() runs the job instead.
  enqueue(Command{CommandKind::Start, {}, 0, 0, Side::Buy, {}, {}, std::move(job)});
}

void EngineHost::wait_idle() {
  std::unique_lock lock(mu_);
  idle_cv_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

void EngineHost::enqueue(Command c) {
  {
    std::lock_guard lock(mu_);
    queue_.push_back(std::move(c));
  }
  cv_.notify_one();
}

void EngineHost::worker_loop() {
  while (true) {
    Command c;
    {
      std::unique_lock lock(mu_);
      cv_.wait(lock, [this] { return quit_ || !queue_.empty(); });
      if (quit_) return;
      c = std::move(queue_.front());
      queue_.pop_front();
      busy_ = true;
    }
    execute(c);
    {
      std::lock_guard lock(mu_);
      busy_ = false;
    }
    idle_cv_.notify_all();
  }
}

void EngineHost::execute(const Command& c) {
  if (c.job) {
    if (storage_) c.job(*storage_);
    return;
  }
  switch (c.kind) {
    case CommandKind::Start: run_start(c); break;
    case CommandKind::Advance: run_advance(c.steps); break;
    case CommandKind::Market: run_market(c); break;
    case CommandKind::Resume: run_resume(); break;
    case CommandKind::StartPack: run_start_pack(c); break;
  }
}

void EngineHost::publish_refusal(CommandKind kind, const std::string& reason) {
  Snapshot s;
  s.status = "invalid";
  s.results.push_back(CommandResult{kind, false, reason});
  publish(std::move(s));
}

// Header digest/status/equity (and a checkpoint every 20 actions) are written before the acknowledgement.
void EngineHost::settle(Snapshot& s) {
  if (!recorder_ || !run_) return;
  recorder_->settle(*run_, s);
  s.durable = recorder_->durable();
}

void EngineHost::run_start(const Command& c) {
  run_ = recorder_ ? recorder_->start(c.mission, c.seed) : EngineRun::create(c.mission, c.seed);
  if (!run_) {
    Snapshot s;
    s.mission = c.mission;
    s.status = "invalid";
    s.results.push_back(CommandResult{CommandKind::Start, false, "unknown_mission"});
    publish(std::move(s));
    return;
  }
  Snapshot s = run_->snapshot();
  settle(s);
  s.results.push_back(CommandResult{CommandKind::Start, true, "none"});
  publish(std::move(s));
}

// One bar at a time so the UI shows progress; intermediate snapshots are only built when no delivery
// is waiting on the UI thread (coalescing). The last one (batch done or run ended) is always built and
// carries the acknowledgement. The batch is one logged action, committed before the first bar.
void EngineHost::run_advance(int steps) {
  if (!run_) return;
  if (recorder_) recorder_->commit(storage::actions::advance(steps));
  for (int k = 1; k <= steps; ++k) {
    const bool active = run_->advance(1);
    const bool last = k == steps || !active;
    if (!last && delivery_pending()) continue;
    Snapshot s = run_->snapshot();
    s.progress_done = k;
    s.progress_total = steps;
    if (last) {
      settle(s);
      s.results.push_back(CommandResult{CommandKind::Advance, true, "none"});
    }
    publish(std::move(s));
    if (!active) return;
  }
}

void EngineHost::run_market(const Command& c) {
  if (!run_) return;
  const storage::Action a = storage::actions::market(c.side, c.qty);
  const storage::ApplyResult r = recorder_ ? recorder_->perform(*run_, a) : storage::apply_action(*run_, a);
  Snapshot s = run_->snapshot();
  settle(s);
  s.results.push_back(CommandResult{CommandKind::Market, r.accepted, r.reason});
  publish(std::move(s));
}

void EngineHost::run_resume() {
  if (!recorder_) return publish_refusal(CommandKind::Resume, "no_storage");
  std::optional<storage::Resumed> r = recorder_->resume();
  if (!r) return publish_refusal(CommandKind::Resume, "no_saved_run");
  if (!r->run) {
    Snapshot s;
    s.status = "invalid";
    s.resume_notice = storage::resume_notice_name(r->notice.kind);
    s.resume_detail = r->notice.detail;
    s.results.push_back(CommandResult{CommandKind::Resume, false, s.resume_notice});
    return publish(std::move(s));
  }
  run_ = std::move(r->run);
  Snapshot s = run_->snapshot();
  s.resume_notice = storage::resume_notice_name(r->notice.kind);
  s.resume_detail = r->notice.detail;
  s.durable = recorder_->durable();
  s.results.push_back(CommandResult{CommandKind::Resume, true, "none"});
  publish(std::move(s));
}

void EngineHost::run_start_pack(const Command& c) {
  if (!recorder_) return publish_refusal(CommandKind::StartPack, "no_storage");
  std::string reason;
  run_ = recorder_->start_pack(c.digest, c.seed, &reason);
  if (!run_) return publish_refusal(CommandKind::StartPack, reason);
  Snapshot s = run_->snapshot();
  settle(s);
  s.results.push_back(CommandResult{CommandKind::StartPack, true, "none"});
  publish(std::move(s));
}

bool EngineHost::delivery_pending() const {
  std::lock_guard lock(mailbox_->mu);
  return mailbox_->posted;
}

void EngineHost::publish(Snapshot s) {
  s.seq = ++seq_;
  bool need_post = false;
  {
    std::lock_guard lock(mailbox_->mu);
    if (mailbox_->pending) {  // not delivered yet: keep its acknowledgements
      auto merged = mailbox_->pending->results;
      merged.insert(merged.end(), s.results.begin(), s.results.end());
      s.results = std::move(merged);
    }
    mailbox_->pending = std::make_shared<const Snapshot>(std::move(s));
    need_post = !mailbox_->posted;
    mailbox_->posted = true;
  }
  if (need_post) post_([box = mailbox_] { deliver(box); });
}

void EngineHost::deliver(const std::shared_ptr<Mailbox>& box) {
  std::shared_ptr<const Snapshot> snap;
  Sink sink;
  {
    std::lock_guard lock(box->mu);
    if (box->closed) return;
    snap = std::move(box->pending);
    box->posted = false;
    sink = box->sink;
  }
  if (snap && sink) sink(snap);
}

}  // namespace tradelab::desktop
