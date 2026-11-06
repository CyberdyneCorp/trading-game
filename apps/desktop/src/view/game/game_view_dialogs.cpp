// GameView: the dialogs over the terminal (plan, calibration, confirm move, help + remapping, journal,
// pattern card, lesson questions, tutorial cards, intel perks), one Switcher in a modal overlay.
#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_layout.h>
#include <raven/raven_widget.h>

#include <cmath>

#include "view/game/game_view.hpp"
#include "view/raven/raven_ui.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/game/game_widgets.hpp"

namespace tradelab::desktop::view {

namespace n = names::game;
using game::Modal;

namespace {

void slider_to(Rid slider, float value) {
  if (slider != R_INVALID_ID && std::fabs(Rslider_get_value(slider) - value) > 0.01f) Rslider_set_value(slider, value);
}

Rid add_label(Rid box, const std::string& text, game::Tone tone, float size = 13) {
  const Rid l = Rlabel_create(text.c_str());
  Rlabel_set_wrap(l, R_TRUE);
  Rlabel_set_text_size(l, size);
  set_color(l, tone);
  Rwidget_add_child(box, l);
  return l;
}

}  // namespace

void GameView::bind_plan_dialog() {
  const auto field = [this](std::string_view name, std::string game::PlanDialog::*member) {
    cb_.text(find(name), [this, member](const std::string& text) {
      vm().plan.*member = text;
      vm().touch();
    });
  };
  field(n::kPlanThesis, &game::PlanDialog::thesis);
  field(n::kPlanInvalidation, &game::PlanDialog::invalidation);
  field(n::kPlanTarget, &game::PlanDialog::target);
  field(n::kPlanLose, &game::PlanDialog::lose_scenario);
  cb_.commit(find(n::kPlanThesis), [this] { vm().plan_confirm(false); });
  cb_.slide(find(n::kPlanConfidence), [this](float v) {
    vm().plan.confidence = static_cast<int>(std::lround(v / 5) * 5);
    vm().touch();
  });
  cb_.click(find(n::kPlanCancel), [this] { vm().plan_cancel(); });
  cb_.click(find(n::kPlanSkip), [this] { vm().plan_confirm(true); });
  cb_.click(find(n::kPlanSave), [this] { vm().plan_confirm(false); });
}

void GameView::bind_dialogs() {
  bind_plan_dialog();
  cb_.slide(find(n::kCalPct), [this](float v) {
    vm().calibration.pct = static_cast<int>(std::lround(v / 5) * 5);
    vm().touch();
  });
  cb_.click(find(n::kCalSkip), [this] { vm().calibration_answer(true); });
  cb_.click(find(n::kCalAnswer), [this] { vm().calibration_answer(false); });
  cb_.click(find(n::kConfirmNo), [this] { vm().confirm_move(false); });
  cb_.click(find(n::kConfirmYes), [this] { vm().confirm_move(true); });
  cb_.click(find(n::kHelpReset), [this] { vm().reset_keymap(); });
  for (const auto name : {n::kHelpOk, n::kJournalOk, n::kPatternOk, n::kLessonClose, n::kPerkClose}) {
    cb_.click(find(name), [this] { vm().close_modal(); });
  }
  cb_.click(find(n::kTutorialPrev), [this] { vm().tutorial_step(-1); });
  cb_.click(find(n::kTutorialNext), [this] { vm().tutorial_step(1); });
  cb_.click(find(n::kTutorialSkip), [this] { vm().tutorial_step(1'000); });
}

void GameView::apply_modal(const game::Texts& t, const GameState& g) {
  const Modal m = vm().modal();
  set_visible(find(n::kModal), m != Modal::None);
  apply_lab_dialog(t);
  if (m == Modal::None) return;
  Rswitcher_set_index(find(n::kModalSwitch), static_cast<int>(m) - 1);
  switch (m) {
    case Modal::Plan: apply_plan(t); break;
    case Modal::Calibration:
      set_label(find(n::kCalQuestion), vm().calibration.question(t, g));
      set_label(find(n::kCalPctLabel), std::to_string(vm().calibration.pct) + "%");
      slider_to(find(n::kCalPct), static_cast<float>(vm().calibration.pct));
      break;
    case Modal::ConfirmMove: {
      const auto* move = vm().pending_move();
      if (move != nullptr) {
        set_label(find(n::kConfirmText), t.tr("chart.moveBeyond", {{"id", std::to_string(move->order_id)},
                                                                    {"price", fmt::fmt_price(move->to, t.locale())},
                                                                    {"liq", fmt::fmt_price(g.account.liquidation_text, t.locale())}}));
      }
      break;
    }
    case Modal::Help: apply_help(t); break;
    case Modal::Journal: {
      std::vector<game::Line> lines;
      const auto texts = game::journal_lines(t, vm().journal());
      for (std::size_t i = 0; i < texts.size(); ++i) {
        lines.push_back({texts[i], game::Tone::Plain});
        // A combo's entry: its state and net premium so far (RunJournal's comboNote).
        const std::string& combo = vm().journal()[i].combo;
        if (!combo.empty() && !vm().combo_note(combo).empty()) lines.push_back({"    " + vm().combo_note(combo), game::Tone::Muted});
      }
      if (lines.empty()) lines.push_back({t.tr("runJournal.empty"), game::Tone::Muted});
      journal_.set(lines);
      break;
    }
    case Modal::Pattern: {
      const auto id = vm().pattern_id();
      const game::PatternCard c = id ? game::pattern_card(t, *id) : game::PatternCard{t.tr("patterns.title"), t.tr("patterns.note"), "", ""};
      set_label(find(n::kPatternName), c.name);
      std::string body = c.what;
      if (!c.means.empty()) body += "\n\n" + t.tr("patterns.means") + ": " + c.means;
      if (!c.confirm.empty()) body += "\n\n" + t.tr("patterns.confirm") + ": " + c.confirm;
      set_label(find(n::kPatternBody), body);
      break;
    }
    case Modal::Lesson: apply_lessons(t, g); break;
    case Modal::Tutorial: apply_tutorial(t, g); break;
    case Modal::Perks: apply_perks(t); break;
    case Modal::Lab:
    case Modal::ConfirmCombo: break;
    case Modal::None: break;
  }
}

void GameView::apply_plan(const game::Texts& t) {
  game::PlanDialog& p = vm().plan;
  set_label(find(n::kPlanHeadline), p.headline(t));
  ui::set_text(find(n::kPlanThesis), p.thesis);
  ui::set_text(find(n::kPlanInvalidation), p.invalidation);
  ui::set_text(find(n::kPlanTarget), p.target);
  ui::set_text(find(n::kPlanLose), p.lose_scenario);
  set_visible(find(n::kPlanLoseBox), p.option());
  set_label(find(n::kPlanConfidenceLabel), t.tr("plan.confidence") + ": " + std::to_string(p.confidence) + "%");
  slider_to(find(n::kPlanConfidence), static_cast<float>(p.confidence));
  set_enabled(find(n::kPlanSave), p.can_save());
}

void GameView::apply_help(const game::Texts& t) {
  game::GameViewModel& m = vm();
  const Rid box = find(n::kHelpRows);
  const auto rows = game::help_rows(t, m.keymap());
  if (help_rows_.size() != rows.size() + 2) {
    clear_rows(box, help_rows_);
    for (const auto& r : rows) {
      const Rid row = Rwidget_create(R_WIDGET_HBOX);
      const Rid key = Rlabel_create("");
      Rwidget_set_size(key, 130, 0);
      set_color(key, game::Tone::Attention);
      const Rid label = Rlabel_create("");
      Rslot_set_fill(label, 1);
      const Rid change = Rbutton_create("");
      const game::Action action = r.action;
      cb_.click(change, [this, action] {
        vm().remap.start(action);
        vm().touch();
      });
      Rwidget_add_child(row, key);
      Rwidget_add_child(row, label);
      Rwidget_add_child(row, change);
      Rwidget_add_child(box, row);
      help_rows_.push_back(row);
    }
    help_rows_.push_back(add_label(box, "", game::Tone::Plain));
    help_rows_.push_back(add_label(box, "", game::Tone::Plain));
  }
  for (std::size_t i = 0; i < rows.size(); ++i) {
    ui::set_text(Rwidget_get_child(help_rows_[i], 0), rows[i].key);
    ui::set_text(Rwidget_get_child(help_rows_[i], 1), rows[i].label);
    const bool waiting = m.remap.waiting && *m.remap.waiting == rows[i].action;
    ui::set_text(Rwidget_get_child(help_rows_[i], 2), t.tr(waiting ? "keys.press" : "keys.change"));
  }
  ui::set_text(help_rows_[rows.size()], "Esc  \xE2\x80\x94  " + t.tr("help.escape"));
  ui::set_text(help_rows_[rows.size() + 1], "\xE2\x86\x90 \xE2\x86\x92 Home End  \xE2\x80\x94  " + t.tr("help.chartKeys"));
  set_label(find(n::kHelpMessage), m.remap.message);
  set_color(find(n::kHelpMessage), m.remap.message_ok ? game::Tone::Up : game::Tone::Down);
}

void GameView::apply_lessons(const game::Texts& t, const GameState& g) {
  game::GameViewModel& m = vm();
  const Rid box = find(n::kLessonRows);
  const auto questions = m.lessons.questions(t, g.lesson);
  std::string key = std::string(locale_code(t.locale())) + std::to_string(m.busy());
  for (const auto& q : questions) key += "|" + q.id + (q.answered ? "a" : "") + (q.invalid ? "i" : "");
  if (key == lesson_key_) return;
  lesson_key_ = key;
  clear_rows(box, lesson_rows_);
  if (!g.lesson.reviewed) lesson_rows_.push_back(add_label(box, t.tr("lesson.unreviewed"), game::Tone::Muted, 12));
  for (const auto& q : questions) add_lesson(box, t, q);
}

// One question: the prompt, then its options (or a number field), Answer and Skip; once answered, a mark.
void GameView::add_lesson(Rid box, const game::Texts& t, const game::LessonQuestionView& q) {
  const Rid group = Rwidget_create(R_WIDGET_VBOX);
  Rwidget_add_child(box, group);
  lesson_rows_.push_back(group);
  add_label(group, q.prompt, game::Tone::Plain);
  if (q.answered) {
    add_label(group, "\xE2\x9C\x93 " + t.tr("lesson.answered"), game::Tone::Muted, 12);
    return;
  }
  for (std::size_t c = 0; c < q.choices.size(); ++c) {
    const Rid radio = Rcheckbox_create(q.choices[c].c_str());
    Rcheckbox_set_style(radio, R_CHECKBOX_RADIO);
    const int index = q.index;
    const int option = static_cast<int>(c);
    cb_.check(radio, [this, index, option](bool on) {
      if (on) vm().lessons.choose(index, option);
    });
    Rwidget_add_child(group, radio);
  }
  if (q.choices.empty()) {
    const Rid input = Rwidget_create(R_WIDGET_TEXT_INPUT);
    Rtext_input_set_placeholder(input, q.unit.c_str());
    Rwidget_set_accessible_name(input, q.prompt.c_str());
    const int index = q.index;
    cb_.text(input, [this, index](const std::string& text) { vm().lessons.type(index, text); });
    Rwidget_add_child(group, input);
    if (q.invalid) add_label(group, t.tr("lesson.invalid"), game::Tone::Attention, 12);
  }
  const Rid row = Rwidget_create(R_WIDGET_HBOX);
  const Rid answer = Rbutton_create(t.tr("lesson.answer").c_str());
  const Rid skip = Rbutton_create(t.tr("lesson.skip").c_str());
  const int index = q.index;
  cb_.click(answer, [this, index] { vm().lesson_answer(index); });
  cb_.click(skip, [this, index] { vm().lesson_skip(index); });
  Rwidget_add_child(row, answer);
  Rwidget_add_child(row, skip);
  Rwidget_add_child(group, row);
}

void GameView::apply_tutorial(const game::Texts& t, const GameState& g) {
  const auto cards = game::tutorial_cards(t, g.mission);
  const int i = vm().tutorial_index();
  if (cards.empty() || i >= static_cast<int>(cards.size())) return;
  const auto& c = cards[static_cast<std::size_t>(i)];
  set_label(find(n::kTutorialStep), t.tr("tut.step", {{"n", std::to_string(i + 1)}, {"total", std::to_string(cards.size())}}));
  set_label(find(n::kTutorialTitle), c.title);
  set_label(find(n::kTutorialBody), c.body);
  set_enabled(find(n::kTutorialPrev), i > 0);
  const bool last = i + 1 == static_cast<int>(cards.size());
  ui::set_text(find(n::kTutorialNext), last ? "OK" : t.tr("tut.next"));
}

void GameView::apply_perks(const game::Texts& t) {
  game::GameViewModel& m = vm();
  const Rid box = find(n::kPerkRows);
  clear_rows(box, perk_rows_);
  const bool editable = m.perks_editable();
  for (const auto& row : m.perk_rows()) {
    if (!row.unlocked) {
      perk_rows_.push_back(add_label(box, row.text, game::Tone::Muted, 13));
      continue;
    }
    const Rid box_row = Rcheckbox_create(row.text.c_str());
    Rcheckbox_set_checked(box_row, row.enabled ? R_TRUE : R_FALSE);
    Rwidget_set_enabled(box_row, editable ? R_TRUE : R_FALSE);
    const game::Perk id = row.id;
    cb_.check(box_row, [this, id](bool on) { vm().set_perk(id, on); });
    Rwidget_add_child(box, box_row);
    perk_rows_.push_back(box_row);
  }
  if (!editable) perk_rows_.push_back(add_label(box, t.tr("desktop.perksFixed"), game::Tone::Muted, 12));
}

}  // namespace tradelab::desktop::view
