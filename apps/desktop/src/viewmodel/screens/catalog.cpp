// Pack catalog (web Catalog.svelte): published packs with their installed / partial / missing state,
// resumable downloads (verified, then installed) and the installed packs to practice on (the web menu's
// practice panel). Offline or unavailable, the last catalog seen stays listed with downloads disabled.
#include <algorithm>

#include "viewmodel/format.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

using packs::CatalogStatus;
using packs::DownloadPhase;

constexpr std::string_view kPlay = "\xE2\x96\xB6 ";  // "▶ "

std::string size_text(const AppViewModel& vm, std::int64_t n) { return fmt_bytes(n, vm.locale()); }

// Catalog.svelte stateText().
std::string download_text(const AppViewModel& vm, const packs::DownloadState& d) {
  const Params p{{"received", size_text(vm, d.received)}, {"size", size_text(vm, d.size)}};
  switch (d.phase) {
    case DownloadPhase::Downloading: return vm.tr("catalog.progress", p);
    case DownloadPhase::Verifying: return vm.tr("catalog.verifying");
    case DownloadPhase::Refused: {
      const std::string key = "packReason." + d.reason.substr(0, d.reason.find(':'));
      const std::string reason = vm.catalog().has(key) ? vm.tr(key, {{"arg", ""}}) : d.reason;
      return vm.tr("pack.refused", {{"reason", reason}});
    }
    default: break;
  }
  const std::string key = "catalog.failed." + d.reason;
  return vm.tr(vm.catalog().has(key) ? key : "catalog.failed.interrupted", p);
}

void status_lines(const AppViewModel& vm, ScreenModel& m) {
  const PackScreens& s = vm.packs();
  std::string text;
  if (s.status == CatalogStatus::Idle || s.status == CatalogStatus::Loading) text = vm.tr("catalog.loading");
  if (s.status == CatalogStatus::Offline) text = "[" + vm.tr("pwa.offline") + "] " + vm.tr("catalog.offline");
  if (s.status == CatalogStatus::Unavailable) text = vm.tr("catalog.unavailable");
  if (s.status == CatalogStatus::Ready && s.entries.empty()) text = vm.tr("catalog.empty");
  m.text(names::kCatalogState, text);
  m.show(names::kCatalogState, !text.empty());
  m.text(names::kCatalogRetry, vm.tr("catalog.retry"));
  m.show(names::kCatalogRetry, s.status == CatalogStatus::Unavailable && vm.packs_available());
  const bool cached = s.status != CatalogStatus::Ready && s.status != CatalogStatus::Loading && !s.entries.empty();
  m.text(names::kCatalogCached, vm.tr("catalog.cached"));
  m.show(names::kCatalogCached, cached);
}

std::string entry_tags(const AppViewModel& vm, const packs::CatalogEntry& e) {
  const storage::StoredPack* installed = vm.packs().pack(e.digest);
  const std::string mode = installed != nullptr && installed->mode ? *installed->mode : e.mode.value_or("");
  std::string tags = "[" + vm.tr(mode != "synthetic" ? "top.known" : "top.synthetic") + "]";
  tags += std::string(kSep) + interval_label(e.interval_seconds);
  tags += std::string(kSep) + fmt::fmt_num(e.bars, vm.locale(), 0) + " " + vm.tr("menu.bars");
  tags += std::string(kSep) + vm.tr("menu.leverageCap") + " " + std::to_string(e.leverage_cap) + "x";
  return tags + std::string(kSep) + size_text(vm, e.size);
}

// The buttons of Catalog.svelte's actions row: one of play, needs-connection, pause, resume, download.
void entry_actions(const AppViewModel& vm, const packs::CatalogEntry& e, ScreenModel& m) {
  const PackScreens& s = vm.packs();
  const EntryStatus st = s.status_of(e);
  const auto d = s.downloads.find(e.digest);
  const bool downloading = d != s.downloads.end() && d->second.phase == DownloadPhase::Downloading;
  const bool can_download = s.status == CatalogStatus::Ready && vm.packs_available();
  const bool installed = st == EntryStatus::Installed;
  m.show(names::kCatalogPlay, installed);
  m.show(names::kCatalogNeedsConnection, !installed && !can_download);
  m.show(names::kCatalogPause, !installed && can_download && downloading);
  m.show(names::kCatalogDownload, !installed && can_download && !downloading);
  m.text(names::kCatalogPlay, std::string(kPlay) + vm.tr("menu.startPack"));
  m.name(names::kCatalogPlay, vm.tr("menu.startPack") + ": " + packs::localized(e.title, locale_code(vm.locale())));
  m.text(names::kCatalogNeedsConnection, vm.tr("catalog.needsConnection"));
  m.text(names::kCatalogPause, vm.tr("catalog.pause"));
  const auto partial = s.partial.find(e.digest);
  const std::int64_t received = partial == s.partial.end() ? 0 : partial->second;
  m.text(names::kCatalogDownload, st == EntryStatus::Partial
                                      ? vm.tr("catalog.resume", {{"received", size_text(vm, received)}, {"size", size_text(vm, e.size)}})
                                      : vm.tr("catalog.download", {{"size", size_text(vm, e.size)}}));
  m.enable(names::kCatalogDownload, !s.busy_with(e.digest));
  m.enable(names::kCatalogPlay, !vm.waiting());
}

void entry_detail(const AppViewModel& vm, ScreenModel& m) {
  const PackScreens& s = vm.packs();
  m.show(names::kCatalogDetail, !s.entries.empty());
  if (s.entries.empty()) return;
  const packs::CatalogEntry& e = s.entries[static_cast<std::size_t>(s.selected)];
  const std::string_view L = locale_code(vm.locale());
  const bool installed = s.status_of(e) == EntryStatus::Installed;
  m.text(names::kCatalogEntryTitle, packs::localized(e.title, L) + (installed ? "  [" + vm.tr("catalog.installed") + "]" : ""));
  m.text(names::kCatalogEntryTags, entry_tags(vm, e));
  m.text(names::kCatalogEntrySummary, packs::localized(e.summary, L));
  const auto d = s.downloads.find(e.digest);
  const bool shown = d != s.downloads.end();
  m.show(names::kCatalogDownloadState, shown);
  m.show(names::kCatalogProgress, shown && d->second.phase == DownloadPhase::Downloading);
  if (shown) {
    m.text(names::kCatalogDownloadState, download_text(vm, d->second));
    const float total = static_cast<float>(std::max<std::int64_t>(1, d->second.size));
    m.values.push_back({names::kCatalogProgress, static_cast<float>(d->second.received) / total});
  }
  entry_actions(vm, e, m);
}

void entry_list(const AppViewModel& vm, ScreenModel& m) {
  const PackScreens& s = vm.packs();
  WidgetItems items{names::kCatalogList, {}, s.selected};
  const std::string_view L = locale_code(vm.locale());
  for (const auto& e : s.entries) {
    const EntryStatus st = s.status_of(e);
    const std::string mark = st == EntryStatus::Installed ? "  \xE2\x9C\x93" : st == EntryStatus::Partial ? "  \xE2\x80\xA6" : "";
    items.items.push_back(packs::localized(e.title, L) + mark);
  }
  m.items.push_back(std::move(items));
  m.name(names::kCatalogList, vm.tr("catalog.title"));
}

// The installed packs (catalog downloads and CSV imports), with a missing file marked.
void library(const AppViewModel& vm, ScreenModel& m) {
  const PackScreens& s = vm.packs();
  m.text(names::kLibraryTitle, vm.tr("menu.practice"));
  m.text(names::kLibraryNote, vm.tr("menu.practiceNote"));
  m.text(names::kLibraryEmpty, vm.tr("menu.noPacks"));
  m.show(names::kLibraryEmpty, s.installed.empty());
  m.show(names::kLibraryList, !s.installed.empty());
  WidgetItems items{names::kLibraryList, {}, s.library_selected};
  for (const auto& p : s.installed) {
    const std::string tag = vm.tr(p.mode && *p.mode == "synthetic" ? "top.synthetic" : "top.known");
    items.items.push_back(p.label + "  [" + tag + "]" + (s.is_missing(p.digest) ? "  [" + vm.tr("pack.evictedTag") + "]" : ""));
  }
  m.items.push_back(std::move(items));
  m.name(names::kLibraryList, vm.tr("menu.practice"));
  const storage::StoredPack* selected =
      s.installed.empty() ? nullptr : &s.installed[static_cast<std::size_t>(s.library_selected)];
  const bool missing = selected != nullptr && s.is_missing(selected->digest);
  m.text(names::kLibraryMissing, vm.tr("desktop.pack.missingFile"));
  m.show(names::kLibraryMissing, missing);
  m.text(names::kLibraryPlay, std::string(kPlay) + vm.tr("menu.startPack"));
  m.text(names::kLibraryRemove, vm.tr("menu.deletePack"));
  m.show(names::kLibraryPlay, selected != nullptr);
  m.show(names::kLibraryRemove, selected != nullptr);
  m.enable(names::kLibraryPlay, selected != nullptr && !missing && !vm.waiting());
  m.enable(names::kLibraryRemove, !vm.waiting());
  if (selected != nullptr) {
    m.name(names::kLibraryPlay, vm.tr("menu.startPack") + ": " + selected->label);
    m.name(names::kLibraryRemove, vm.tr("menu.deletePack") + ": " + selected->label);
  }
}

}  // namespace

ScreenModel catalog(const AppViewModel& vm) {
  ScreenModel m;
  m.text(names::kCatalogTitle, vm.tr("catalog.title"));
  m.text(names::kCatalogBack, vm.tr("debrief.menu"));
  m.text(names::kCatalogNote, vm.tr("catalog.note"));
  status_lines(vm, m);
  entry_list(vm, m);
  entry_detail(vm, m);
  library(vm, m);
  return m;
}

}  // namespace tradelab::desktop::screens
