#include "view/screens/shell_view.hpp"

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_draw.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <array>
#include <optional>

#include "view/chart_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/keymap.hpp"
#include "viewmodel/report_texts.hpp"
#include "viewmodel/screen_names.hpp"

namespace tradelab::desktop {

namespace {

AppViewModel& vm_of(void* user_data) { return *static_cast<AppViewModel*>(user_data); }

// ---- Painters: the lesson's illustrative chart and the debrief's regime band ----

constexpr std::array<float, 3> kHatch{0x24 / 255.0f, 0x41 / 255.0f, 0x3A / 255.0f};
constexpr std::array<float, 3> kAttention{0xFF / 255.0f, 0xC8 / 255.0f, 0x57 / 255.0f};

void paint_lesson_chart(Rid /*w*/, RPainter* p, float width, float height, void* user_data) {
  const LessonStep* step = vm_of(user_data).shell().lesson.step();
  if (step == nullptr || !step->chart) return;
  const LessonChart& c = *step->chart;
  const int total = static_cast<int>(c.candles.size());
  const int known = c.known > 0 ? std::min(c.known, total) : total;
  std::vector<Bar> bars;
  for (int i = 0; i < known; ++i) {
    const LessonCandle& k = c.candles[static_cast<std::size_t>(i)];
    bars.push_back(Bar{k.o, k.h, k.l, k.c, k.v});
  }
  // Scaled from the closed bars only; the unknown future is hatched and nothing is drawn in it.
  const float known_width = width * static_cast<float>(known) / static_cast<float>(std::max(1, total));
  const ChartGeometry g = chart_geometry(bars, known_width, height, std::max(1, known));
  paint_chart(p, g, known_width, height);
  for (float x = known_width; x < width; x += 8.0f) {
    Rdraw_line(p, x, height, std::min(width, x + height), 0, 1.0f, kHatch[0], kHatch[1], kHatch[2], 0.8f);
  }
  if (c.highlight > 0 && c.highlight <= static_cast<int>(g.candles.size())) {
    const Candle& h = g.candles[static_cast<std::size_t>(c.highlight - 1)];
    Rdraw_rect_outline(p, h.x - 3, 1, h.width + 6, height - 2, 0, 1.0f, kAttention[0], kAttention[1], kAttention[2], 1.0f);
  }
}

std::array<float, 3> regime_colour(const std::string& label) {
  if (label == "trend") return {0x4F / 255.0f, 0xD8 / 255.0f, 0xFF / 255.0f};
  if (label == "high_vol" || label == "shock") return {0xFF / 255.0f, 0x5F / 255.0f, 0x7A / 255.0f};
  if (label == "crowded" || label == "low_liquidity") return kAttention;
  return {0x8F / 255.0f, 0xB3 / 255.0f, 0xA1 / 255.0f};
}

void paint_regime_band(Rid /*w*/, RPainter* p, float width, float height, void* user_data) {
  const AppViewModel& vm = vm_of(user_data);
  const Snapshot* s = vm.snapshot();
  if (s == nullptr || !s->debrief) return;
  for (const auto& piece : regime_band(s->debrief->reveal["spans"], s->warmup_bars, s->step)) {
    const auto col = regime_colour(piece.label);
    const float x = static_cast<float>(piece.left) * width;
    const float w = static_cast<float>(piece.width) * width;
    Rdraw_rect(p, x, 0, w, height, 0, col[0], col[1], col[2], 0.35f);
    if (piece.width > 0.08) {
      const std::string name = regime_name(vm.catalog(), piece.label, piece.direction);
      Rdraw_text(p, R_INVALID_ID, 0, x + 4, 4, name.c_str(), 0xE3 / 255.0f, 0xF5 / 255.0f, 0xEA / 255.0f, 1.0f);
    }
  }
}

// ---- Tables ----

// A "#" column keeps a narrow fixed width; the others share the rest.
void set_columns(Rid table, const std::vector<std::string>& columns) {
  Rtable_clear_columns(table);
  for (const auto& c : columns) Rtable_add_column(table, c.c_str(), 0);
  const bool index = !columns.empty() && columns[0] == "#";
  for (int c = 0; c < static_cast<int>(columns.size()); ++c) Rtable_set_column_fill(table, c, index && c == 0 ? 0.0f : 1.0f);
  if (index) Rtable_set_column_width(table, 0, 44);
}

// Cells may hold '|' (Rtable_add_row's separator), so each is set on its own. The selection is kept.
void set_rows(Rid table, const std::vector<std::vector<std::string>>& rows) {
  const int selected = Rtable_get_selected(table);
  Rtable_clear_rows(table);
  for (const auto& row : rows) {
    const int r = Rtable_add_row(table, nullptr);
    for (int c = 0; c < static_cast<int>(row.size()); ++c) Rtable_set_cell(table, r, c, row[static_cast<std::size_t>(c)].c_str());
  }
  if (selected >= 0 && selected < static_cast<int>(rows.size())) Rtable_set_selected(table, selected);
}

// ---- Widget callbacks ----

void on_lesson_selected(Rid, int index, void* user_data) { vm_of(user_data).select_lesson(index); }

void on_lesson_activated(Rid, int index, void* user_data) {
  vm_of(user_data).select_lesson(index);
  vm_of(user_data).start_selected_lesson();
}

void on_journal_filter(Rid, int index, void* user_data) { vm_of(user_data).set_journal_filter(index); }

void on_key_row(Rid, int row, void* user_data) { vm_of(user_data).select_key_row(row); }

void on_text_scale(Rid, float value, void* user_data) { vm_of(user_data).set_text_scale(value); }

void on_palette(Rid, int checked, void* user_data) { vm_of(user_data).set_colorblind(checked != 0); }

// One callback for the four CRT switches: the widget says which one changed.
struct CrtSwitch {
  AppViewModel* vm;
  bool CrtSettings::*field;
};

void on_crt(Rid, int checked, void* user_data) {
  const auto* sw = static_cast<const CrtSwitch*>(user_data);
  CrtSettings crt = sw->vm->settings().crt;
  crt.*(sw->field) = checked != 0;
  sw->vm->set_crt(crt);
}

// Key names as the web's keyName() stores them: named keys here, printable ones from R_EVENT_TEXT.
std::optional<std::string> named_key(RKey key) {
  switch (key) {
    case R_KEY_SPACE: return "Space";
    case R_KEY_ESCAPE: return "Escape";
    case R_KEY_ENTER: case R_KEY_KP_ENTER: return "Enter";
    case R_KEY_TAB: return "Tab";
    case R_KEY_LEFT: return "ArrowLeft";
    case R_KEY_RIGHT: return "ArrowRight";
    case R_KEY_UP: return "ArrowUp";
    case R_KEY_DOWN: return "ArrowDown";
    case R_KEY_HOME: return "Home";
    case R_KEY_END: return "End";
    case R_KEY_PAGE_UP: return "PageUp";
    case R_KEY_PAGE_DOWN: return "PageDown";
    default: return std::nullopt;
  }
}

}  // namespace

bool ShellView::capture_key(AppViewModel& vm, const REvent* e) {
  if (!vm.shell().capturing_key) return false;
  const bool shift = (e->modifiers & R_MOD_SHIFT) != 0;
  if (e->type == R_EVENT_KEY_DOWN) {
    if (const auto name = named_key(e->key)) return vm.capture_key(key_name(*name == "Space" ? " " : *name, shift));
    return true;  // a printable key: its character arrives as R_EVENT_TEXT
  }
  if (e->type == R_EVENT_TEXT && e->codepoint > 32 && e->codepoint < 127) {
    return vm.capture_key(key_name(std::string(1, static_cast<char>(e->codepoint)), shift));
  }
  return e->type == R_EVENT_KEY_UP;
}

Rid ShellView::find(std::string_view name) const { return ui::find(scope_, name); }

void ShellView::bind() {
  AppViewModel& vm = vm_;
  ui::bind_action(names::kActResume, [&vm] { vm.resume(); });
  ui::bind_action(names::kActOpenCurriculum, [&vm] { vm.open_curriculum(); });
  ui::bind_action(names::kActOpenJournal, [&vm] { vm.open_journal(); });
  ui::bind_action(names::kActOpenSettings, [&vm] { vm.open_settings(); });
  ui::bind_action(names::kActCampaignStart, [&vm] { vm.campaign_start(); });
  ui::bind_action(names::kActCampaignContinue, [&vm] { vm.campaign_continue(); });
  ui::bind_action(names::kActCampaignEnd, [&vm] { vm.campaign_end_ask(); });
  ui::bind_action(names::kActCampaignEndYes, [&vm] { vm.campaign_end_answer(true); });
  ui::bind_action(names::kActCampaignEndNo, [&vm] { vm.campaign_end_answer(false); });
  ui::bind_action(names::kActCampaignNext, [&vm] { vm.campaign_next(); });
  ui::bind_action(names::kActBackToMenu, [&vm] { vm.back_to_menu(); });
  ui::bind_action(names::kActLessonStart, [&vm] { vm.start_selected_lesson(); });
  ui::bind_action(names::kActLessonLeave, [&vm] { vm.lesson_leave(); });
  ui::bind_action(names::kActLessonPrev, [&vm] { vm.lesson_prev(); });
  ui::bind_action(names::kActLessonNext, [&vm] { vm.lesson_next(); });
  ui::bind_action(names::kActLessonFinish, [&vm] { vm.lesson_finish(); });
  for (int k = 0; k < static_cast<int>(names::kActLessonChoices.size()); ++k) {
    ui::bind_action(names::kActLessonChoices[static_cast<std::size_t>(k)], [&vm, k] { vm.lesson_pick(k); });
  }
  ui::bind_action(names::kActLangPt, [&vm] { vm.set_locale(Locale::PtBR); });
  ui::bind_action(names::kActLangEn, [&vm] { vm.set_locale(Locale::En); });
  ui::bind_action(names::kActKeyChange, [&vm] { vm.begin_key_change(); });
  ui::bind_action(names::kActKeyReset, [&vm] { vm.reset_keys(); });
  bind_callbacks();
}

void ShellView::bind_callbacks() {
  static std::array<CrtSwitch, 4> switches;
  switches = {CrtSwitch{&vm_, &CrtSettings::enabled}, CrtSwitch{&vm_, &CrtSettings::scanlines},
              CrtSwitch{&vm_, &CrtSettings::bloom}, CrtSwitch{&vm_, &CrtSettings::noise}};
  const std::array<std::string_view, 4> boxes = {names::kSettingsCrt, names::kSettingsScanlines, names::kSettingsBloom,
                                                 names::kSettingsNoise};
  for (std::size_t i = 0; i < boxes.size(); ++i) Rcheckbox_set_changed_callback(find(boxes[i]), on_crt, &switches[i]);
  Rcheckbox_set_changed_callback(find(names::kSettingsPalette), on_palette, &vm_);
  Rslider_set_changed_callback(find(names::kSettingsTextScale), on_text_scale, &vm_);
  Rtable_set_selection_changed_callback(find(names::kSettingsKeys), on_key_row, &vm_);
  Rcombo_box_set_changed_callback(find(names::kJournalFilter), on_journal_filter, &vm_);
  Rlist_view_set_selection_changed_callback(find(names::kCurriculumLessons), on_lesson_selected, &vm_);
  Rlist_view_set_activated_callback(find(names::kCurriculumLessons), on_lesson_activated, &vm_);
  Rcustom_set_paint_callback(find(names::kLessonChart), paint_lesson_chart, &vm_);
  Rcustom_set_paint_callback(find(names::kDebriefRegimeBand), paint_regime_band, &vm_);
}

void ShellView::apply(const ScreenModel& m) {
  for (const auto& t : m.texts) {
    const Rid w = find(t.widget);
    if (w != R_INVALID_ID) ui::set_text(w, t.text);
  }
  for (const auto& n : m.names) Rwidget_set_accessible_name(find(n.widget), n.text.c_str());
  for (const auto& l : m.lines) apply_lines(l);
  for (const auto& t : m.tables) apply_table(t);
  for (const auto& it : m.items) apply_items(it);
  apply_flags(m);
}

// Widget callbacks fired by these setters see the viewmodel's own values: no-ops there.
void ShellView::apply_flags(const ScreenModel& m) {
  for (const auto& f : m.visible) Rwidget_set_visibility(find(f.widget), f.on ? R_VISIBLE : R_COLLAPSED);
  for (const auto& f : m.enabled) Rwidget_set_enabled(find(f.widget), f.on ? R_TRUE : R_FALSE);
  for (const auto& f : m.checked) {
    const Rid w = find(f.widget);
    if ((Rcheckbox_is_checked(w) != 0) != f.on) Rcheckbox_set_checked(w, f.on ? R_TRUE : R_FALSE);
  }
  const Rid slider = find(names::kSettingsTextScale);
  const float scale = vm_.settings().text_scale;
  if (slider != R_INVALID_ID && Rslider_get_value(slider) != scale) Rslider_set_value(slider, scale);
}

void ShellView::apply_lines(const WidgetLines& l) {
  auto& shown = lines_[std::string(l.widget)];
  const bool same = shown.size() == l.lines.size() &&
                    std::equal(shown.begin(), shown.end(), l.lines.begin(),
                               [](const Line& a, const Line& b) { return a.text == b.text && a.tone == b.tone; });
  if (same) return;
  const Rid box = find(l.widget);
  if (box == R_INVALID_ID) return;
  Rwidget_clear_children(box);
  for (const Line& line : l.lines) {
    const Rid label = Rlabel_create(line.text.c_str());
    Rlabel_set_wrap(label, R_TRUE);
    const std::string_view cls = tone_class(line.tone);
    if (!cls.empty()) Rwidget_set_class(label, std::string(cls).c_str());
    if (line.tone == Tone::Title) {
      Rwidget_set_accessible_role(label, R_ROLE_HEADING);
      if (look_.pixel_font() != R_INVALID_ID) Rlabel_set_font(label, look_.pixel_font());
      Rlabel_set_text_size(label, kPixelTitleSize);
    }
    Rwidget_add_child(box, label);
  }
  shown = l.lines;
}

void ShellView::apply_table(const WidgetTable& t) {
  auto it = tables_.find(t.widget);
  if (it != tables_.end() && it->second.columns == t.columns && it->second.rows == t.rows) return;
  const Rid table = find(t.widget);
  if (table == R_INVALID_ID) return;
  if (it == tables_.end() || it->second.columns != t.columns) set_columns(table, t.columns);
  set_rows(table, t.rows);
  tables_[std::string(t.widget)] = t;
}

void ShellView::apply_items(const WidgetItems& items) {
  const Rid w = find(items.widget);
  if (w == R_INVALID_ID) return;
  auto it = items_.find(items.widget);
  const bool same = it != items_.end() && it->second.items == items.items;
  const bool combo = Rwidget_get_type(w) == R_WIDGET_COMBO_BOX;
  if (!same) {
    combo ? Rcombo_box_clear_items(w) : Rlist_view_clear_items(w);
    for (const auto& text : items.items) combo ? Rcombo_box_add_item(w, text.c_str()) : Rlist_view_add_item(w, text.c_str());
  }
  const int current = combo ? Rcombo_box_get_selected(w) : Rlist_view_get_selected(w);
  if (!same || current != items.selected) {
    combo ? Rcombo_box_set_selected(w, items.selected) : Rlist_view_set_selected(w, items.selected);
  }
  items_[std::string(items.widget)] = items;
}

}  // namespace tradelab::desktop
