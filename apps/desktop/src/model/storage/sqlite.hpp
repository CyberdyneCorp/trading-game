// Thin RAII layer over the vendored SQLite (third_party/sqlite): one connection, prepared statements,
// transactions. Errors throw storage::Error; the repository layer above decides what they mean.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace tradelab::desktop::storage {

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Statement {
 public:
  Statement(sqlite3* db, std::string_view sql);
  ~Statement();
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&& other) noexcept;
  Statement& operator=(Statement&&) = delete;

  // Parameters are 1-based, as in SQLite.
  Statement& bind(int index, std::int64_t value);
  Statement& bind(int index, std::string_view text);
  Statement& bind(int index, const char* text) { return bind(index, std::string_view(text)); }
  Statement& bind(int index, const std::string& text) { return bind(index, std::string_view(text)); }
  Statement& bind(int index, int value) { return bind(index, static_cast<std::int64_t>(value)); }
  Statement& bind(int index, const std::optional<std::string>& text);
  Statement& bind_blob(int index, std::span<const std::uint8_t> bytes);
  Statement& bind_null(int index);

  // Next row: true while a row is available, false when done.
  bool step();
  // Runs a statement that returns no rows.
  void run();

  // Columns are 0-based.
  [[nodiscard]] std::int64_t int64(int column) const;
  [[nodiscard]] std::string text(int column) const;
  [[nodiscard]] std::optional<std::string> optional_text(int column) const;
  [[nodiscard]] std::vector<std::uint8_t> blob(int column) const;
  [[nodiscard]] bool is_null(int column) const;

 private:
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};

class Database {
 public:
  // Opens (creating if needed) the database at `path`; ":memory:" for a private in-memory one.
  // Applies the connection settings: WAL (on-disk files), synchronous=FULL, foreign keys, busy timeout.
  explicit Database(const std::string& path);
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  void exec(std::string_view sql);
  [[nodiscard]] Statement prepare(std::string_view sql) { return Statement(db_, sql); }

  [[nodiscard]] int user_version();
  void set_user_version(int version);
  // PRAGMA journal_mode (e.g. "wal"; "memory" for an in-memory database).
  [[nodiscard]] std::string journal_mode();
  [[nodiscard]] std::int64_t changes() const;

  [[nodiscard]] sqlite3* handle() const { return db_; }

 private:
  sqlite3* db_ = nullptr;
};

// BEGIN IMMEDIATE on construction; ROLLBACK on destruction unless commit() ran.
class Transaction {
 public:
  explicit Transaction(Database& db);
  ~Transaction();
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  void commit();

 private:
  Database& db_;
  bool done_ = false;
};

// The SQLite library version the build links (e.g. "3.54.1").
std::string sqlite_version();

}  // namespace tradelab::desktop::storage
