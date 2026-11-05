// Repository over the SQLite database (port of RunStore in apps/web/src/lib/game/storage.ts).
//
// A run is a header (mission, seed, engine, policy, journal, ...) plus an append-only action log keyed
// by (run_id, seq). Each action is committed in its own transaction BEFORE the engine sees it, so
// anything the UI showed as accepted survives a crash. Resume restores the latest checkpoint and replays
// the tail of the log through the deterministic engine; the stored digest only verifies the replay.
//
// One RunStore per thread (one SQLite connection each): the simulation worker writes, the UI may open
// its own store on the same file to read (WAL lets readers run beside the writer).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/snapshot.hpp"
#include "model/storage/progress.hpp"
#include "model/storage/sqlite.hpp"

namespace tradelab::desktop::storage {

// Start options of a run: desktop::RunOptions (snapshot.hpp), stored as the web's RunRecord.options.
using RunOptions = desktop::RunOptions;

struct RunRecord {
  std::string run_id;
  std::string mission_id;  // a built-in mission id, or the pack's scenario id
  std::uint32_t seed = 0;
  std::int64_t created_at = 0;  // ms since the epoch
  int execution_policy = 0;
  std::string engine;           // engine version that started the run (tl_version "engine")
  std::string status = "active";  // active | finished | bankrupt | left
  std::int64_t actions = 0;       // committed actions
  std::string digest;
  RunOptions options;
  std::string journal = "[]";    // JSON array (web PlanEntry[])
  std::string questions = "[]";  // JSON array
  int stop_warnings = 0;
  std::optional<std::string> campaign;  // JSON (web CampaignLink)
  std::optional<std::string> assisted;  // JSON (web AssistedLink)
  std::optional<std::string> equity;
  std::optional<std::string> pack_digest;
};

// Header fields a command may change; the action count is only ever moved by append_action.
struct RunPatch {
  std::optional<std::string> digest;
  std::optional<std::string> status;
  std::optional<std::string> equity;
  std::optional<std::string> journal;
  std::optional<std::string> questions;
  std::optional<int> stop_warnings;
};

struct StoredRun {
  RunRecord record;
  std::vector<std::string> actions;  // JSON text of each action, seq 0.. (gap-free prefix only)
};

struct Checkpoint {
  std::string run_id;
  std::int64_t actions = 0;  // the state after this many logged actions
  std::string hex;           // tl_checkpoint
  std::string digest;        // state digest at that point (checked after restore)
  std::string engine;        // engine version that wrote it: only that engine restores it
  std::optional<std::string> series_names;
};

struct StoredPack {
  std::string digest;
  std::string scenario_id;
  std::string label;
  std::int64_t size = 0;
  bool is_private = false;
  int total_bars = 0;
  int warmup_bars = 0;
  int leverage_cap = 0;
  int gaps = 0;
  std::int64_t added_at = 0;
  std::string origin = "import";  // import | catalog
  std::optional<std::string> mode;
};

struct Download {
  std::string digest;
  std::string url;
  std::int64_t size = 0;
  std::int64_t received = 0;  // bytes committed to the .part file
};

class RunStore {
 public:
  // Opens (or creates) the database and migrates it. Throws storage::Error, SchemaTooNew.
  explicit RunStore(const std::string& path);

  // ---- runs ----
  // Stores a new run and makes it the active one; the run it replaces, if unfinished, is left.
  void create_run(const RunRecord& r);
  void leave_active_run();
  // Durably appends one action (returns after COMMIT); bumps the run's action count.
  void append_action(const std::string& run_id, std::int64_t seq, const std::string& action_json);
  void update_run(const std::string& run_id, const RunPatch& patch);
  [[nodiscard]] std::optional<RunRecord> run(const std::string& run_id);
  [[nodiscard]] std::optional<StoredRun> active_run();
  // Every run header, newest first; an 'active' run that is not the resumable one reads as left.
  [[nodiscard]] std::vector<RunRecord> runs();
  // Missions with a run played to the end (no pack practice, no assisted attempt).
  [[nodiscard]] std::vector<std::string> finished_missions();

  // ---- checkpoints ----
  // Keeps only the latest checkpoint of a run (an older one is never needed to resume).
  void save_checkpoint(const Checkpoint& cp);
  [[nodiscard]] std::optional<Checkpoint> checkpoint(const std::string& run_id);
  void prune_checkpoints(const std::optional<std::string>& keep_run_id);

  // ---- meta: progress, settings, device seed, catalog cache ----
  [[nodiscard]] Progress progress();
  void save_progress(const Progress& p);
  [[nodiscard]] std::optional<std::string> setting(const std::string& key);
  void set_setting(const std::string& key, const std::string& value);
  // This device's signing seed for private imports (64 hex), created on first use from `random`.
  [[nodiscard]] std::string device_seed(const std::function<std::string()>& random_hex);
  [[nodiscard]] std::optional<std::string> meta(const std::string& key);
  void set_meta(const std::string& key, const std::string& value);

  // ---- packs ----
  void save_pack(const StoredPack& p);
  [[nodiscard]] std::optional<StoredPack> pack(const std::string& digest);
  [[nodiscard]] std::vector<StoredPack> packs();  // oldest first
  void delete_pack(const std::string& digest);

  // ---- downloads ----
  void save_download(const Download& d);
  [[nodiscard]] std::optional<Download> download(const std::string& digest);
  [[nodiscard]] std::vector<Download> downloads();
  void clear_download(const std::string& digest);

  [[nodiscard]] Database& db() { return db_; }
  // Schema version found when the file was opened (0 = new database).
  [[nodiscard]] int opened_version() const { return opened_version_; }

 private:
  void leave_active_locked();
  Database db_;
  int opened_version_ = 0;
};

std::string new_run_id();
std::int64_t now_ms();

std::string options_json(const RunOptions& o);
RunOptions options_from_json(const std::string& text);

}  // namespace tradelab::desktop::storage
