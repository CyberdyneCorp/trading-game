#include "model/storage/run_store.hpp"

#include <algorithm>
#include <chrono>
#include <random>

#include "model/json.hpp"
#include "model/storage/fault.hpp"
#include "model/storage/json_writer.hpp"
#include "model/storage/schema.hpp"

namespace tradelab::desktop::storage {

namespace {

constexpr const char* kRunColumns =
    "run_id, mission_id, seed, created_at, execution_policy, engine, status, actions, digest, options, journal, "
    "questions, stop_warnings, campaign, assisted, equity, pack_digest, engine_abi";

RunRecord read_run(const Statement& s) {
  RunRecord r;
  r.run_id = s.text(0);
  r.mission_id = s.text(1);
  r.seed = static_cast<std::uint32_t>(s.int64(2));
  r.created_at = s.int64(3);
  r.execution_policy = static_cast<int>(s.int64(4));
  r.engine = s.text(5);
  r.status = s.text(6);
  r.actions = s.int64(7);
  r.digest = s.text(8);
  r.options = options_from_json(s.text(9));
  r.journal = s.text(10);
  r.questions = s.text(11);
  r.stop_warnings = static_cast<int>(s.int64(12));
  r.campaign = s.optional_text(13);
  r.assisted = s.optional_text(14);
  r.equity = s.optional_text(15);
  r.pack_digest = s.optional_text(16);
  if (!s.is_null(17)) r.engine_abi = static_cast<int>(s.int64(17));
  return r;
}

constexpr const char* kPackColumns =
    "digest, scenario_id, label, size, private, total_bars, warmup_bars, leverage_cap, gaps, added_at, origin, mode";

StoredPack read_pack(const Statement& s) {
  StoredPack p;
  p.digest = s.text(0);
  p.scenario_id = s.text(1);
  p.label = s.text(2);
  p.size = s.int64(3);
  p.is_private = s.int64(4) != 0;
  p.total_bars = static_cast<int>(s.int64(5));
  p.warmup_bars = static_cast<int>(s.int64(6));
  p.leverage_cap = static_cast<int>(s.int64(7));
  p.gaps = static_cast<int>(s.int64(8));
  p.added_at = s.int64(9);
  p.origin = s.text(10);
  p.mode = s.optional_text(11);
  return p;
}

Download read_download(const Statement& s) { return Download{s.text(0), s.text(1), s.int64(2), s.int64(3)}; }

std::string random_suffix() {
  static std::mt19937_64 gen{std::random_device{}()};
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  for (int i = 0; i < 16; ++i) out += kHex[gen() & 0xF];
  return out;
}

void patch_column(Database& db, const std::string& run_id, const char* column, const std::string& value) {
  db.prepare(std::string("UPDATE runs SET ") + column + " = ? WHERE run_id = ?").bind(1, value).bind(2, run_id).run();
}

}  // namespace

std::string new_run_id() {
  std::string ts;
  for (auto ms = static_cast<std::uint64_t>(now_ms()); ms > 0; ms /= 36) ts.insert(ts.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[ms % 36]);
  return "run-" + ts + "-" + random_suffix();
}

std::int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string options_json(const RunOptions& o) {
  JsonWriter w;
  w.begin_object().field("adviceAllowance", o.advice_allowance).field("newsLead", o.news_lead).field("calendar", o.calendar);
  if (!o.starting_balance.empty()) w.field("startingBalance", o.starting_balance);
  if (o.leverage_cap != 0) w.field("leverageCap", o.leverage_cap);
  return w.end_object().text();
}

RunOptions options_from_json(const std::string& text) {
  const auto v = json::parse(text);
  if (!v) return {};
  RunOptions o;
  o.advice_allowance = static_cast<int>((*v)["adviceAllowance"].as_int(0));
  o.news_lead = static_cast<int>((*v)["newsLead"].as_int(0));
  o.calendar = (*v)["calendar"].as_bool(false);
  o.starting_balance = (*v)["startingBalance"].as_string();
  o.leverage_cap = static_cast<int>((*v)["leverageCap"].as_int(0));
  return o;
}

RunStore::RunStore(const std::string& path) : db_(path) { opened_version_ = migrate(db_); }

// ---- runs ----

void RunStore::leave_active_locked() {
  const auto active = meta("active");
  if (!active) return;
  db_.prepare("UPDATE runs SET status = 'left' WHERE run_id = ? AND status = 'active'").bind(1, *active).run();
}

void RunStore::create_run(const RunRecord& r) {
  Transaction tx(db_);
  leave_active_locked();
  db_.prepare(std::string("INSERT INTO runs (") + kRunColumns +
              ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
      .bind(1, r.run_id)
      .bind(2, r.mission_id)
      .bind(3, static_cast<std::int64_t>(r.seed))
      .bind(4, r.created_at)
      .bind(5, static_cast<std::int64_t>(r.execution_policy))
      .bind(6, r.engine)
      .bind(7, r.status)
      .bind(8, r.actions)
      .bind(9, r.digest)
      .bind(10, options_json(r.options))
      .bind(11, r.journal)
      .bind(12, r.questions)
      .bind(13, static_cast<std::int64_t>(r.stop_warnings))
      .bind(14, r.campaign)
      .bind(15, r.assisted)
      .bind(16, r.equity)
      .bind(17, r.pack_digest)
      .bind(18, r.engine_abi ? std::optional<std::int64_t>(*r.engine_abi) : std::nullopt)
      .run();
  set_meta("active", r.run_id);
  tx.commit();
}

void RunStore::leave_active_run() {
  Transaction tx(db_);
  leave_active_locked();
  tx.commit();
}

void RunStore::append_action(const std::string& run_id, std::int64_t seq, const std::string& action_json) {
  Transaction tx(db_);
  db_.prepare("INSERT INTO actions (run_id, seq, action) VALUES (?, ?, ?)").bind(1, run_id).bind(2, seq).bind(3, action_json).run();
  // The digest describes the state before this action: cleared until settle() records the new one, so a
  // crash in between never reads as a replay mismatch.
  db_.prepare("UPDATE runs SET actions = max(actions, ?), digest = '' WHERE run_id = ?").bind(1, seq + 1).bind(2, run_id).run();
  fault(FaultPoint::MidCommit);
  tx.commit();
}

void RunStore::update_run(const std::string& run_id, const RunPatch& p) {
  Transaction tx(db_);
  if (p.digest) patch_column(db_, run_id, "digest", *p.digest);
  if (p.status) patch_column(db_, run_id, "status", *p.status);
  if (p.equity) patch_column(db_, run_id, "equity", *p.equity);
  if (p.journal) patch_column(db_, run_id, "journal", *p.journal);
  if (p.questions) patch_column(db_, run_id, "questions", *p.questions);
  if (p.stop_warnings) {
    db_.prepare("UPDATE runs SET stop_warnings = ? WHERE run_id = ?").bind(1, *p.stop_warnings).bind(2, run_id).run();
  }
  tx.commit();
}

std::optional<RunRecord> RunStore::run(const std::string& run_id) {
  Statement s = db_.prepare(std::string("SELECT ") + kRunColumns + " FROM runs WHERE run_id = ?");
  s.bind(1, run_id);
  if (!s.step()) return std::nullopt;
  return read_run(s);
}

std::optional<StoredRun> RunStore::active_run() {
  const auto id = meta("active");
  if (!id) return std::nullopt;
  auto record = run(*id);
  if (!record) return std::nullopt;
  StoredRun out{std::move(*record), {}};
  Statement s = db_.prepare("SELECT seq, action FROM actions WHERE run_id = ? ORDER BY seq");
  s.bind(1, *id);
  // Only a gap-free prefix is trusted (a transaction never leaves holes, but be strict).
  while (s.step() && s.int64(0) == static_cast<std::int64_t>(out.actions.size())) out.actions.push_back(s.text(1));
  return out;
}

std::vector<RunRecord> RunStore::runs() {
  const auto active = meta("active");
  std::vector<RunRecord> out;
  Statement s = db_.prepare(std::string("SELECT ") + kRunColumns + " FROM runs ORDER BY created_at DESC, rowid DESC");
  while (s.step()) {
    RunRecord r = read_run(s);
    if (r.status == "active" && r.run_id != active) r.status = "left";
    out.push_back(std::move(r));
  }
  return out;
}

std::vector<std::string> RunStore::finished_missions() {
  std::vector<std::string> out;
  Statement s = db_.prepare(
      "SELECT DISTINCT mission_id FROM runs WHERE status = 'finished' AND pack_digest IS NULL AND assisted IS NULL "
      "ORDER BY mission_id");
  while (s.step()) out.push_back(s.text(0));
  return out;
}

// ---- checkpoints ----

void RunStore::save_checkpoint(const Checkpoint& cp) {
  db_.prepare(
         "INSERT INTO checkpoints (run_id, actions, hex, digest, engine, series_names) VALUES (?, ?, ?, ?, ?, ?) "
         "ON CONFLICT(run_id) DO UPDATE SET actions = excluded.actions, hex = excluded.hex, digest = excluded.digest, "
         "engine = excluded.engine, series_names = excluded.series_names WHERE excluded.actions >= checkpoints.actions")
      .bind(1, cp.run_id)
      .bind(2, cp.actions)
      .bind(3, cp.hex)
      .bind(4, cp.digest)
      .bind(5, cp.engine)
      .bind(6, cp.series_names)
      .run();
}

std::optional<Checkpoint> RunStore::checkpoint(const std::string& run_id) {
  Statement s = db_.prepare("SELECT run_id, actions, hex, digest, engine, series_names FROM checkpoints WHERE run_id = ?");
  s.bind(1, run_id);
  if (!s.step()) return std::nullopt;
  return Checkpoint{s.text(0), s.int64(1), s.text(2), s.text(3), s.text(4), s.optional_text(5)};
}

void RunStore::prune_checkpoints(const std::optional<std::string>& keep_run_id) {
  db_.prepare("DELETE FROM checkpoints WHERE run_id IS NOT ?").bind(1, keep_run_id).run();
}

// ---- meta ----

std::optional<std::string> RunStore::meta(const std::string& key) {
  Statement s = db_.prepare("SELECT value FROM meta WHERE key = ?");
  s.bind(1, key);
  if (!s.step()) return std::nullopt;
  return s.text(0);
}

void RunStore::set_meta(const std::string& key, const std::string& value) {
  db_.prepare("INSERT INTO meta (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value")
      .bind(1, key)
      .bind(2, value)
      .run();
}

Progress RunStore::progress() {
  const auto text = meta("progress");
  const auto v = text ? json::parse(*text) : std::nullopt;
  return migrate_progress(v ? *v : json::Value{});
}

void RunStore::save_progress(const Progress& p) { set_meta("progress", progress_json(p)); }

std::optional<std::string> RunStore::setting(const std::string& key) { return meta("setting:" + key); }

void RunStore::set_setting(const std::string& key, const std::string& value) { set_meta("setting:" + key, value); }

std::string RunStore::device_seed(const std::function<std::string()>& random_hex) {
  Transaction tx(db_);
  if (auto seed = meta("deviceSeed")) return *seed;  // the transaction rolls back: nothing was written
  const std::string seed = random_hex();
  set_meta("deviceSeed", seed);
  tx.commit();
  return seed;
}

// ---- packs ----

void RunStore::save_pack(const StoredPack& p) {
  db_.prepare(std::string("INSERT OR REPLACE INTO packs (") + kPackColumns + ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
      .bind(1, p.digest)
      .bind(2, p.scenario_id)
      .bind(3, p.label)
      .bind(4, p.size)
      .bind(5, static_cast<std::int64_t>(p.is_private ? 1 : 0))
      .bind(6, static_cast<std::int64_t>(p.total_bars))
      .bind(7, static_cast<std::int64_t>(p.warmup_bars))
      .bind(8, static_cast<std::int64_t>(p.leverage_cap))
      .bind(9, static_cast<std::int64_t>(p.gaps))
      .bind(10, p.added_at)
      .bind(11, p.origin)
      .bind(12, p.mode)
      .run();
}

std::optional<StoredPack> RunStore::pack(const std::string& digest) {
  Statement s = db_.prepare(std::string("SELECT ") + kPackColumns + " FROM packs WHERE digest = ?");
  s.bind(1, digest);
  if (!s.step()) return std::nullopt;
  return read_pack(s);
}

std::vector<StoredPack> RunStore::packs() {
  std::vector<StoredPack> out;
  Statement s = db_.prepare(std::string("SELECT ") + kPackColumns + " FROM packs ORDER BY added_at, digest");
  while (s.step()) out.push_back(read_pack(s));
  return out;
}

void RunStore::delete_pack(const std::string& digest) {
  db_.prepare("DELETE FROM packs WHERE digest = ?").bind(1, digest).run();
}

// ---- downloads ----

void RunStore::save_download(const Download& d) {
  db_.prepare("INSERT OR REPLACE INTO downloads (digest, url, size, received) VALUES (?, ?, ?, ?)")
      .bind(1, d.digest)
      .bind(2, d.url)
      .bind(3, d.size)
      .bind(4, d.received)
      .run();
}

std::optional<Download> RunStore::download(const std::string& digest) {
  Statement s = db_.prepare("SELECT digest, url, size, received FROM downloads WHERE digest = ?");
  s.bind(1, digest);
  if (!s.step()) return std::nullopt;
  return read_download(s);
}

std::vector<Download> RunStore::downloads() {
  std::vector<Download> out;
  Statement s = db_.prepare("SELECT digest, url, size, received FROM downloads ORDER BY digest");
  while (s.step()) out.push_back(read_download(s));
  return out;
}

void RunStore::clear_download(const std::string& digest) {
  db_.prepare("DELETE FROM downloads WHERE digest = ?").bind(1, digest).run();
}

}  // namespace tradelab::desktop::storage
