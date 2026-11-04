#include <doctest/doctest.h>

#include <string>

#include "pack_fixtures.hpp"

using namespace tradelab;

namespace {

bool has_error(const CsvReport& r, std::string_view code, std::uint32_t line = 0) {
  for (const CsvIssue& e : r.errors) {
    if (e.code == code && (line == 0 || e.line == line)) return true;
  }
  return false;
}

std::string header() { return "timestamp,open,high,low,close,volume\n"; }

// Valid file of n rows with `extra` appended after row `at` (0 = header line).
std::string with_line(std::size_t n, std::size_t at, const std::string& line) {
  const std::string csv = test::make_csv(n);
  std::size_t pos = 0;
  for (std::size_t i = 0; i <= at; ++i) pos = csv.find('\n', pos) + 1;
  return csv.substr(0, pos) + line + "\n" + csv.substr(pos);
}

}  // namespace

TEST_CASE("csv: a clean hourly file imports with no errors and no gaps") {
  std::vector<CsvRow> rows;
  const CsvReport r = import_csv(test::make_csv(120), test::hourly_meta(), &rows);
  CHECK(r.ok);
  CHECK(r.rows == 120);
  CHECK(rows.size() == 120);
  CHECK(r.gap_count == 0);
  CHECK_FALSE(r.has_funding);
  CHECK(r.first_time == 1'700'000'000);
  CHECK(r.last_time == 1'700'000'000 + 119 * 3600);
  CHECK(rows[0].open == Price::from_raw(3'000'000'000'000));
}

TEST_CASE("csv: timestamps in milliseconds declared as seconds fail clearly (DATA01)") {
  std::vector<CsvRow> rows;
  const CsvReport r = import_csv(test::make_csv(80, 1'700'000'000'000LL, 3'600'000), test::hourly_meta(), &rows);
  CHECK_FALSE(r.ok);
  CHECK(has_error(r, "timestamp_unit", 2));
  CHECK(r.error_count == 1);  // reported once, not per row, and nothing silently rescaled
  CHECK(rows.empty());

  CsvMeta ms = test::hourly_meta();
  ms.timestamp_unit = TimestampUnit::Milliseconds;
  CHECK(import_csv(test::make_csv(80, 1'700'000'000'000LL, 3'600'000), ms, &rows).ok);
  CHECK(rows[1].time - rows[0].time == 3600);
  CHECK_FALSE(import_csv(test::make_csv(80), ms, nullptr).ok);  // seconds declared as milliseconds
}

TEST_CASE("csv: gaps are reported with their size and never filled") {
  std::string csv = header();
  for (int i = 0; i < 60; ++i) {
    const int t = i < 30 ? i : i + 3;  // three missing hours after row 30
    csv += std::to_string(1'700'000'000 + t * 3600) + ",100,101,99,100.5,10\n";
  }
  std::vector<CsvRow> rows;
  const CsvReport r = import_csv(csv, test::hourly_meta(), &rows);
  CHECK(r.ok);
  REQUIRE(r.gap_count == 1);
  CHECK(r.gaps[0].missing == 3);
  CHECK(r.gaps[0].line == 32);
  CHECK(r.gaps[0].to - r.gaps[0].from == 4 * 3600);
  CHECK(rows.size() == 60);  // no invented bars
}

TEST_CASE("csv: validation rules name the line and the rule") {
  const CsvMeta meta = test::hourly_meta();
  CHECK(has_error(import_csv(with_line(60, 5, "1700014400,100,101,99,100,1"), meta, nullptr), "duplicate_timestamp", 7));
  CHECK(has_error(import_csv(with_line(60, 5, "1699000000,100,101,99,100,1"), meta, nullptr), "not_increasing", 7));
  CHECK(has_error(import_csv(with_line(60, 5, "1700016200,100,101,99,100,1"), meta, nullptr), "interval_mismatch", 7));
  CHECK(has_error(import_csv(with_line(60, 60, "1700216000,0,101,99,100,1"), meta, nullptr), "non_positive_price", 62));
  CHECK(has_error(import_csv(with_line(60, 60, "1700216000,100,101,99,100,-1"), meta, nullptr), "negative_volume", 62));
  CHECK(has_error(import_csv(with_line(60, 60, "1700216000,100,99.5,99,100,1"), meta, nullptr), "high_inconsistent", 62));
  CHECK(has_error(import_csv(with_line(60, 60, "1700216000,100,101,100.5,100.2,1"), meta, nullptr), "low_inconsistent", 62));
  CHECK(has_error(import_csv(with_line(60, 60, "1700216000,1e2,101,99,100,1"), meta, nullptr), "invalid_number", 62));
  CHECK(has_error(import_csv(with_line(60, 60, "1700216000,100,101"), meta, nullptr), "column_count", 62));
  CHECK(has_error(import_csv(with_line(60, 60, "yesterday,100,101,99,100,1"), meta, nullptr), "invalid_timestamp", 62));
  CHECK(has_error(import_csv("time,open,high,low,close\n1,2,3,1,2\n", meta, nullptr), "missing_column:volume", 1));
  CHECK(has_error(import_csv(test::make_csv(10), meta, nullptr), "too_few_rows"));
  CHECK(has_error(import_csv("", meta, nullptr), "empty_file"));
  CHECK(has_error(import_csv(std::string(kCsvMaxBytes + 1, 'x'), meta, nullptr), "file_too_large"));
}

TEST_CASE("csv: ISO dates, quoted fields, BOM, CRLF and column order are accepted") {
  std::string csv = "\xEF\xBB\xBF\"Date\",\"Close\",\"Open\",\"High\",\"Low\",\"Volume\"\r\n";
  for (int i = 0; i < 60; ++i) {
    const int h = i % 24;
    const int d = 1 + i / 24;
    csv += "\"2024-02-0" + std::to_string(d) + "T" + (h < 10 ? "0" : "") + std::to_string(h) + ":00:00Z\",100.5,100,101,99,7\r\n";
  }
  std::vector<CsvRow> rows;
  const CsvReport r = import_csv(csv, test::hourly_meta(), &rows);
  CHECK(r.ok);
  CHECK(r.first_time == 1'706'745'600);  // 2024-02-01T00:00:00Z
  CHECK(rows[0].close == Price::from_raw(10'050'000'000));
  CHECK(has_error(import_csv("time,open,high,low,close,volume\n2024-02-30 00:00,1,1,1,1,1\n", test::hourly_meta(), nullptr),
                  "invalid_timestamp", 2));
}

TEST_CASE("csv: quote volume is converted with the close; funding is carried forward") {
  CsvMeta meta = test::hourly_meta();
  meta.volume_unit = VolumeUnit::Quote;
  std::vector<CsvRow> rows;
  REQUIRE(import_csv(test::make_csv(60, 1'700'000'000, 3600, 30'000.0, true), meta, &rows).ok);
  CHECK(rows[0].volume.raw > 0);
  CHECK(rows[0].volume.raw < Qty::kScale);  // 100.5 quote units at ~30,000 is a fraction of a coin
  CHECK(rows[0].funding == Rate::from_raw(1'000'000));
  CHECK(rows[3].funding == rows[0].funding);  // empty cells keep the last published rate
  CHECK(import_csv(with_line(60, 60, "1700216000,100,101,99,100,1,2"), test::hourly_meta(), nullptr).ok);  // extra cells ignored
}
