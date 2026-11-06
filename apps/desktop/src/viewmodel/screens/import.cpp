// CSV import wizard (web CsvImport.svelte): file -> declared metadata -> the engine's report (errors,
// gaps with the gap-fill choice) -> a private practice pack. Every rule is the engine's.
#include <iterator>

#include "viewmodel/format.hpp"
#include "viewmodel/screens/screens.hpp"

namespace tradelab::desktop::screens {

namespace {

using Step = PackScreens::Step;

// i18n tCount: `key` with {n} in the locale, or its `…One` form when n is 1.
std::string count_text(const AppViewModel& vm, const std::string& key, long n, Params params = {}) {
  params.emplace_back("n", fmt::fmt_num(static_cast<double>(n), vm.locale(), 0));
  const std::string one = key + "One";
  return vm.tr(n == 1 && vm.catalog().has(one) ? one : key, params);
}

// i18n csvIssueText: "code" or "code:arg".
std::string issue_text(const AppViewModel& vm, const std::string& code) {
  const auto colon = code.find(':');
  const std::string key = "csvIssue." + code.substr(0, colon);
  if (!vm.catalog().has(key)) return code;
  return vm.tr(key, {{"arg", colon == std::string::npos ? std::string() : code.substr(colon + 1)}});
}

void valid_lines(const AppViewModel& vm, const json::Value& r, WidgetLines& out) {
  out.lines.push_back({"\xE2\x9C\x94 " + vm.tr("csv.valid"), Tone::Up});
  const std::string period = vm.tr("csv.period", {{"from", iso_utc(r["firstTime"].as_int())}, {"to", iso_utc(r["lastTime"].as_int())}});
  out.lines.push_back({count_text(vm, "csv.rows", static_cast<long>(r["rows"].as_int())) + std::string(kSep) + period +
                       std::string(kSep) + vm.tr(r["hasFunding"].as_bool(false) ? "csv.withFunding" : "csv.noFunding")});
}

void error_lines(const AppViewModel& vm, const json::Value& r, WidgetLines& out) {
  const long count = static_cast<long>(r["errorCount"].as_int());
  out.lines.push_back({"\xE2\x9C\x96 " + count_text(vm, "csv.errors", count), Tone::Down});
  for (const json::Value& e : r["errors"].items()) {
    const long line = static_cast<long>(e["line"].as_int());
    const std::string where = line > 0 ? vm.tr("csv.line", {{"line", std::to_string(line)}}) + ": " : std::string();
    out.lines.push_back({"\xE2\x80\xA2 " + where + issue_text(vm, e["code"].as_string())});
  }
  const long listed = static_cast<long>(r["errors"].size());
  if (count > listed) out.lines.push_back({count_text(vm, "csv.more", count - listed), Tone::Muted});
}

void gap_lines(const AppViewModel& vm, const json::Value& r, WidgetLines& out) {
  const long gaps = static_cast<long>(r["gapCount"].as_int());
  if (gaps <= 0) return;
  out.lines.push_back({"\xE2\x9A\xA0 " + count_text(vm, "csv.gaps", gaps), Tone::Attention});
  for (const json::Value& g : r["gaps"].items()) {
    out.lines.push_back({"\xE2\x80\xA2 " + count_text(vm, "csv.gapLine", static_cast<long>(g["missing"].as_int()),
                                                      {{"line", std::to_string(g["line"].as_int())}})});
  }
}

void report(const AppViewModel& vm, ScreenModel& m) {
  const auto& r = vm.packs().report;
  m.show(names::kCsvReport, r.has_value());
  m.show(names::kCsvGapRow, false);
  if (!r) return;
  WidgetLines out{names::kCsvReport, {}};
  const bool ok = (*r)["ok"].as_bool(false);
  if (ok) valid_lines(vm, *r, out); else error_lines(vm, *r, out);
  gap_lines(vm, *r, out);
  m.lines.push_back(std::move(out));
  if (!ok || (*r)["gapCount"].as_int() <= 0) return;
  m.show(names::kCsvGapRow, true);
  m.text(names::kCsvGapLabel, vm.tr("csv.gapFill"));
  m.text(names::kCsvGapHint, vm.tr("csv.gapFillHint"));
  WidgetItems fills{names::kCsvGapFill, {}, 0};
  for (std::size_t i = 0; i < std::size(kGapFills); ++i) {
    fills.items.push_back(vm.tr("csv.gapFill." + std::string(kGapFills[i])));
    if (kGapFills[i] == vm.packs().meta.gap_fill) fills.selected = static_cast<int>(i);
  }
  m.items.push_back(std::move(fills));
  m.name(names::kCsvGapFill, vm.tr("csv.gapFill"));
}

WidgetItems choice(std::string_view widget, std::vector<std::string> items, int selected) {
  return WidgetItems{widget, std::move(items), selected};
}

void meta_fields(const AppViewModel& vm, ScreenModel& m) {
  const PackScreens& s = vm.packs();
  const CsvMeta& meta = s.meta;
  m.show(names::kCsvMeta, s.step != Step::File);
  const std::pair<std::string_view, const char*> labels[] = {
      {names::kCsvAssetLabel, "csv.asset"},       {names::kCsvPairLabel, "csv.pair"},     {names::kCsvVenueLabel, "csv.venue"},
      {names::kCsvIntervalLabel, "csv.interval"}, {names::kCsvUnitLabel, "csv.tsUnit"},   {names::kCsvVolumeLabel, "csv.volumeUnit"},
      {names::kCsvLeverageLabel, "csv.leverage"}};
  for (const auto& [w, key] : labels) m.text(w, vm.tr(key));
  const std::string suggested = default_label(meta, s.file_name);
  m.text(names::kCsvNameLabel, vm.tr("csv.label") + " (" + suggested + ")");
  m.text(names::kCsvAsset, meta.asset);
  m.text(names::kCsvPair, meta.pair);
  m.text(names::kCsvVenue, meta.venue);
  m.text(names::kCsvLeverage, std::to_string(meta.leverage_cap));
  m.text(names::kCsvName, s.label);
  std::vector<std::string> intervals;
  int interval = 3;
  for (std::size_t i = 0; i < std::size(kIntervals); ++i) {
    intervals.emplace_back(kIntervals[i].label);
    if (kIntervals[i].seconds == meta.interval_seconds) interval = static_cast<int>(i);
  }
  m.items.push_back(choice(names::kCsvInterval, intervals, interval));
  m.items.push_back(choice(names::kCsvUnit, {vm.tr("csv.seconds"), vm.tr("csv.millis")}, meta.timestamp_unit == "ms" ? 1 : 0));
  m.items.push_back(choice(names::kCsvVolume, {vm.tr("csv.base"), vm.tr("csv.quote")}, meta.volume_unit == "quote" ? 1 : 0));
  const bool editable = !s.busy && s.step == Step::Meta;
  for (auto w : {names::kCsvAsset, names::kCsvPair, names::kCsvVenue, names::kCsvInterval, names::kCsvUnit,
                 names::kCsvVolume, names::kCsvLeverage, names::kCsvName}) {
    m.enable(w, editable);
  }
  for (const auto& [w, key] : {std::pair{names::kCsvAsset, "csv.asset"}, std::pair{names::kCsvPair, "csv.pair"},
                               std::pair{names::kCsvVenue, "csv.venue"}, std::pair{names::kCsvInterval, "csv.interval"},
                               std::pair{names::kCsvUnit, "csv.tsUnit"}, std::pair{names::kCsvVolume, "csv.volumeUnit"},
                               std::pair{names::kCsvLeverage, "csv.leverage"}, std::pair{names::kCsvName, "csv.label"}}) {
    m.name(w, vm.tr(key));
  }
}

void actions(const AppViewModel& vm, ScreenModel& m) {
  const PackScreens& s = vm.packs();
  const bool ok = s.report && (*s.report)["ok"].as_bool(false);
  m.text(names::kCsvCancel, vm.tr("csv.cancel"));
  m.text(names::kCsvBack, vm.tr("csv.back"));
  m.text(names::kCsvValidate, vm.tr("csv.validate"));
  m.text(names::kCsvCreate, "\xE2\x96\xB6 " + vm.tr("csv.create"));
  m.show(names::kCsvBack, s.step == Step::Report);
  m.show(names::kCsvValidate, s.step == Step::Meta);
  m.show(names::kCsvCreate, s.step == Step::Report && ok);
  for (auto w : {names::kCsvValidate, names::kCsvCreate, names::kCsvOpen}) m.enable(w, !s.busy && vm.packs_available());
  m.text(names::kCsvError, s.error);
  m.show(names::kCsvError, !s.error.empty());
  m.text(names::kCsvWorking, vm.tr("csv.working"));
  m.show(names::kCsvWorking, s.busy);
}

}  // namespace

ScreenModel csv_import(const AppViewModel& vm) {
  ScreenModel m;
  const PackScreens& s = vm.packs();
  m.text(names::kCsvTitle, vm.tr("csv.title"));
  m.text(names::kCsvIntro, vm.tr("csv.intro"));
  m.text(names::kCsvPrivacy, vm.tr("csv.privacy"));
  m.text(names::kCsvFileLabel, vm.tr("csv.file") + std::string(kSep) + vm.tr("desktop.csv.path"));
  m.text(names::kCsvPath, s.path);
  m.name(names::kCsvPath, vm.tr("desktop.csv.path"));
  m.text(names::kCsvOpen, vm.tr("desktop.csv.open"));
  m.text(names::kCsvFileName, s.file_name);
  m.show(names::kCsvFileName, !s.file_name.empty());
  m.enable(names::kCsvPath, !s.busy && s.step != Step::Report);
  meta_fields(vm, m);
  report(vm, m);
  actions(vm, m);
  return m;
}

}  // namespace tradelab::desktop::screens
