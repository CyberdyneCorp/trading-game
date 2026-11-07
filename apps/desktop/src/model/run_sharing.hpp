// Run files on the desktop's storage (port of apps/web/src/lib/game/runSharing.svelte.ts;
// add-run-sharing-and-journal-stats D2, D5, D7): export a stored run (drafted from SQLite, sealed by
// the engine), import a file (verified by the engine, kept in the imports table), and recompute the
// summaries of runs that ended before schema v6. Every call runs on the simulation worker
// (EngineHost::post_storage_job) and reports back on the UI thread through RunFileService.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/run_files.hpp"
#include "model/storage/storage.hpp"

namespace tradelab::desktop {
class EngineHost;
}  // namespace tradelab::desktop

namespace tradelab::desktop::runfiles {

struct ImportOutcome {
  bool ok = false;
  std::string reason;  // the engine's refusal reason (or too_large, unreadable)
  std::string path;    // invalid_field: the first bad member
  std::string detail;  // policy_mismatch: the file's policy
  storage::ImportRecord record;
};

struct ExportOutcome {
  bool ok = false;
  std::string reason;  // private_pack | pack_missing | no_actions | unknown_run | an engine reason
  std::string detail;
  std::string name;    // tradelab-<mission>-<seed>.tlrun
  std::string text;    // the canonical file (with a final newline)
  bool mismatch = false;  // the sealed digest differs from the stored one (a damaged local log)
};

struct RecomputeOutcome {
  int done = 0;
  int skipped = 0;
  std::vector<std::pair<std::string, std::string>> summaries;  // (run id, summary JSON) written
};

ImportOutcome import_text(storage::Storage& s, const std::string& text);
ExportOutcome export_run(storage::Storage& s, const std::string& run_id, const EngineStamp& stamp, bool include_notes);
RecomputeOutcome recompute_summaries(storage::Storage& s, const EngineStamp& stamp);
// The canonical file of one of the player's runs, for the replay viewer (plan notes included: local).
ExportOutcome own_file(storage::Storage& s, const std::string& run_id, const EngineStamp& stamp);

// Reports to the viewmodel (on the UI thread).
struct RunFileEvent {
  enum class Kind : std::uint8_t { Imports, Imported, Exported, Watch, Recomputed };
  Kind kind = Kind::Imports;
  std::vector<storage::ImportRecord> imports;  // Imports (newest first; also sent after Imported)
  ImportOutcome imported;
  ExportOutcome exported;  // Exported (written to `written`) and Watch (the file to watch)
  std::string written;     // the path the export was written to ("" when writing failed)
  RecomputeOutcome recomputed;
};

// What viewmodels ask (RunFileService in the app, fakes in tests).
class RunFileCommands {
 public:
  virtual ~RunFileCommands() = default;
  virtual void list_imports() = 0;
  virtual void import_file(const std::string& path) = 0;
  // Writes the run's file into `dir` (created when missing).
  virtual void export_run(const std::string& run_id, bool include_notes, const std::string& dir) = 0;
  virtual void remove_import(const std::string& id) = 0;
  // The canonical file of an own run for the replay viewer (reported as a Watch event).
  virtual void own_file(const std::string& run_id) = 0;
  virtual void recompute() = 0;
};

class RunFileService final : public RunFileCommands {
 public:
  using Task = std::function<void()>;
  using Post = std::function<void(Task)>;
  using Sink = std::function<void(const RunFileEvent&)>;

  RunFileService(EngineHost& host, EngineStamp stamp, Post post, Sink sink);
  ~RunFileService() override;
  RunFileService(const RunFileService&) = delete;
  RunFileService& operator=(const RunFileService&) = delete;

  void list_imports() override;
  void import_file(const std::string& path) override;
  void export_run(const std::string& run_id, bool include_notes, const std::string& dir) override;
  void remove_import(const std::string& id) override;
  void own_file(const std::string& run_id) override;
  void recompute() override;

 private:
  struct Shared {
    std::mutex mu;
    bool closed = false;
    Sink sink;
  };
  void report(RunFileEvent e) const;

  EngineHost& host_;
  EngineStamp stamp_;
  Post post_;
  std::shared_ptr<Shared> shared_;
};

// Reads a run file from disk (at most kRunFileMaxBytes); nullopt when unreadable or too large (`why`).
std::optional<std::string> read_run_file(const std::string& path, std::string* why);

}  // namespace tradelab::desktop::runfiles
