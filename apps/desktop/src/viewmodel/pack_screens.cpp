#include "viewmodel/pack_screens.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <utility>

#include "model/storage/json_writer.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop {

namespace {

constexpr long long kMillisThreshold = 100'000'000'000LL;  // engine rule: seconds past year 5000 are ms

bool all_digits(std::string_view s) {
  return !s.empty() && std::ranges::all_of(s, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) s.remove_suffix(1);
  return s;
}

// The first cell of a CSV line, quotes and spaces removed.
std::string first_cell(std::string_view line) {
  std::string cell(line.substr(0, line.find(',')));
  std::erase(cell, '"');
  return std::string(trim(cell));
}

// First two data rows' integer timestamps, skipping the header (packs.ts firstTimestamps).
std::vector<long long> first_timestamps(std::string_view csv) {
  std::vector<long long> out;
  bool header = true;
  while (!csv.empty() && out.size() < 2) {
    const std::size_t nl = csv.find('\n');
    std::string_view line = csv.substr(0, nl);
    csv = nl == std::string_view::npos ? std::string_view() : csv.substr(nl + 1);
    if (std::exchange(header, false)) continue;
    const std::string cell = first_cell(line);
    if (all_digits(cell) && cell.size() < 19) out.push_back(std::stoll(cell));
  }
  return out;
}

}  // namespace

std::string fmt_bytes(std::int64_t n, Locale locale) {
  const double mb = static_cast<double>(n) / (1024.0 * 1024.0);
  if (mb >= 1) {
    std::string text = fmt::fmt_num(mb, locale, 1);
    const std::string zero = locale == Locale::En ? ".0" : ",0";
    if (text.ends_with(zero)) text.resize(text.size() - 2);  // maximumFractionDigits: 1
    return text + " MB";
  }
  const auto kb = std::max<std::int64_t>(1, (n + 512) / 1024);
  return fmt::fmt_num(static_cast<double>(kb), locale, 0) + " KB";
}

std::string interval_label(int seconds) {
  for (const auto& i : kIntervals) {
    if (i.seconds == seconds) return std::string(i.label);
  }
  return std::to_string(seconds) + "s";
}

CsvMeta guess_meta(std::string_view csv, const CsvMeta& base) {
  const auto ts = first_timestamps(csv);
  CsvMeta out = base;
  if (ts.size() < 2) return out;
  out.timestamp_unit = ts[0] >= kMillisThreshold ? "ms" : "s";
  const long long step = (ts[1] - ts[0]) / (out.timestamp_unit == "ms" ? 1000 : 1);
  for (const auto& i : kIntervals) {
    if (i.seconds == step) out.interval_seconds = i.seconds;
  }
  return out;
}

std::string default_label(const CsvMeta& meta, const std::string& file_name) {
  std::string name = !meta.pair.empty() ? meta.pair : !meta.asset.empty() ? meta.asset : file_name;
  if (meta.pair.empty() && meta.asset.empty()) {
    if (const auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) name.resize(dot);
  }
  std::string label = name + " \xC2\xB7 " + interval_label(meta.interval_seconds);
  // At most 60 characters (UTF-8: never cut inside a code point).
  std::size_t chars = 0;
  for (std::size_t i = 0; i < label.size(); ++i) {
    if ((static_cast<unsigned char>(label[i]) & 0xC0U) != 0x80U && ++chars > 60) {
      label.resize(i);
      break;
    }
  }
  return label;
}

std::string csv_meta_json(const CsvMeta& m) {
  storage::JsonWriter w;
  w.begin_object().field("asset", m.asset).field("pair", m.pair).field("venue", m.venue);
  w.field("volumeUnit", m.volume_unit).field("intervalSeconds", m.interval_seconds);
  w.field("timestampUnit", m.timestamp_unit).field("leverageCap", m.leverage_cap).field("gapFill", m.gap_fill);
  return w.end_object().text();
}

std::string iso_utc(std::int64_t seconds) {
  const auto t = static_cast<std::time_t>(seconds);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

EntryStatus PackScreens::status_of(const packs::CatalogEntry& e) const {
  if (pack(e.digest) != nullptr && !is_missing(e.digest)) return EntryStatus::Installed;
  const auto it = partial.find(e.digest);
  return it != partial.end() && it->second > 0 ? EntryStatus::Partial : EntryStatus::None;
}

bool PackScreens::busy_with(const std::string& digest) const {
  const auto it = downloads.find(digest);
  return it != downloads.end() &&
         (it->second.phase == packs::DownloadPhase::Downloading || it->second.phase == packs::DownloadPhase::Verifying);
}

const storage::StoredPack* PackScreens::pack(const std::string& digest) const {
  const auto it = std::ranges::find_if(installed, [&](const storage::StoredPack& p) { return p.digest == digest; });
  return it == installed.end() ? nullptr : &*it;
}

const storage::StoredPack* PackScreens::pack_of_scenario(const std::string& scenario_id) const {
  const auto it = std::ranges::find_if(installed, [&](const storage::StoredPack& p) { return p.scenario_id == scenario_id; });
  return it == installed.end() ? nullptr : &*it;
}

bool PackScreens::is_missing(const std::string& digest) const { return std::ranges::find(missing, digest) != missing.end(); }

}  // namespace tradelab::desktop
