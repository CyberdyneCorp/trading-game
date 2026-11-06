#include "model/storage/schema.hpp"

#include <array>
#include <string_view>

namespace tradelab::desktop::storage {

namespace {

// Index i holds the step from version i to i + 1. Never edit a released step: add a new one.
constexpr std::array<std::string_view, kSchemaVersion> kSteps = {
    // 1: runs, the action log and key/value meta.
    R"sql(
CREATE TABLE runs (
  run_id TEXT PRIMARY KEY,
  mission_id TEXT NOT NULL,
  seed INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  execution_policy INTEGER NOT NULL,
  engine TEXT,
  status TEXT NOT NULL CHECK (status IN ('active', 'finished', 'bankrupt', 'left')),
  actions INTEGER NOT NULL DEFAULT 0,
  digest TEXT NOT NULL DEFAULT '',
  options TEXT,
  journal TEXT NOT NULL DEFAULT '[]',
  questions TEXT NOT NULL DEFAULT '[]',
  stop_warnings INTEGER NOT NULL DEFAULT 0,
  campaign TEXT,
  assisted TEXT,
  equity TEXT
);
CREATE TABLE actions (
  run_id TEXT NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,
  seq INTEGER NOT NULL CHECK (seq >= 0),
  action TEXT NOT NULL,
  PRIMARY KEY (run_id, seq)
) WITHOUT ROWID;
CREATE TABLE meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
) WITHOUT ROWID;
)sql",
    // 2: installed packs; a run may play one.
    R"sql(
CREATE TABLE packs (
  digest TEXT PRIMARY KEY CHECK (length(digest) = 64),
  scenario_id TEXT NOT NULL,
  label TEXT NOT NULL,
  size INTEGER NOT NULL,
  private INTEGER NOT NULL,
  total_bars INTEGER NOT NULL,
  warmup_bars INTEGER NOT NULL,
  leverage_cap INTEGER NOT NULL,
  gaps INTEGER NOT NULL DEFAULT 0,
  added_at INTEGER NOT NULL,
  origin TEXT NOT NULL CHECK (origin IN ('import', 'catalog')),
  mode TEXT
) WITHOUT ROWID;
ALTER TABLE runs ADD COLUMN pack_digest TEXT;
)sql",
    // 3: the latest checkpoint of a run.
    R"sql(
CREATE TABLE checkpoints (
  run_id TEXT PRIMARY KEY REFERENCES runs(run_id) ON DELETE CASCADE,
  actions INTEGER NOT NULL,
  hex TEXT NOT NULL,
  digest TEXT NOT NULL,
  engine TEXT NOT NULL,
  series_names TEXT
) WITHOUT ROWID;
)sql",
    // 4: resumable downloads; the bytes live in packs/<digest>.part.
    R"sql(
CREATE TABLE downloads (
  digest TEXT PRIMARY KEY CHECK (length(digest) = 64),
  url TEXT NOT NULL,
  size INTEGER NOT NULL,
  received INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;
)sql",
    // 5: the engine C ABI each run was recorded under (a run with another ABI does not resume).
    R"sql(
ALTER TABLE runs ADD COLUMN engine_abi INTEGER;
)sql",
};

}  // namespace

void migrate_to(Database& db, int version) {
  for (int v = db.user_version(); v < version; ++v) {
    Transaction tx(db);
    db.exec(kSteps.at(static_cast<std::size_t>(v)));
    db.set_user_version(v + 1);
    tx.commit();
  }
}

int migrate(Database& db) {
  const int found = db.user_version();
  if (found > kSchemaVersion) {
    throw SchemaTooNew("database schema v" + std::to_string(found) + " is newer than this build (v" +
                       std::to_string(kSchemaVersion) + ")");
  }
  migrate_to(db, kSchemaVersion);
  return found;
}

}  // namespace tradelab::desktop::storage
