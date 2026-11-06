#include "model/storage/sqlite.hpp"

#include <sqlite3.h>

#include <utility>

namespace tradelab::desktop::storage {

namespace {

[[noreturn]] void fail(sqlite3* db, std::string_view what) {
  std::string msg(what);
  if (db != nullptr) msg += std::string(": ") + sqlite3_errmsg(db);
  throw Error(msg);
}

void check(sqlite3* db, int rc, std::string_view what) {
  if (rc != SQLITE_OK) fail(db, what);
}

}  // namespace

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db) {
  check(db_, sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr), sql);
}

Statement::~Statement() { sqlite3_finalize(stmt_); }

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::bind(int index, std::int64_t value) {
  check(db_, sqlite3_bind_int64(stmt_, index, value), "bind");
  return *this;
}

Statement& Statement::bind(int index, std::string_view text) {
  check(db_, sqlite3_bind_text(stmt_, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT), "bind");
  return *this;
}

Statement& Statement::bind(int index, const std::optional<std::string>& text) {
  return text ? bind(index, std::string_view(*text)) : bind_null(index);
}

Statement& Statement::bind(int index, const std::optional<std::int64_t>& value) {
  return value ? bind(index, *value) : bind_null(index);
}

Statement& Statement::bind_blob(int index, std::span<const std::uint8_t> bytes) {
  check(db_, sqlite3_bind_blob(stmt_, index, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT), "bind");
  return *this;
}

Statement& Statement::bind_null(int index) {
  check(db_, sqlite3_bind_null(stmt_, index), "bind");
  return *this;
}

bool Statement::step() {
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  fail(db_, "step");
}

void Statement::run() {
  while (step()) {
  }
}

std::int64_t Statement::int64(int column) const { return sqlite3_column_int64(stmt_, column); }

std::string Statement::text(int column) const {
  const auto* p = sqlite3_column_text(stmt_, column);
  const int n = sqlite3_column_bytes(stmt_, column);
  return p == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n));
}

std::optional<std::string> Statement::optional_text(int column) const {
  if (is_null(column)) return std::nullopt;
  return text(column);
}

std::vector<std::uint8_t> Statement::blob(int column) const {
  const auto* p = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt_, column));
  const int n = sqlite3_column_bytes(stmt_, column);
  return p == nullptr ? std::vector<std::uint8_t>() : std::vector<std::uint8_t>(p, p + n);
}

bool Statement::is_null(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }

Database::Database(const std::string& path) {
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
    std::string msg = "open " + path;
    if (db_ != nullptr) msg += std::string(": ") + sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    throw Error(msg);
  }
  sqlite3_busy_timeout(db_, 2000);
  try {
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=FULL");  // an acknowledged command survives power loss
    exec("PRAGMA foreign_keys=ON");
  } catch (...) {
    sqlite3_close(db_);
    throw;
  }
}

Database::~Database() { sqlite3_close(db_); }

void Database::exec(std::string_view sql) {
  const std::string text(sql);
  char* err = nullptr;
  if (sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = text + ": " + (err != nullptr ? err : "error");
    sqlite3_free(err);
    throw Error(msg);
  }
}

int Database::user_version() {
  Statement s = prepare("PRAGMA user_version");
  return s.step() ? static_cast<int>(s.int64(0)) : 0;
}

void Database::set_user_version(int version) { exec("PRAGMA user_version=" + std::to_string(version)); }

std::string Database::journal_mode() {
  Statement s = prepare("PRAGMA journal_mode");
  return s.step() ? s.text(0) : std::string();
}

std::int64_t Database::changes() const { return sqlite3_changes64(db_); }

Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN IMMEDIATE"); }

Transaction::~Transaction() {
  if (done_) return;
  try {
    db_.exec("ROLLBACK");
  } catch (const Error&) {  // nothing more to undo
  }
}

void Transaction::commit() {
  db_.exec("COMMIT");
  done_ = true;
}

std::string sqlite_version() { return sqlite3_libversion(); }

}  // namespace tradelab::desktop::storage
