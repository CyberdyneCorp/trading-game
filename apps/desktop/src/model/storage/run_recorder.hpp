// Persistence of the run the simulation worker plays (spec desktop-client "Local storage and offline",
// design "Storage" and "Crash recovery (OFF02)"). Used on the worker only, by EngineHost:
//
//   start / start_pack   new engine run + run header (engine version, execution policy); the previous
//                        unfinished run is left
//   commit(action)       appends the action in its own transaction BEFORE the engine sees it
//   (the host applies it: apply_action, or its own batch loop for "adv")
//   settle(snapshot)     header digest/status/equity, and a checkpoint every kCheckpointEvery actions,
//                        before the host acknowledges the command to the UI
//   resume()             the active run: latest checkpoint (same engine version, digest verified) + the
//                        tail of the log, else the whole log from the start
//
// Engine pinning (web engineCache.ts, design "Updater"): the desktop ships one engine. A run recorded by
// another engine version resumes on this one by replaying its whole log (its checkpoint is ignored) and
// says so (engine.pinMissing); a different execution policy or a replay digest that differs from the
// recorded one is reported like the web does. A storage failure never stops play: the run continues and
// durable() turns false (the web's `durable = false`).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "model/engine_run.hpp"
#include "model/snapshot.hpp"
#include "model/storage/actions.hpp"
#include "model/storage/storage.hpp"

namespace tradelab::desktop::storage {

// A checkpoint every this many logged actions (web CHECKPOINT_EVERY).
constexpr std::int64_t kCheckpointEvery = 20;

struct EngineVersion {
  std::string engine;
  int policy = 0;
  int abi = 0;  // tl_abi_version
};
// This build's engine (tl_version).
EngineVersion current_engine_version();

enum class ResumeNoticeKind : std::uint8_t {
  None,
  EngineChanged,   // recorded by another engine version: full replay on this one (detail: that version)
  PolicyChanged,   // recorded under another execution policy (detail: that policy number)
  DigestMismatch,  // the replayed state differs from the recorded digest (detail: its first 8 hex)
  PackMissing,     // the run's pack file is gone: nothing resumed
  AbiChanged,      // recorded under another engine C ABI: nothing resumed (detail: "<engine>/<abi>")
  Damaged,         // the run could not be rebuilt (unknown mission, refused pack): nothing resumed
};

// "" for None, else "engine_changed", "policy_changed", "digest_mismatch", "pack_missing", "abi_changed",
// "damaged".
std::string resume_notice_name(ResumeNoticeKind kind);

struct ResumeNotice {
  ResumeNoticeKind kind = ResumeNoticeKind::None;
  std::string detail;
  // The catalog key the UI shows ("engine.pinMissing" {build}, "pack.missing"), or "" when the web shows
  // a fixed English line (policy, digest) or nothing.
  [[nodiscard]] std::string i18n_key() const;
};

struct Resumed {
  std::unique_ptr<EngineRun> run;  // null when nothing could be resumed (see notice)
  ResumeNotice notice;
  std::string run_id;
  RunRecord record;                 // the resumed run's header
  std::vector<std::string> log;     // its committed actions
  std::int64_t actions = 0;        // logged actions replayed into the state
  std::int64_t from_checkpoint = 0;  // of which restored from the checkpoint (0 = full replay)
};

class RunRecorder {
 public:
  explicit RunRecorder(Storage& storage);

  // `links` are stored on the run's record (campaign mission, assisted attempt).
  std::unique_ptr<EngineRun> start(const std::string& mission, std::uint32_t seed, const RunOptions& options = {},
                                   const RunLinks& links = {});
  // A run on an installed pack; nullptr (with *reason) when it is missing or refused.
  std::unique_ptr<EngineRun> start_pack(const std::string& digest, std::uint32_t seed, std::string* reason = nullptr,
                                        const RunLinks& links = {});

  // Commit-before-engine. Returns false when the write failed (play goes on, durable() is false).
  bool commit(const Action& action);
  // commit + apply_action (every command except the host's batch advance).
  ApplyResult perform(EngineRun& run, const Action& action);
  // After the command's state is known, before it is acknowledged.
  void settle(EngineRun& run, const Snapshot& state);

  // The active run (also an ended one, which opens on its debrief); nullopt when there is none.
  std::optional<Resumed> resume();

  [[nodiscard]] bool durable() const { return durable_; }
  [[nodiscard]] bool recording() const { return !run_id_.empty(); }
  [[nodiscard]] const std::string& run_id() const { return run_id_; }
  [[nodiscard]] std::int64_t logged_actions() const { return next_seq_; }

 private:
  void begin(const RunRecord& record);
  std::unique_ptr<EngineRun> restore_checkpoint(const RunRecord& r, const std::vector<std::string>& log,
                                                const std::optional<std::vector<std::uint8_t>>& pack,
                                                std::int64_t* restored);
  std::unique_ptr<EngineRun> fresh(const RunRecord& r, const std::optional<std::vector<std::uint8_t>>& pack);
  ResumeNotice replay_notice(const RunRecord& r, std::int64_t replayed, const std::string& digest) const;
  template <class Fn>
  void guarded(Fn&& fn);

  Storage& storage_;
  EngineVersion engine_;
  std::string run_id_;
  std::int64_t next_seq_ = 0;
  bool durable_ = true;
};

}  // namespace tradelab::desktop::storage
