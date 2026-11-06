// ShellView glue of the tutorial, catalog and CSV-import screens, the assisted-practice panels (terminal
// and debrief) and the debrief's chart: actions and widget callbacks to AppViewModel commands.
#include <raven/raven_controls.h>
#include <raven/raven_draw.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <array>

#include "view/game/scene_painter.hpp"
#include "view/raven/raven_ui.hpp"
#include "view/screens/shell_view.hpp"
#include "viewmodel/screen_names.hpp"

namespace tradelab::desktop {

namespace {

AppViewModel& vm_of(void* user_data) { return *static_cast<AppViewModel*>(user_data); }

std::string text_or_empty(const char* text) { return text != nullptr ? text : ""; }

void paint_debrief_chart(Rid /*w*/, RPainter* p, float width, float height, void* user_data) {
  view::paint_scene(p, vm_of(user_data).debrief_scene(width, height));
}

void on_catalog_selected(Rid, int index, void* user_data) { vm_of(user_data).catalog_select(index); }
void on_library_selected(Rid, int index, void* user_data) { vm_of(user_data).library_select(index); }
void on_assist_turn(Rid, const char* text, void* user_data) { vm_of(user_data).set_assist_turn(text_or_empty(text)); }

// One callback per kind of wizard field: the widget's slot says which field it is.
struct FieldSlot {
  AppViewModel* vm;
  ImportField field;
};
struct ChoiceSlot {
  AppViewModel* vm;
  ImportChoice choice;
};

void on_field(Rid, const char* text, void* user_data) {
  const auto* s = static_cast<const FieldSlot*>(user_data);
  s->vm->import_set(s->field, text_or_empty(text));
}

void on_choice(Rid, int index, void* user_data) {
  const auto* s = static_cast<const ChoiceSlot*>(user_data);
  s->vm->import_choose(s->choice, index);
}

}  // namespace

bool ShellView::tutorial_key(AppViewModel& vm, Rid window, const REvent* e) {
  if (vm.screen() != Screen::Tutorial || e->type != R_EVENT_KEY_DOWN) return false;
  switch (e->key) {
    case R_KEY_RIGHT: vm.tutorial_step(1); return true;
    case R_KEY_LEFT: vm.tutorial_step(-1); return true;
    case R_KEY_ESCAPE: vm.tutorial_finish(); return true;
    case R_KEY_ENTER:
    case R_KEY_KP_ENTER: {
      const Rid focused = Rwindow_get_focused_widget(window);
      if (focused != R_INVALID_ID && Rwidget_get_type(focused) == R_WIDGET_BUTTON) return false;
      vm.tutorial_step(1);
      return true;
    }
    default: return false;
  }
}

void ShellView::bind_flows() {
  AppViewModel& vm = vm_;
  ui::bind_action(names::kActOpenCatalog, [&vm] { vm.open_catalog(); });
  ui::bind_action(names::kActOpenImport, [&vm] { vm.open_import(); });
  ui::bind_action(names::kActTutSkip, [&vm] { vm.tutorial_finish(); });
  ui::bind_action(names::kActTutPrev, [&vm] { vm.tutorial_step(-1); });
  ui::bind_action(names::kActTutNext, [&vm] { vm.tutorial_step(1); });
  ui::bind_action(names::kActBriefTutorial, [&vm] { vm.show_tutorial(); });
  ui::bind_action(names::kActCatalogDownload, [&vm] { vm.catalog_download(); });
  ui::bind_action(names::kActCatalogPause, [&vm] { vm.catalog_pause(); });
  ui::bind_action(names::kActCatalogPlay, [&vm] { vm.catalog_play(); });
  ui::bind_action(names::kActCatalogRetry, [&vm] { vm.catalog_retry(); });
  ui::bind_action(names::kActLibraryPlay, [&vm] { vm.library_play(); });
  ui::bind_action(names::kActLibraryRemove, [&vm] { vm.library_remove(); });
  ui::bind_action(names::kActCsvOpen, [&vm] { vm.import_open_file(); });
  ui::bind_action(names::kActCsvValidate, [&vm] { vm.import_validate(); });
  ui::bind_action(names::kActCsvBack, [&vm] { vm.import_back(); });
  ui::bind_action(names::kActCsvCreate, [&vm] { vm.import_create(); });
  ui::bind_action(names::kActCsvCancel, [&vm] { vm.import_cancel(); });
  ui::bind_action(names::kActAssistRewind, [&vm] { vm.rewind(); });
  ui::bind_action(names::kActAssistNewSeed, [&vm] { vm.practice_new_seed(); });
  ui::bind_action(names::kActDebriefBarsPrev, [&vm] { vm.debrief_page(-1); });
  ui::bind_action(names::kActDebriefBarsNext, [&vm] { vm.debrief_page(1); });
  bind_flow_callbacks();
}

void ShellView::bind_flow_callbacks() {
  static std::array<FieldSlot, 6> fields;
  fields = {FieldSlot{&vm_, ImportField::Path}, FieldSlot{&vm_, ImportField::Asset}, FieldSlot{&vm_, ImportField::Pair},
            FieldSlot{&vm_, ImportField::Venue}, FieldSlot{&vm_, ImportField::Leverage}, FieldSlot{&vm_, ImportField::Label}};
  const std::array<std::string_view, 6> inputs = {names::kCsvPath, names::kCsvAsset, names::kCsvPair,
                                                  names::kCsvVenue, names::kCsvLeverage, names::kCsvName};
  for (std::size_t i = 0; i < inputs.size(); ++i) Rtext_input_set_changed_callback(find(inputs[i]), on_field, &fields[i]);
  static std::array<ChoiceSlot, 4> choices;
  choices = {ChoiceSlot{&vm_, ImportChoice::Interval}, ChoiceSlot{&vm_, ImportChoice::TimestampUnit},
             ChoiceSlot{&vm_, ImportChoice::VolumeUnit}, ChoiceSlot{&vm_, ImportChoice::GapFill}};
  const std::array<std::string_view, 4> combos = {names::kCsvInterval, names::kCsvUnit, names::kCsvVolume, names::kCsvGapFill};
  for (std::size_t i = 0; i < combos.size(); ++i) Rcombo_box_set_changed_callback(find(combos[i]), on_choice, &choices[i]);
  Rlist_view_set_selection_changed_callback(find(names::kCatalogList), on_catalog_selected, &vm_);
  Rlist_view_set_selection_changed_callback(find(names::kLibraryList), on_library_selected, &vm_);
  Rtext_input_set_changed_callback(find(names::kGameAssist.turn), on_assist_turn, &vm_);
  Rtext_input_set_changed_callback(find(names::kDebriefAssist.turn), on_assist_turn, &vm_);
  Rcustom_set_paint_callback(find(names::kDebriefChart), paint_debrief_chart, &vm_);
}

void ShellView::apply_values(const ScreenModel& m) {
  for (const auto& v : m.values) {
    const Rid w = find(v.widget);
    if (w != R_INVALID_ID && Rprogress_bar_get_value(w) != v.value) Rprogress_bar_set_value(w, v.value);
  }
}

}  // namespace tradelab::desktop
