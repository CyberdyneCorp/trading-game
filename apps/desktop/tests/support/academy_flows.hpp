// Scripted Academy flows driven by injected input (clicks, keys, typed text), the desktop counterpart
// of the web's academy, glossary and guided-first-trade Playwright specs. Shared by the headless tests
// (Raven null platform) and the windowed tests (Vulkan, with captures).
#pragma once

#include <doctest/doctest.h>

#include <raven/raven_accessibility.h>
#include <raven/raven_controls.h>
#include <raven/raven_widget.h>
#include <raven/raven_window.h>

#include <functional>
#include <string>

#include "support/academy_helpers.hpp"
#include "support/driver.hpp"
#include "support/game_flows.hpp"
#include "view/app.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/curriculum.hpp"
#include "viewmodel/format.hpp"
#include "viewmodel/screen_names.hpp"

namespace tradelab::desktop::testing {

using AcademyShot = std::function<void(const std::string&)>;

inline bool visible(Driver& d, std::string_view name) { return Rwidget_get_visibility(d.widget(name)) == R_VISIBLE; }

// Activates a control: a click, or (keyboard) the focus and Enter.
inline bool press(Driver& d, std::string_view name, bool keyboard) {
  if (!keyboard) return d.click(name);
  if (!visible(d, name) || Rwidget_is_enabled(d.widget(name)) == 0) return false;
  d.focus(name);
  d.key(R_KEY_ENTER);
  d.frames(2);
  return true;
}

inline std::string_view sandbox_input(const std::string& field) {
  if (field == "qty") return names::kSandboxQty;
  if (field == "leverage") return names::kSandboxLeverage;
  if (field == "stop") return names::kSandboxStop;
  return names::kSandboxTarget;
}

// Types into a field from the keyboard: focus, clear, type.
inline void type_keys(Driver& d, std::string_view name, const std::string& text) {
  d.focus(name);
  d.key(R_KEY_END);
  for (std::size_t n = ui::text_of(d.widget(name)).size(); n > 0; --n) d.key(R_KEY_BACKSPACE);
  Rwindow_inject_text(Rwidget_get_window(d.widget(name)), text.c_str());
  d.frames(2);
}

// Answers the current step's question as a player would: the choice button, the typed number and
// Check (Enter in the field), or the sandbox edit and Check.
inline void give_answer(App& app, Driver& d, bool right, bool keyboard) {
  const LessonStep& s = *app.vm().shell().lesson.step();
  const Locale L = app.vm().locale();
  switch (s.question->type) {
    case StepQuestion::Type::Choice: {
      const int k = right ? s.question->answer : test_support::choice_wrong(s);
      REQUIRE(press(d, names::kLessonChoices[static_cast<std::size_t>(k)], keyboard));
      break;
    }
    case StepQuestion::Type::Number: {
      const std::string text = right ? test_support::number_answer(s, L) : test_support::number_wrong(s, L);
      if (keyboard) {
        type_keys(d, names::kLessonNumber, text);
        d.key(R_KEY_ENTER);  // Enter in the field checks it
        d.frames(2);
      } else {
        d.type_into(names::kLessonNumber, text);
        REQUIRE(d.click(names::kLessonCheck));
      }
      break;
    }
    case StepQuestion::Type::Set: {
      const auto e = right ? test_support::set_answer(s, L) : test_support::set_wrong(s, L);
      REQUIRE(e.has_value());
      keyboard ? type_keys(d, sandbox_input(e->field), e->text) : d.type_into(sandbox_input(e->field), e->text);
      REQUIRE(press(d, names::kLessonSetCheck, keyboard));
      break;
    }
  }
}

// Plays the open lesson to its end screen (each question wrong first when asked), then Finish.
inline void play_lesson(App& app, Driver& d, bool wrong_first, bool keyboard, const AcademyShot& shot = {}) {
  const std::string id = app.vm().shell().lesson.lesson()->id;
  for (int guard = 0; guard < 60 && !app.vm().shell().lesson.finished(); ++guard) {
    const LessonStep& s = *app.vm().shell().lesson.step();
    INFO(id << " step " << s.id);
    CHECK(d.text(names::kLessonTitle) == app.vm().tr("academyLesson." + id + "." + s.id + ".title"));
    if (shot && !s.is_question()) shot(id + "-" + s.id);
    if (s.is_question()) {
      if (wrong_first) {
        give_answer(app, d, false, keyboard);
        CHECK(app.vm().shell().lesson.status() == AnswerStatus::Wrong);
        CHECK(visible(d, names::kLessonFeedback));
        CHECK(Rwidget_is_enabled(d.widget(names::kLessonNext)) == 0);
      }
      give_answer(app, d, true, keyboard);
      REQUIRE(app.vm().shell().lesson.status() == AnswerStatus::Right);
      if (shot) shot(id + "-" + s.id);
    }
    REQUIRE(press(d, names::kLessonNext, keyboard));
  }
  REQUIRE(app.vm().shell().lesson.finished());
  CHECK(visible(d, names::kLessonDonePanel));
  if (shot) shot(id + "-done");
  REQUIRE(press(d, names::kLessonFinish, keyboard));
}

// Academy.svelte: every lesson started from its row, played and completed; each ends up "done".
inline void flow_every_lesson(App& app, Driver& d, bool wrong_first, bool keyboard, const AcademyShot& shot = {}) {
  REQUIRE(d.click(names::kMenuAcademy));
  REQUIRE(app.vm().screen() == Screen::Academy);
  if (shot) shot("academy");
  const int count = Rlist_view_get_item_count(d.widget(names::kAcademyLessons));
  REQUIRE(count == static_cast<int>(web_content().lessons.size()));
  for (int i = 0; i < count; ++i) {
    d.focus(names::kAcademyLessons);  // the list from the keyboard: the next row down
    if (i > 0) d.key(R_KEY_DOWN);
    REQUIRE(app.vm().shell().academy.academy_selected == i);
    REQUIRE(press(d, names::kAcademyStart, keyboard));
    REQUIRE(app.vm().screen() == Screen::Lesson);
    play_lesson(app, d, wrong_first, keyboard, shot);
    REQUIRE(app.vm().screen() == Screen::Academy);
  }
  CHECK(app.vm().progress().lessons_done.size() == web_content().lessons.size());
  if (shot) shot("academy-all-done");
  REQUIRE(d.click(names::kAcademyBack));
}

// Walks the open lesson to a step, answering each question right on the way.
inline void walk_to_step(App& app, Driver& d, std::string_view step, bool keyboard) {
  for (int guard = 0; guard < 40 && app.vm().shell().lesson.step()->id != step; ++guard) {
    if (app.vm().shell().lesson.step()->is_question()) give_answer(app, d, true, keyboard);
    REQUIRE(press(d, names::kLessonNext, keyboard));
  }
  REQUIRE(app.vm().shell().lesson.step()->id == step);
}

// Moves a slider with the arrow keys, `n` steps (negative: left).
inline void slide_keys(Driver& d, std::string_view slider, int n) {
  d.focus(slider);
  for (int i = 0; i < (n < 0 ? -n : n); ++i) d.key(n < 0 ? R_KEY_LEFT : R_KEY_RIGHT);
  d.frames(2);
}

inline std::string lab_name(std::string_view suffix) { return std::string(names::kLessonLabPrefix) + std::string(suffix); }

// LessonWhatIf.svelte and LessonBuilder.svelte: the lab on options-greeks' call (the engine's 5,71 and
// Greeks the text states, only the unlocked sliders move, +2% gives 6,83), the vega transfer answered
// from the lab's P&L at +4 points, and strategies-shapes' builder (a template priced and analysed by the
// engine, the short straddle from its checkbox), all from the keyboard or the mouse.
inline void flow_lesson_engine_widgets(App& app, Driver& d, bool keyboard, const AcademyShot& shot = {}) {
  AppViewModel& vm = app.vm();
  vm.open_lesson("options-greeks", Screen::Menu);
  d.frames(2);
  walk_to_step(app, d, "move", keyboard);
  REQUIRE(d.until([&] { return vm.lesson_lab().result().has_value(); }));
  d.frames(2);
  CHECK(visible(d, names::kLessonWhatIfBox));
  CHECK_FALSE(visible(d, names::kLessonBuilderBox));
  CHECK(d.text(lab_name("summary")).find(fmt::fmt_num(5.71, vm.locale(), 2)) != std::string::npos);
  CHECK(Rwidget_is_enabled(d.widget(lab_name("iv"))) == 0);  // fixed in this lesson
  CHECK(Rwidget_is_enabled(d.widget(lab_name("price"))) != 0);
  if (shot) shot("lesson-whatif-zero");
  slide_keys(d, lab_name("price"), 2);
  REQUIRE(d.until([&] { return vm.lesson_lab().shift.price == 2 && vm.lesson_lab().result() && fmt::num(vm.lesson_lab().result()->legs[0].value) > 6.8; }));
  d.frames(2);
  CHECK(d.text(lab_name("price-value")).rfind("+2%", 0) == 0);
  CHECK(d.text(lab_name("summary")).find(fmt::fmt_num(6.83, vm.locale(), 2)) != std::string::npos);
  if (shot) shot("lesson-whatif-price");
  walk_to_step(app, d, "vega", keyboard);
  REQUIRE(d.until([&] { return vm.lesson_lab().result().has_value() && vm.lesson_lab().shift == game::LabShift{}; }));
  slide_keys(d, lab_name("iv"), 4);
  REQUIRE(d.until([&] { return vm.lesson_lab().shift.iv == 4 && vm.lesson_lab().result() && fmt::num(vm.lesson_lab().result()->net.pnl) > 1.5; }));
  d.frames(2);
  CHECK(d.text(lab_name("summary")).find("+" + fmt::fmt_num(1.54, vm.locale(), 2)) != std::string::npos);
  if (shot) shot("lesson-whatif-vega");
  give_answer(app, d, true, keyboard);  // the number the lab shows
  CHECK(vm.shell().lesson.status() == AnswerStatus::Right);
  REQUIRE(press(d, names::kLessonLeave, keyboard));

  vm.open_lesson("strategies-shapes", Screen::Menu);
  d.frames(2);
  walk_to_step(app, d, "view", keyboard);
  d.frames(2);
  CHECK(visible(d, names::kLessonBuilderBox));
  CHECK(visible(d, names::kLessonBuilderPick));
  CHECK_FALSE(visible(d, names::kLessonBuilderPayoff));
  const std::string bull = std::string(names::kLessonBuilderTemplatePrefix) + "0";
  REQUIRE(press(d, bull, keyboard));
  REQUIRE(d.until([&] { return vm.lesson_builder().result().has_value(); }));
  d.frames(3);
  CHECK(visible(d, names::kLessonBuilderPayoff));
  CHECK_FALSE(visible(d, names::kLessonBuilderPick));
  CHECK(d.text(names::kLessonBuilderAnalysis).find(vm.tr("lab.maxLoss")) != std::string::npos);
  const auto name_of = [&](const std::string& w) { return std::string(Rwidget_get_accessible_name(d.widget(w))); };
  CHECK(name_of(bull) == vm.tr("strategy.bull_call") + " \xE2\x9C\x93");  // the picked template is marked
  if (shot) shot("lesson-builder-bull");
  const std::string straddle = std::string(names::kLessonBuilderTemplatePrefix) + "4";
  REQUIRE(press(d, straddle, keyboard));
  REQUIRE(d.until([&] { return vm.lesson_builder().tmpl() == game::Template::Straddle && vm.lesson_builder().result().has_value(); }));
  d.frames(2);
  REQUIRE(Rwidget_is_enabled(d.widget(names::kLessonBuilderShort)) != 0);
  if (keyboard) {
    d.focus(names::kLessonBuilderShort);
    d.key(R_KEY_SPACE);
    d.frames(2);
  } else {
    REQUIRE(d.click(names::kLessonBuilderShort));
  }
  REQUIRE(d.until([&] { return vm.lesson_builder().short_side() && vm.lesson_builder().result().has_value(); }));
  d.frames(3);
  CHECK(name_of(straddle) == vm.tr("strategy.short_straddle") + " \xE2\x9C\x93");
  CHECK(name_of(bull) == vm.tr("strategy.bull_call"));
  if (shot) shot("lesson-builder-short-straddle");
  give_answer(app, d, true, keyboard);
  REQUIRE(press(d, names::kLessonLeave, keyboard));
}

// Glossary.svelte: opened from the menu, search without accents, a term opened (its "new" mark goes),
// a related term followed, and back; then a lesson's term button opens it at that term and returns.
inline void flow_glossary(App& app, Driver& d, const AcademyShot& shot = {}) {
  REQUIRE(d.click(names::kMenuGlossary));
  REQUIRE(app.vm().screen() == Screen::Glossary);
  CHECK(visible(d, names::kGlossaryPick));
  d.type_into(names::kGlossarySearch, "liquidacao");
  CHECK(Rlist_view_get_item_count(d.widget(names::kGlossaryList)) >= 1);
  d.focus(names::kGlossaryList);
  d.key(R_KEY_DOWN);
  REQUIRE(visible(d, names::kGlossaryEntry));
  const std::string term = app.vm().shell().academy.glossary_term;
  CHECK(d.text(names::kGlossaryTerm) == app.vm().tr("glossary." + term + ".term"));
  CHECK(contains(app.vm().progress().glossary_seen, term));
  if (shot) shot("glossary-term");
  if (visible(d, names::kGlossaryRelated[0])) {
    REQUIRE(d.click(names::kGlossaryRelated[0]));
    CHECK(app.vm().shell().academy.glossary_term != term);
  }
  REQUIRE(d.click(names::kGlossaryBack));
  REQUIRE(app.vm().screen() == Screen::Menu);

  // In place: a lesson step's term button carries the definition and opens the glossary there.
  app.vm().open_lesson("futures-margin", Screen::Menu);
  d.frames(2);
  for (int guard = 0; guard < 20 && app.vm().shell().lesson.step()->terms.empty(); ++guard) {
    if (app.vm().shell().lesson.step()->is_question()) give_answer(app, d, true, false);
    REQUIRE(d.click(names::kLessonNext));
  }
  const std::string id = app.vm().shell().lesson.step()->terms[0];
  const Rid button = d.widget(names::kLessonTerms[0]);
  CHECK(std::string(Rwidget_get_tooltip(button)).starts_with(app.vm().tr("glossary." + id + ".term") + ": "));
  d.focus(names::kLessonTerms[0]);  // the focused term's definition shows on the help line
  CHECK(visible(d, names::kLessonTermHelp));
  CHECK(d.text(names::kLessonTermHelp) == std::string(Rwidget_get_tooltip(button)));
  if (shot) shot("lesson-term-focus");
  d.key(R_KEY_ENTER);
  d.frames(2);
  REQUIRE(app.vm().screen() == Screen::Glossary);
  CHECK(app.vm().shell().academy.glossary_term == id);
  REQUIRE(d.click(names::kGlossaryBack));
  CHECK(app.vm().screen() == Screen::Lesson);
  REQUIRE(d.click(names::kLessonLeave));
  CHECK(app.vm().screen() == Screen::Menu);
}

// The guided first trade on the first campaign terminal: started by itself, Space on its Next button
// moves the tour (never the terminal), a typed stop and one advance complete their steps, skip with Esc,
// replay from the help overlay and from the briefing.
inline void flow_coach(App& app, Driver& d, const AcademyShot& shot = {}) {
  REQUIRE(d.click(names::kCampaignStart));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Tutorial || app.vm().screen() == Screen::Briefing; }));
  if (app.vm().screen() == Screen::Tutorial) d.key(R_KEY_ESCAPE);
  REQUIRE(app.vm().screen() == Screen::Briefing);
  REQUIRE(d.click(names::kBriefGo));
  REQUIRE(app.vm().screen() == Screen::Game);
  Terminal t(app, d);
  t.skip_question();
  d.frames(3);
  REQUIRE(app.vm().coach().step() != nullptr);
  CHECK(app.vm().coach().step()->id == "chart");
  REQUIRE(visible(d, names::kGameCoach));
  CHECK(d.text(names::kGameCoachTitle) == "O gráfico");
  // Focus moved to the callout; Space there is the button's: the tour moves, the run does not.
  CHECK(Rwidget_is_focused(d.widget(names::kGameCoachNext)) != 0);
  const int bar = t.g().step;
  d.key(R_KEY_SPACE);
  REQUIRE(d.idle());
  CHECK(t.g().step == bar);
  CHECK(app.vm().coach().step()->id == "ticket");
  if (shot) shot("coach-ticket");
  d.key(R_KEY_ENTER);
  d.frames(2);
  REQUIRE(app.vm().coach().step()->id == "stop");
  if (shot) shot("coach-stop");
  d.type_into(names::game::kStop, "1");  // typing a stop completes the step
  d.frames(2);
  REQUIRE(app.vm().coach().step()->id == "risk");
  REQUIRE(d.click(names::kGameCoachNext));
  REQUIRE(app.vm().coach().step()->id == "advance");
  if (shot) shot("coach-advance");
  t.advance();  // one advance completes the terminal part
  d.frames(2);
  REQUIRE(app.vm().coach().step() != nullptr);
  CHECK(app.vm().coach().step()->id == "debrief");
  CHECK_FALSE(visible(d, names::kGameCoach));
  CHECK(contains(app.vm().progress().coach_done, std::string("first-trade")));

  // Replay from the help overlay: starts here; Esc skips it.
  REQUIRE(d.click(names::game::kHelp));
  REQUIRE(app.vm().game().modal() == game::Modal::Help);
  REQUIRE(d.click(names::kGameHelpTour));
  d.frames(2);
  REQUIRE(app.vm().coach().step() != nullptr);
  CHECK(app.vm().coach().step()->id == "chart");
  d.key(R_KEY_ESCAPE);
  d.frames(2);
  CHECK(app.vm().coach().step() == nullptr);
  CHECK_FALSE(visible(d, names::kGameCoach));

  // Replay from the briefing: asked there, it starts when that terminal opens.
  REQUIRE(d.click(names::kGameMenu));
  REQUIRE(d.click(names::kCampaignContinue));
  d.frames(3);
  CHECK(app.vm().coach().step() == nullptr);  // not by itself any more
  REQUIRE(d.click(names::kGameMenu));
  app.vm().select(0);
  d.frames(2);
  REQUIRE(d.click(names::kMenuStart));
  REQUIRE(d.until([&] { return app.vm().screen() == Screen::Tutorial || app.vm().screen() == Screen::Briefing; }));
  if (app.vm().screen() == Screen::Tutorial) d.key(R_KEY_ESCAPE);
  REQUIRE(app.vm().screen() == Screen::Briefing);
  REQUIRE(d.click(names::kBriefTour));
  REQUIRE(d.click(names::kBriefGo));
  t.skip_question();
  d.frames(3);
  REQUIRE(app.vm().coach().step() != nullptr);
  CHECK(app.vm().coach().step()->id == "chart");
  if (shot) shot("coach-replay");
  REQUIRE(d.click(names::kGameCoachSkip));
  CHECK(app.vm().coach().step() == nullptr);
  REQUIRE(d.click(names::kGameMenu));
}

}  // namespace tradelab::desktop::testing
