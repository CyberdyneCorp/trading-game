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
  Command c;
  c.mission = mission;
  c.seed = seed;
  enqueue(std::move(c));
}

void EngineHost::advance(int steps) {
  Command c;
  c.kind = CommandKind::Advance;
  c.steps = steps;
  enqueue(std::move(c));
}

void EngineHost::market(Side side, const std::string& qty) {
  Command c;
  c.kind = CommandKind::Market;
  c.side = side;
  c.qty = qty;
  enqueue(std::move(c));
}

void EngineHost::game(const GameCommand& command) {
  Command c;
  c.kind = CommandKind::Game;
  c.game = command;
  enqueue(std::move(c));
}

void EngineHost::start_with(const std::string& mission, std::uint32_t seed, const RunOptions& options) {
  Command c;
  c.kind = CommandKind::Start;
  c.mission = mission;
  c.seed = seed;
  c.options = options;
  enqueue(std::move(c));
}

void EngineHost::resume() {
  Command c;
  c.kind = CommandKind::Resume;
  enqueue(std::move(c));
}

void EngineHost::start_pack(const std::string& digest, std::uint32_t seed) {
  Command c;
  c.kind = CommandKind::StartPack;
  c.seed = seed;
  c.digest = digest;
  enqueue(std::move(c));
}

void EngineHost::start_linked(const std::string& mission, std::uint32_t seed, const RunOptions& options,
                              const RunLinks& links) {
  Command c;
  c.kind = CommandKind::Start;
  c.mission = mission;
  c.seed = seed;
  c.options = options;
  c.links = links;
  enqueue(std::move(c));
}

void EngineHost::rewind(int to_step, const RunLinks& links) {
  Command c;
  c.kind = CommandKind::Rewind;
  c.steps = to_step;
  c.links = links;
  enqueue(std::move(c));
}

void EngineHost::analyze(AnalysisKind kind, const std::string& request, AnalysisReply reply) {
  Command c;
  c.analysis = kind;
  c.qty = request;
  c.reply = std::move(reply);
  enqueue(std::move(c));
}

void EngineHost::post_storage_job(StorageJob job) {
  // Not a run command: execute() runs the job instead.
  Command c;
  c.job = std::move(job);
  enqueue(std::move(c));
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
  if (c.reply) return run_analysis(c);
  switch (c.kind) {
    case CommandKind::Start: run_start(c); break;
    case CommandKind::Advance: run_advance(c.steps); break;
    case CommandKind::Market: run_market(c); break;
    case CommandKind::Game: run_game(c); break;
    case CommandKind::Resume: run_resume(); break;
    case CommandKind::StartPack: run_start_pack(c); break;
    case CommandKind::Rewind: run_rewind(c); break;
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

// A new engine run (and its stored record): a built-in mission, or an installed pack.
std::unique_ptr<EngineRun> EngineHost::open_run(const Current& next, const RunLinks& links, std::string* reason) {
  if (!next.pack_digest.empty()) {
    if (!recorder_) {
      *reason = "no_storage";
      return nullptr;
    }
    return recorder_->start_pack(next.pack_digest, next.seed, reason, links);
  }
  auto run = recorder_ ? recorder_->start(next.mission, next.seed, next.options, links)
                       : EngineRun::create(next.mission, next.seed, next.options);
  if (!run) *reason = "unknown_mission";
  return run;
}

// Every action applied to the run in play goes through here: logged (and committed first) and kept in
// memory for an assisted rewind.
storage::ApplyResult EngineHost::perform(const storage::Action& a) {
  current_.log.push_back(a.json);
  return recorder_ ? recorder_->perform(*run_, a) : storage::apply_action(*run_, a);
}

void EngineHost::stamp(Snapshot& s) {
  if (recorder_) s.run_id = recorder_->run_id();
  s.assisted = current_.assisted;
  // One copy per change of the log (a command, an advance batch), shared by the snapshots after it.
  if (!shared_log_ || *shared_log_ != current_.log) shared_log_ = std::make_shared<const std::vector<std::string>>(current_.log);
  s.log = shared_log_;
}

void EngineHost::run_start(const Command& c) {
  Current next{c.mission, c.seed, c.options, {}, !c.links.assisted.empty(), {}};
  std::string reason;
  auto run = open_run(next, c.links, &reason);
  if (!run) {
    Snapshot s;
    s.mission = c.mission;
    s.status = "invalid";
    s.results.push_back(CommandResult{CommandKind::Start, false, reason});
    publish(std::move(s));
    return;
  }
  run_ = std::move(run);
  current_ = std::move(next);
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
  const storage::Action logged = storage::actions::advance(steps);
  current_.log.push_back(logged.json);
  if (recorder_) recorder_->commit(logged);
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
  const storage::ApplyResult r = perform(storage::actions::market(c.side, c.qty));
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
  const storage::RunRecord& rec = r->record;
  current_ = Current{rec.mission_id, rec.seed, rec.options, rec.pack_digest.value_or(""), rec.assisted.has_value(),
                     std::move(r->log)};
  Snapshot s = run_->snapshot();
  s.resume_notice = storage::resume_notice_name(r->notice.kind);
  s.resume_detail = r->notice.detail;
  s.durable = recorder_->durable();
  s.results.push_back(CommandResult{CommandKind::Resume, true, "none"});
  publish(std::move(s));
}

void EngineHost::run_start_pack(const Command& c) {
  if (!recorder_) return publish_refusal(CommandKind::StartPack, "no_storage");
  Current next{{}, c.seed, {}, c.digest, false, {}};
  std::string reason;
  auto run = open_run(next, c.links, &reason);
  if (!run) return publish_refusal(CommandKind::StartPack, reason);
  run_ = std::move(run);
  current_ = std::move(next);
  Snapshot s = run_->snapshot();
  settle(s);
  s.results.push_back(CommandResult{CommandKind::StartPack, true, "none"});
  publish(std::move(s));
}

// A command without a run (or with no kind) is still acknowledged, so the viewmodel never waits for it
// forever; it is not logged. Every other terminal command is a logged action, like the market order.
void EngineHost::run_game(const Command& c) {
  if (!run_ || c.game.kind == GameCommandKind::None) {
    Snapshot s;
    s.status = "invalid";
    CommandResult r{CommandKind::Game, false, "invalid_sequence"};
    r.game = c.game.kind;
    s.results.push_back(std::move(r));
    publish(std::move(s));
    return;
  }
  const storage::ApplyResult applied = perform(storage::actions::from_game(c.game));
  Snapshot s = run_->snapshot();
  settle(s);
  s.results.push_back(game_result(c.game.kind, applied.result));
  publish(std::move(s));
}

// The same market and start options (without the advisor) and the actions logged before `to_step`, as a
// new stored run flagged assisted; the attempt rewound keeps its record and log.
void EngineHost::run_rewind(const Command& c) {
  if (!run_) return publish_refusal(CommandKind::Rewind, "invalid_sequence");
  const Snapshot now = run_->snapshot();
  if (c.steps < now.warmup_bars || c.steps >= now.step) return publish_refusal(CommandKind::Rewind, "invalid_step");
  const std::vector<std::string> prefix = storage::rewind_prefix(current_.log, now.warmup_bars, c.steps);
  Current next{current_.mission, current_.seed, current_.options, current_.pack_digest, true, {}};
  next.options.advice_allowance = 0;
  std::string reason;
  auto run = open_run(next, c.links, &reason);
  if (!run) return publish_refusal(CommandKind::Rewind, reason);
  run_ = std::move(run);
  current_ = std::move(next);
  for (const std::string& text : prefix) perform(storage::Action{text});
  Snapshot s = run_->snapshot();
  settle(s);
  s.results.push_back(CommandResult{CommandKind::Rewind, true, "none"});
  publish(std::move(s));
}

// Report-only: nothing is logged or published. The answer goes to the UI thread like a snapshot, and is
// dropped when the host is gone by then.
void EngineHost::run_analysis(const Command& c) {
  std::string out = R"({"error":"no_run"})";
  if (c.analysis == AnalysisKind::WhatIf) out = EngineRun::option_whatif(c.qty);
  else if (run_ && c.analysis == AnalysisKind::RunWhatIf) out = run_->run_whatif(c.qty);
  else if (run_) out = run_->combo_preview(c.qty);
  post_([box = mailbox_, reply = c.reply, text = std::move(out)] {
    {
      std::lock_guard lock(box->mu);
      if (box->closed) return;
    }
    reply(text);
  });
}

bool EngineHost::delivery_pending() const {
  std::lock_guard lock(mailbox_->mu);
  return mailbox_->posted;
}

void EngineHost::publish(Snapshot s) {
  s.seq = ++seq_;
  if (run_) stamp(s);
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
