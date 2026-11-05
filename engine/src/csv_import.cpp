// Private CSV import (spec scenario-packs "Private CSV import"): strict, locale-independent
// parsing and validation with a gap report. Nothing is corrected silently: a timestamp in the
// wrong unit, a duplicate or a bar whose high is below its body is an error, and missing bars are
// reported as gaps, never filled.
//
// Issue codes: file_too_large, empty_file, missing_column:<name>, too_many_rows, too_few_rows,
// line_too_long, column_count, invalid_timestamp, timestamp_unit, invalid_number,
// invalid_interval, non_positive_price, negative_volume, invalid_funding, high_inconsistent, low_inconsistent,
// duplicate_timestamp, not_increasing, interval_mismatch.

#include <algorithm>
#include <array>
#include <optional>

#include "tradelab/pack.hpp"

namespace tradelab {
namespace {

constexpr std::size_t kMaxLine = 512;
constexpr std::int64_t kMillisThreshold = 100'000'000'000;  // 1e11: seconds beyond year 5000
constexpr std::uint32_t kMaxRows = 20'000;

enum Col : std::uint8_t { kTime, kOpen, kHigh, kLow, kClose, kVolume, kFunding, kColCount };

constexpr std::array<std::string_view, kColCount> kColNames = {"timestamp", "open", "high", "low", "close", "volume", "funding"};

std::string lower_trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '"')) s.remove_suffix(1);
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; });
  return out;
}

std::optional<Col> column_for(const std::string& name) {
  static constexpr std::array<std::pair<std::string_view, Col>, 17> kAliases = {{
      {"timestamp", kTime}, {"time", kTime}, {"date", kTime}, {"datetime", kTime}, {"open_time", kTime}, {"ts", kTime},
      {"open", kOpen}, {"high", kHigh}, {"low", kLow}, {"close", kClose},
      {"volume", kVolume}, {"vol", kVolume}, {"funding", kFunding}, {"funding_rate", kFunding}, {"fundingrate", kFunding},
      {"o", kOpen}, {"c", kClose},
  }};
  for (const auto& [alias, col] : kAliases) {
    if (alias == name) return col;
  }
  return std::nullopt;
}

// Trims spaces and one pair of surrounding double quotes (fields never contain commas or quotes).
std::string_view clean_field(std::string_view f) {
  while (!f.empty() && f.front() == ' ') f.remove_prefix(1);
  while (!f.empty() && f.back() == ' ') f.remove_suffix(1);
  if (f.size() >= 2 && f.front() == '"' && f.back() == '"') f = f.substr(1, f.size() - 2);
  return f;
}

std::vector<std::string_view> split(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= line.size(); ++i) {
    if (i == line.size() || line[i] == ',') {
      out.push_back(clean_field(line.substr(start, i - start)));
      start = i + 1;
    }
  }
  return out;
}

bool all_digits(std::string_view s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::optional<std::int64_t> parse_uint(std::string_view s) {
  if (!all_digits(s) || s.size() > 15) return std::nullopt;
  std::int64_t v = 0;
  for (const char c : s) v = v * 10 + (c - '0');
  return v;
}

// Howard Hinnant's days_from_civil: proleptic Gregorian date -> days since 1970-01-01.
std::int64_t days_from_civil(std::int64_t y, std::int64_t m, std::int64_t d) {
  y -= m <= 2 ? 1 : 0;
  const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
  const std::int64_t yoe = y - era * 400;
  const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool valid_date(std::int64_t y, std::int64_t m, std::int64_t d) {
  static constexpr std::array<std::int64_t, 12> kDays = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12 || d < 1 || d > kDays[static_cast<std::size_t>(m - 1)]) return false;
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m != 2 || d <= 28 || leap;
}

std::optional<std::int64_t> number_at(std::string_view s, std::size_t at, std::size_t len) {
  return at + len <= s.size() ? parse_uint(s.substr(at, len)) : std::nullopt;
}

// UTC time zone suffix after the seconds: none, "Z", "+00:00" or ".000Z"-style zero fractions.
bool utc_suffix(std::string_view rest) {
  if (!rest.empty() && rest.front() == '.') {
    rest.remove_prefix(1);
    while (!rest.empty() && rest.front() == '0') rest.remove_prefix(1);
  }
  return rest.empty() || rest == "Z" || rest == "+00:00" || rest == "+0000";
}

// "YYYY-MM-DD", "YYYY-MM-DD HH:MM[:SS]" or with 'T', UTC only.
std::optional<std::int64_t> parse_iso(std::string_view s) {
  const auto y = number_at(s, 0, 4);
  const auto mo = number_at(s, 5, 2);
  const auto d = number_at(s, 8, 2);
  if (!y || !mo || !d || s[4] != '-' || s[7] != '-' || !valid_date(*y, *mo, *d)) return std::nullopt;
  std::int64_t secs = days_from_civil(*y, *mo, *d) * 86400;
  if (s.size() == 10) return secs;
  if (s[10] != 'T' && s[10] != ' ') return std::nullopt;
  const auto h = number_at(s, 11, 2);
  const auto mi = number_at(s, 14, 2);
  if (!h || !mi || s.size() < 16 || s[13] != ':' || *h > 23 || *mi > 59) return std::nullopt;
  secs += *h * 3600 + *mi * 60;
  std::size_t rest = 16;
  if (s.size() >= 19 && s[16] == ':') {
    const auto sec = number_at(s, 17, 2);
    if (!sec || *sec > 59) return std::nullopt;
    secs += *sec;
    rest = 19;
  }
  return utc_suffix(s.substr(rest)) ? std::optional(secs) : std::nullopt;
}

class Importer {
 public:
  Importer(const CsvMeta& meta, CsvReport& rep, std::vector<CsvRow>& rows) : meta_(meta), rep_(rep), rows_(rows) {}

  void run(std::string_view text) {
    if (text.size() > kCsvMaxBytes) return issue(0, "file_too_large");
    if (meta_.interval_seconds == 0) return issue(0, "invalid_interval");
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);  // UTF-8 BOM
    std::uint32_t line_no = 0;
    std::size_t pos = 0;
    while (pos < text.size() && !stop_) {
      std::size_t end = text.find('\n', pos);
      if (end == std::string_view::npos) end = text.size();
      std::string_view line = text.substr(pos, end - pos);
      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      pos = end + 1;
      ++line_no;
      if (!line.empty()) on_line(line_no, line);
    }
    finish();
  }

 private:
  void issue(std::uint32_t line, std::string code) {
    ++rep_.error_count;
    if (rep_.errors.size() < kCsvMaxListed) rep_.errors.push_back({line, std::move(code)});
  }

  void on_line(std::uint32_t line_no, std::string_view line) {
    if (line.size() > kMaxLine) return issue(line_no, "line_too_long");
    const auto fields = split(line);
    if (!have_header_) return header(line_no, fields);
    if (rep_.rows >= kMaxRows) {
      stop_ = true;
      return issue(line_no, "too_many_rows");
    }
    ++rep_.rows;
    row(line_no, fields);
  }

  void header(std::uint32_t line_no, const std::vector<std::string_view>& fields) {
    have_header_ = true;
    cols_.fill(-1);
    for (std::size_t i = 0; i < fields.size(); ++i) {
      const auto col = column_for(lower_trim(fields[i]));
      if (col && cols_[*col] < 0) cols_[*col] = static_cast<int>(i);
    }
    for (std::size_t c = kTime; c < kFunding; ++c) {
      if (cols_[c] < 0) issue(line_no, "missing_column:" + std::string(kColNames[c]));
    }
    rep_.has_funding = cols_[kFunding] >= 0;
    stop_ = rep_.error_count > 0;
  }

  std::optional<std::int64_t> timestamp(std::uint32_t line_no, std::string_view f) {
    if (!all_digits(f)) {
      const auto t = parse_iso(f);
      if (!t) issue(line_no, "invalid_timestamp");
      return t;
    }
    const auto v = parse_uint(f);
    if (!v) {
      issue(line_no, "invalid_timestamp");
      return std::nullopt;
    }
    const bool millis = meta_.timestamp_unit == TimestampUnit::Milliseconds;
    if ((*v >= kMillisThreshold) != millis) {
      if (!unit_reported_) issue(line_no, "timestamp_unit");  // DATA01: no silent correction
      unit_reported_ = true;
      return std::nullopt;
    }
    if (millis && *v % 1000 != 0) {
      issue(line_no, "invalid_timestamp");
      return std::nullopt;
    }
    return millis ? *v / 1000 : *v;
  }

  template <typename F>
  bool decimal(std::uint32_t line_no, std::string_view f, F* out) {
    const auto r = parse_decimal<F>(f);
    if (!r.ok()) {
      issue(line_no, "invalid_number");
      return false;
    }
    *out = r.value();
    return true;
  }

  [[nodiscard]] std::string_view field(const std::vector<std::string_view>& fields, Col c) const {
    return fields[static_cast<std::size_t>(cols_[c])];
  }

  bool prices(std::uint32_t line_no, const std::vector<std::string_view>& fields, CsvRow& r) {
    return decimal(line_no, field(fields, kOpen), &r.open) && decimal(line_no, field(fields, kHigh), &r.high) &&
           decimal(line_no, field(fields, kLow), &r.low) && decimal(line_no, field(fields, kClose), &r.close);
  }

  bool volume(std::uint32_t line_no, std::string_view f, CsvRow& r) {
    if (meta_.volume_unit == VolumeUnit::Base) return decimal(line_no, f, &r.volume);
    Money quote;
    if (!decimal(line_no, f, &quote)) return false;
    if (r.close.raw <= 0) return true;  // reported by the price checks
    const auto base = div<Qty>(quote, r.close, Rounding::Floor);
    if (!base.ok()) {
      issue(line_no, "invalid_number");
      return false;
    }
    r.volume = base.value();
    return true;
  }

  bool funding(std::uint32_t line_no, const std::vector<std::string_view>& fields, CsvRow& r) {
    r.funding = last_funding_;
    if (cols_[kFunding] < 0 || field(fields, kFunding).empty()) return true;
    if (!decimal(line_no, field(fields, kFunding), &r.funding)) return false;
    if (r.funding.raw <= -Rate::kScale || r.funding.raw >= Rate::kScale) {
      issue(line_no, "invalid_funding");
      return false;
    }
    last_funding_ = r.funding;
    return true;
  }

  bool geometry(std::uint32_t line_no, const CsvRow& r) {
    if (r.open.raw <= 0 || r.high.raw <= 0 || r.low.raw <= 0 || r.close.raw <= 0) {
      issue(line_no, "non_positive_price");
      return false;
    }
    if (r.volume.raw < 0) {
      issue(line_no, "negative_volume");
      return false;
    }
    const bool high_ok = r.high >= std::max({r.open, r.close, r.low});
    const bool low_ok = r.low <= std::min({r.open, r.close, r.high});
    if (!high_ok) issue(line_no, "high_inconsistent");
    if (!low_ok) issue(line_no, "low_inconsistent");
    return high_ok && low_ok;
  }

  void sequence(std::uint32_t line_no, const CsvRow& r) {
    if (rows_.empty()) return;
    const std::int64_t prev = rows_.back().time;
    const std::int64_t d = r.time - prev;
    const auto step = static_cast<std::int64_t>(meta_.interval_seconds);
    if (d == 0) return issue(line_no, "duplicate_timestamp");
    if (d < 0) return issue(line_no, "not_increasing");
    if (d % step != 0) return issue(line_no, "interval_mismatch");
    if (d == step) return;
    ++rep_.gap_count;
    if (rep_.gaps.size() < kCsvMaxListed) rep_.gaps.push_back({line_no, prev, r.time, static_cast<std::uint32_t>(d / step - 1)});
  }

  void row(std::uint32_t line_no, const std::vector<std::string_view>& fields) {
    const auto needed = static_cast<std::size_t>(*std::max_element(cols_.begin(), cols_.end()));
    if (fields.size() <= needed) return issue(line_no, "column_count");
    const auto t = timestamp(line_no, field(fields, kTime));
    CsvRow r;
    if (!t || !prices(line_no, fields, r) || !volume(line_no, field(fields, kVolume), r) || !funding(line_no, fields, r)) return;
    r.time = *t;
    if (!geometry(line_no, r)) return;
    const std::uint32_t before = rep_.error_count;
    sequence(line_no, r);
    if (rep_.error_count == before) rows_.push_back(r);
  }

  void finish() {
    if (!have_header_) issue(0, "empty_file");
    else if (rep_.error_count == 0 && rows_.size() < kCsvMinRows) issue(0, "too_few_rows");
    if (!rows_.empty()) {
      rep_.first_time = rows_.front().time;
      rep_.last_time = rows_.back().time;
    }
    rep_.ok = rep_.error_count == 0;
  }

  const CsvMeta& meta_;
  CsvReport& rep_;
  std::vector<CsvRow>& rows_;
  std::array<int, kColCount> cols_{};
  Rate last_funding_;
  bool have_header_ = false;
  bool unit_reported_ = false;
  bool stop_ = false;
};

}  // namespace

CsvReport import_csv(std::string_view text, const CsvMeta& meta, std::vector<CsvRow>* rows) {
  CsvReport rep;
  std::vector<CsvRow> parsed;
  Importer(meta, rep, parsed).run(text);
  if (rep.ok && rows != nullptr) *rows = std::move(parsed);
  return rep;
}

}  // namespace tradelab
