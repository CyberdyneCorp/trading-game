// Database schema and its migrations (PRAGMA user_version). The versions follow the web's IndexedDB
// `tradelab` database so both clients keep the same model (apps/web/src/lib/game/storage.ts):
//   1  runs (header), actions (append-only log keyed by (run_id, seq)), meta (progress, settings, ...)
//   2  packs (installed packs by digest) and runs.pack_digest           (web DB v2)
//   3  checkpoints (latest per run, tagged with the engine that wrote it) (web DB v3)
//   4  downloads (resumable pack downloads: url, size, bytes committed)   (desktop only)
// A database newer than this build is refused (SchemaTooNew) rather than written to.
#pragma once

#include "model/storage/sqlite.hpp"

namespace tradelab::desktop::storage {

constexpr int kSchemaVersion = 4;

class SchemaTooNew : public Error {
 public:
  using Error::Error;
};

// Brings the database to kSchemaVersion, each step in its own transaction; returns the version found.
int migrate(Database& db);

// Applies the migrations up to `version` only (tests build older databases with it).
void migrate_to(Database& db, int version);

}  // namespace tradelab::desktop::storage
