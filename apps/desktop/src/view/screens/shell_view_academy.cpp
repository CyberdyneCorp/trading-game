// ShellView glue of the Academy (lesson widgets and questions, the Academy and glossary screens, the
// review prompt, the debrief's recommended lesson and coach step), the payoff painter, and the
// in-place definitions: a marked term's tooltip and accessible description, and the help line that
// shows the focused term's definition (keyboard users get on focus what the mouse gets on hover).
#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_draw.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <array>

#include "view/raven/raven_ui.hpp"
#include "view/screens/shell_view.hpp"
#include "viewmodel/academy/payoff.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/format.hpp"

namespace tradelab::desktop {

namespace {

AppViewModel& vm_of(void* user_data) { return *static_cast<AppViewModel*>(user_data); }

std::string text_or_empty(const char* text) { return text != nullptr ? text : ""; }

// terminal-ui tokens (apps/web/src/app.css): --accent, --info, --line, --muted.
constexpr std::array<float, 3> kAccent{0x39 / 255.0f, 0xFF / 255.0f, 0x88 / 255.0f};
constexpr std::array<float, 3> kInfo{0x4F / 255.0f, 0xD8 / 255.0f, 0xFF / 255.0f};
constexpr std::array<float, 3> kLine{0x24 / 255.0f, 0x41 / 255.0f, 0x3A / 255.0f};
constexpr std::array<float, 3> kMuted{0x8F / 255.0f, 0xB3 / 255.0f, 0xA1 / 255.0f};

void line(RPainter* p, float x0, float y0, float x1, float y1, const std::array<float, 3>& c, float width = 1.0f) {
  Rdraw_line(p, x0, y0, x1, y1, width, c[0], c[1], c[2], 1.0f);
}

// LessonPayoff.svelte's diagram, from academy::payoff_geometry (no math here).
void paint_payoff(Rid /*w*/, RPainter* p, float width, float height, void* user_data) {
  const LessonStep* step = vm_of(user_data).shell().lesson.step();
  const auto* w = step != nullptr ? std::get_if<PayoffWidget>(&step->widget) : nullptr;
  if (w == nullptr) return;
  const academy::PayoffGeometry g = academy::payoff_geometry(*w, width, height);
  if (g.curve.empty()) return;
  line(p, g.left, g.zero_y, g.right, g.zero_y, kLine);
  const Locale L = vm_of(user_data).locale();
  for (const auto& [x, strike] : g.strikes) {
    for (float y = g.top; y < g.bottom; y += 6) line(p, x, y, x, std::min(g.bottom, y + 3), kLine);
    const std::string label = fmt::fmt_num(strike, L, strike >= 1000 ? 0 : 2);
    Rdraw_text(p, R_INVALID_ID, 11, x - 12, g.bottom + 6, label.c_str(), kMuted[0], kMuted[1], kMuted[2], 1.0f);
  }
  for (std::size_t i = 1; i < g.curve.size(); ++i) line(p, g.curve[i - 1].x, g.curve[i - 1].y, g.curve[i].x, g.curve[i].y, kAccent, 2.0f);
  for (const auto& b : g.breakevens) Rdraw_rect(p, b.x - 3.5f, b.y - 3.5f, 7, 7, 3.5f, kInfo[0], kInfo[1], kInfo[2], 1.0f);
}

struct SandboxSlot {
  AppViewModel* vm;
  AppViewModel::SandboxField field;
};

void on_sandbox(Rid, const char* text, void* user_data) {
  const auto* s = static_cast<const SandboxSlot*>(user_data);
  s->vm->sandbox_set_text(s->field, text_or_empty(text));
}

void on_typed(Rid, const char* text, void* user_data) { vm_of(user_data).lesson_set_typed(text_or_empty(text)); }
void on_typed_commit(Rid, const char*, void* user_data) { vm_of(user_data).lesson_submit_number(); }
void on_glossary_search(Rid, const char* text, void* user_data) { vm_of(user_data).glossary_search(text_or_empty(text)); }
void on_glossary_selected(Rid, int index, void* user_data) { vm_of(user_data).glossary_select(index); }
void on_academy_selected(Rid, int index, void* user_data) { vm_of(user_data).academy_select(index); }
void on_due_selected(Rid, int index, void* user_data) { vm_of(user_data).academy_due_select(index); }

void on_academy_activated(Rid, int index, void* user_data) {
  vm_of(user_data).academy_select(index);
  vm_of(user_data).academy_start_selected();
}

void on_due_activated(Rid, int index, void* user_data) {
  vm_of(user_data).academy_due_select(index);
  vm_of(user_data).academy_review_selected();
}

// The help line of each screen that shows marked terms.
std::string_view help_line_of(Screen s) {
  switch (s) {
    case Screen::Lesson: return names::kLessonTermHelp;
    case Screen::Debrief: return names::kDebriefTermHelp;
    case Screen::Game: return names::kGameTermHelp;
    default: return {};
  }
}

}  // namespace

void ShellView::bind_academy() {
  AppViewModel& vm = vm_;
  ui::bind_action(names::kActLessonCheck, [&vm] { vm.lesson_submit_number(); });
  ui::bind_action(names::kActLessonSetCheck, [&vm] { vm.lesson_check_set(); });
  ui::bind_action(names::kActSandboxBuy, [&vm] { vm.sandbox_set_side(true); });
  ui::bind_action(names::kActSandboxSell, [&vm] { vm.sandbox_set_side(false); });
  for (std::size_t k = 0; k < names::kActLessonTerms.size(); ++k) {
    ui::bind_action(names::kActLessonTerms[k], [&vm, k] {
      const LessonStep* s = vm.shell().lesson.step();
      if (s != nullptr && k < s->terms.size()) vm.open_glossary(s->terms[k]);
    });
  }
  ui::bind_action(names::kActOpenAcademy, [&vm] { vm.open_academy(); });
  ui::bind_action(names::kActOpenGlossary, [&vm] { vm.open_glossary(); });
  ui::bind_action(names::kActAcademyStart, [&vm] { vm.academy_start_selected(); });
  ui::bind_action(names::kActAcademyReview, [&vm] { vm.academy_review_selected(); });
  ui::bind_action(names::kActGlossaryBack, [&vm] { vm.leave_glossary(); });
  for (std::size_t k = 0; k < names::kActGlossaryRelated.size(); ++k) {
    ui::bind_action(names::kActGlossaryRelated[k], [&vm, k] {
      const GlossaryEntry* g = web_content().term(vm.shell().academy.glossary_term);
      if (g != nullptr && k < g->related.size()) vm.glossary_open(g->related[k]);
    });
  }
  ui::bind_action(names::kActReviewDismiss, [&vm] { vm.dismiss_review_prompt(); });
  ui::bind_action(names::kActDebriefLesson, [&vm] { vm.open_debrief_lesson(); });
  ui::bind_action(names::kActBriefTour, [&vm] { vm.coach_replay(); });
  ui::bind_action(names::kActHelpTour, [&vm] { vm.coach_replay(); });
  ui::bind_action(names::kActCoachNext, [&vm] { vm.coach_next(); });
  ui::bind_action(names::kActCoachSkip, [&vm] { vm.coach_skip(); });
  bind_academy_callbacks();
}

void ShellView::bind_academy_callbacks() {
  using F = AppViewModel::SandboxField;
  static std::array<SandboxSlot, 4> slots;
  slots = {SandboxSlot{&vm_, F::Qty}, SandboxSlot{&vm_, F::Leverage}, SandboxSlot{&vm_, F::Stop}, SandboxSlot{&vm_, F::Target}};
  const std::array<std::string_view, 4> inputs = {names::kSandboxQty, names::kSandboxLeverage, names::kSandboxStop, names::kSandboxTarget};
  for (std::size_t i = 0; i < inputs.size(); ++i) Rtext_input_set_changed_callback(find(inputs[i]), on_sandbox, &slots[i]);
  Rtext_input_set_changed_callback(find(names::kLessonNumber), on_typed, &vm_);
  Rtext_input_set_committed_callback(find(names::kLessonNumber), on_typed_commit, &vm_);
  Rtext_input_set_changed_callback(find(names::kGlossarySearch), on_glossary_search, &vm_);
  Rlist_view_set_selection_changed_callback(find(names::kGlossaryList), on_glossary_selected, &vm_);
  Rlist_view_set_activated_callback(find(names::kGlossaryList), on_glossary_selected, &vm_);
  Rlist_view_set_selection_changed_callback(find(names::kAcademyLessons), on_academy_selected, &vm_);
  Rlist_view_set_activated_callback(find(names::kAcademyLessons), on_academy_activated, &vm_);
  Rlist_view_set_selection_changed_callback(find(names::kAcademyDue), on_due_selected, &vm_);
  Rlist_view_set_activated_callback(find(names::kAcademyDue), on_due_activated, &vm_);
  Rcustom_set_paint_callback(find(names::kLessonPayoff), paint_payoff, &vm_);
}

// A marked term: its definition as tooltip (hover) and accessible description (read after the name).
void ShellView::apply_tips(const ScreenModel& m) {
  for (const auto& t : m.tips) {
    const Rid w = find(t.widget);
    if (w == R_INVALID_ID) continue;
    Rwidget_set_tooltip(w, t.text.empty() ? nullptr : t.text.c_str());
    Rwidget_set_accessible_description(w, t.text.c_str());
  }
  for (const auto& d : m.descriptions) Rwidget_set_accessible_description(find(d.widget), d.text.c_str());
}

// The focused control's definition on the screen's help line (Term.svelte shows it on focus).
void ShellView::focus_help(Rid window) {
  const std::string_view help = help_line_of(vm_.screen());
  if (help.empty()) return;
  const Rid focused = Rwindow_get_focused_widget(window);
  const char* tip = focused != R_INVALID_ID ? Rwidget_get_tooltip(focused) : "";
  const std::string text = tip != nullptr ? tip : "";
  const Rid label = find(help);
  if (label == R_INVALID_ID) return;
  const RVisibility want = text.empty() ? R_COLLAPSED : R_VISIBLE;
  if (ui::text_of(label) != text) ui::set_text(label, text);
  if (Rwidget_get_visibility(label) != want) Rwidget_set_visibility(label, want);
}

}  // namespace tradelab::desktop
