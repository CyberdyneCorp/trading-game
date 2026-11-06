// AppViewModel: the pack catalog (web Catalog.svelte + catalog.svelte.ts), the installed packs and their
// practice runs, and the CSV import wizard (web CsvImport.svelte + csvImport.svelte.ts). Pack work runs
// on the simulation worker (packs::PackCommands); its answers arrive as PackEvents on the UI thread.
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "model/packs/pack_library.hpp"
#include "viewmodel/app_viewmodel.hpp"

namespace tradelab::desktop {

namespace {

int clamp_index(int i, std::size_t size) { return size == 0 ? 0 : std::clamp(i, 0, static_cast<int>(size) - 1); }

}  // namespace

void AppViewModel::attach_packs(packs::PackCommands* commands) {
  pack_commands_ = commands;
  if (pack_commands_ != nullptr) pack_commands_->refresh();
}

void AppViewModel::on_pack_event(const packs::PackEvent& e) {
  using Kind = packs::PackEvent::Kind;
  switch (e.kind) {
    case Kind::Library: library_event(e); break;
    case Kind::Catalog:
      packs_.status = e.status;
      if (e.status != packs::CatalogStatus::Loading) packs_.entries = e.entries;
      packs_.selected = clamp_index(packs_.selected, packs_.entries.size());
      break;
    case Kind::Download:
      // Installed: the entry shows as installed from the library event that follows.
      if (e.download.phase == packs::DownloadPhase::Installed) {
        packs_.downloads.erase(e.digest);
      } else {
        packs_.downloads[e.digest] = e.download;
      }
      break;
    case Kind::CsvReport: csv_report(e); break;
    case Kind::CsvImported: csv_imported(e); break;
  }
  touch();
}

void AppViewModel::library_event(const packs::PackEvent& e) {
  packs_.installed = e.packs;
  packs_.missing = e.missing;
  packs_.partial = e.partial;
  packs_.library_selected = clamp_index(packs_.library_selected, packs_.installed.size());
}

// ---- Catalog ----

void AppViewModel::open_catalog() {
  if (screen_ != Screen::Menu) return;
  screen_ = Screen::Catalog;
  if (pack_commands_ != nullptr) {
    pack_commands_->load_catalog();
  } else {
    packs_.status = packs::CatalogStatus::Unavailable;
  }
  touch();
}

void AppViewModel::catalog_select(int index) {
  const int next = clamp_index(index, packs_.entries.size());
  if (next == packs_.selected) return;
  packs_.selected = next;
  touch();
}

void AppViewModel::catalog_download() {
  if (pack_commands_ == nullptr || packs_.entries.empty() || packs_.status != packs::CatalogStatus::Ready) return;
  const packs::CatalogEntry& e = packs_.entries[static_cast<std::size_t>(packs_.selected)];
  if (packs_.busy_with(e.digest) || packs_.status_of(e) == EntryStatus::Installed) return;
  const std::string label = packs::localized(e.title, locale_code(locale()));
  packs_.downloads[e.digest] = {packs::DownloadPhase::Downloading, packs_.partial[e.digest], e.size, {}};
  pack_commands_->download(e, label);
  touch();
}

void AppViewModel::catalog_pause() {
  if (pack_commands_ == nullptr || packs_.entries.empty()) return;
  pack_commands_->pause(packs_.entries[static_cast<std::size_t>(packs_.selected)].digest);
}

// Downloads run on the simulation worker, ahead of any run started meanwhile: leaving the catalog pauses
// them (what arrived is kept; Resume continues from there).
void AppViewModel::pause_downloads() {
  if (pack_commands_ == nullptr) return;
  for (const auto& [digest, d] : packs_.downloads) {
    if (d.phase == packs::DownloadPhase::Downloading) pack_commands_->pause(digest);
  }
}

void AppViewModel::catalog_play() {
  if (packs_.entries.empty()) return;
  const packs::CatalogEntry& e = packs_.entries[static_cast<std::size_t>(packs_.selected)];
  if (packs_.status_of(e) == EntryStatus::Installed) start_pack(e.digest);
}

void AppViewModel::catalog_retry() {
  if (pack_commands_ != nullptr && screen_ == Screen::Catalog) pack_commands_->load_catalog();
}

void AppViewModel::library_select(int index) {
  const int next = clamp_index(index, packs_.installed.size());
  if (next == packs_.library_selected) return;
  packs_.library_selected = next;
  touch();
}

void AppViewModel::library_play() {
  if (packs_.installed.empty()) return;
  start_pack(packs_.installed[static_cast<std::size_t>(packs_.library_selected)].digest);
}

void AppViewModel::library_remove() {
  if (pack_commands_ == nullptr || packs_.installed.empty() || waiting()) return;
  pack_commands_->remove(packs_.installed[static_cast<std::size_t>(packs_.library_selected)].digest);
}

// A practice run on an installed pack: no advisor, no perks, no tutorial (web session.startPack).
void AppViewModel::start_pack(const std::string& digest) {
  if (in_flight_ > 0 || packs_.is_missing(digest)) return;
  leave_active_run();
  if (send([&] { engine_.start_pack(digest, seeds_()); })) {
    saved_run_.reset();
    start_pending_ = true;
    shell_.run_campaign.reset();
    shell_.pending_assisted.reset();
    shell_.pending_pack = digest;
    if (const auto* p = packs_.pack(digest)) run_mission_ = p->scenario_id;
    notice_.clear();
  }
}

std::string AppViewModel::pack_label_of(const std::string& mission) const {
  const storage::StoredPack* p = packs_.pack_of_scenario(mission);
  return p != nullptr ? p->label : std::string();
}

// ---- CSV import wizard ----

void AppViewModel::open_import() {
  if (screen_ != Screen::Menu) return;
  const std::string path = packs_.path;
  packs_.step = PackScreens::Step::File;
  packs_.file_name.clear();
  packs_.csv.clear();
  packs_.meta = CsvMeta{};
  packs_.label.clear();
  packs_.report.reset();
  packs_.error.clear();
  packs_.busy = false;
  packs_.path = path;  // the folder typed last time stays handy
  screen_ = Screen::Import;
  touch();
}

void AppViewModel::import_set(ImportField field, std::string value) {
  CsvMeta& m = packs_.meta;
  switch (field) {
    case ImportField::Path: packs_.path = std::move(value); break;
    case ImportField::Asset: m.asset = std::move(value); break;
    case ImportField::Pair: m.pair = std::move(value); break;
    case ImportField::Venue: m.venue = std::move(value); break;
    case ImportField::Label: packs_.label = std::move(value); break;
    case ImportField::Leverage: {
      int lev = m.leverage_cap;
      std::from_chars(value.data(), value.data() + value.size(), lev);
      m.leverage_cap = lev;
      break;
    }
  }
  touch();
}

void AppViewModel::import_choose(ImportChoice choice, int index) {
  CsvMeta& m = packs_.meta;
  switch (choice) {
    case ImportChoice::Interval: m.interval_seconds = kIntervals[clamp_index(index, std::size(kIntervals))].seconds; break;
    case ImportChoice::TimestampUnit: m.timestamp_unit = index == 1 ? "ms" : "s"; break;
    case ImportChoice::VolumeUnit: m.volume_unit = index == 1 ? "quote" : "base"; break;
    case ImportChoice::GapFill: m.gap_fill = std::string(kGapFills[clamp_index(index, std::size(kGapFills))]); break;
  }
  touch();
}

// The file at the typed path (the web's file input): too big is refused before reading it.
void AppViewModel::import_open_file() {
  namespace fs = std::filesystem;
  packs_.error.clear();
  std::error_code ec;
  const fs::path path(packs_.path);
  const auto size = fs::file_size(path, ec);
  if (ec) {
    packs_.error = tr("desktop.csv.unreadable");
    return touch();
  }
  if (size > packs::kCsvMaxBytes) {
    packs_.error = tr("csv.tooBig");
    return touch();
  }
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  if (!in && !in.eof()) {
    packs_.error = tr("desktop.csv.unreadable");
    return touch();
  }
  packs_.csv = text.str();
  packs_.file_name = path.filename().string();
  packs_.meta = guess_meta(packs_.csv, packs_.meta);
  packs_.report.reset();
  packs_.step = PackScreens::Step::Meta;
  touch();
}

void AppViewModel::import_validate() {
  if (packs_.busy || packs_.step != PackScreens::Step::Meta || pack_commands_ == nullptr) return;
  packs_.busy = true;
  packs_.error.clear();
  pack_commands_->validate_csv(packs_.csv, csv_meta_json(packs_.meta));
  touch();
}

void AppViewModel::csv_report(const packs::PackEvent& e) {
  if (screen_ != Screen::Import || !packs_.busy) return;
  packs_.busy = false;
  const auto report = json::parse(e.report_json);
  if (!report) {
    packs_.error = e.report_json;
    return;
  }
  if (const json::Value& field = (*report)["metaError"]; field.kind() == json::Value::Kind::String) {
    const std::string key = "csvMeta." + field.as_string();
    packs_.error = catalog_.has(key) ? tr(key) : tr("csv.meta", {{"field", field.as_string()}});
    return;
  }
  packs_.report = *report;
  packs_.step = PackScreens::Step::Report;
}

void AppViewModel::import_back() {
  if (screen_ != Screen::Import || packs_.busy) return;
  packs_.error.clear();
  packs_.report.reset();
  packs_.step = PackScreens::Step::Meta;
  touch();
}

void AppViewModel::import_create() {
  const bool valid = packs_.report && (*packs_.report)["ok"].as_bool(false);
  if (packs_.busy || packs_.step != PackScreens::Step::Report || !valid || pack_commands_ == nullptr) return;
  packs_.busy = true;
  packs_.error.clear();
  const std::string label = packs_.label.empty() ? default_label(packs_.meta, packs_.file_name) : packs_.label;
  pack_commands_->import_csv(packs_.csv, csv_meta_json(packs_.meta), label);
  touch();
}

// Created: back to the menu (web) with the pack listed; refused: the engine's reason.
void AppViewModel::csv_imported(const packs::PackEvent& e) {
  if (screen_ != Screen::Import || !packs_.busy) return;
  packs_.busy = false;
  if (!e.ok) {
    const std::string key = "packReason." + e.reason.substr(0, e.reason.find(':'));
    const std::string reason = catalog_.has(key) ? tr(key, {{"arg", ""}}) : e.reason;
    packs_.error = tr("csv.failed", {{"reason", reason}});
    return;
  }
  screen_ = Screen::Menu;
  notice_ = tr("pack.installed", {{"label", packs_.label.empty() ? default_label(packs_.meta, packs_.file_name) : packs_.label}});
}

void AppViewModel::import_cancel() {
  if (screen_ != Screen::Import) return;
  screen_ = Screen::Menu;
  touch();
}

}  // namespace tradelab::desktop
