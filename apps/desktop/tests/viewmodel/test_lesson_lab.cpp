// A lesson step's engine-valued widgets (add-academy-and-strategies D3; web LessonWhatIf.svelte,
// LessonBuilder.svelte): the what-if lab on the step's fixed legs with only its unlocked sliders, and the
// builder's templates on a synthetic chain priced at the model. With the real engine host: the numbers
// the lesson texts state (content checks of options-greeks, the same the web's academyEngine.test.ts
// and the Python binding's test_academy_numbers.py verify), and the builder's priced legs.
#include <doctest/doctest.h>

#include <memory>
#include <string>

#include "model/content.hpp"
#include "model/engine_host.hpp"
#include "support/academy_helpers.hpp"
#include "support/shell_helpers.hpp"
#include "support/ui_queue.hpp"
#include "viewmodel/academy/lesson_lab.hpp"
#include "viewmodel/academy_names.hpp"
#include "viewmodel/app_viewmodel.hpp"
#include "viewmodel/format.hpp"

using namespace tradelab::desktop;
using namespace tradelab::desktop::test_support;
using testing::UiQueue;
namespace names = tradelab::desktop::names;

namespace {

const LessonStep& step_of(const std::string& lesson, const std::string& id) {
  const AcademyLesson* l = web_content().lesson(lesson);
  REQUIRE(l != nullptr);
  for (const auto& s : l->steps) {
    if (s.id == id) return s;
  }
  FAIL("no step " << id << " in " << lesson);
  return l->steps.front();
}

struct Real {
  UiQueue ui;
  std::unique_ptr<AppViewModel> vm;
  EngineHost host{[this](EngineHost::Task t) { ui.post(std::move(t)); },
                  [this](std::shared_ptr<const Snapshot> s) { vm->on_snapshot(std::move(s)); }};
  Real() {
    vm = std::make_unique<AppViewModel>(builtin_catalog(), host.missions(), host, [] { return 1u; });
    testing::tutorials_seen(*vm);
  }
  void settle() {
    for (int i = 0; i < 100; ++i) {
      host.wait_idle();
      if (ui.drain() == 0) return;
    }
  }
  // Opens the lesson and walks to the step, answering each question right on the way.
  void open_at(const std::string& lesson, const std::string& id) {
    vm->open_lesson(lesson, Screen::Academy);
    for (int guard = 0; guard < 40 && vm->shell().lesson.step()->id != id; ++guard) {
      const LessonStep& s = *vm->shell().lesson.step();
      if (s.question && s.question->type == StepQuestion::Type::Choice) vm->lesson_pick(s.question->answer);
      if (s.question && s.question->type == StepQuestion::Type::Number) {
        vm->lesson_set_typed(number_answer(s, vm->locale()));
        vm->lesson_submit_number();
      }
      vm->lesson_next();
    }
    REQUIRE(vm->shell().lesson.step()->id == id);
    settle();
  }
  [[nodiscard]] const game::WhatIfLeg& leg() const { return vm->lesson_lab().result()->legs.at(0); }
  void slide(game::Slider s, int v) {
    vm->lesson_lab().slide(s, v);
    vm->lesson_lab_changed();
    settle();
  }
};

double num(const std::string& s, int digits) { return fmt::num(game::to_fixed(fmt::num(s), digits)); }

}  // namespace

TEST_CASE("lesson widgets: content.json carries the what-if and builder widgets of the web's lessons") {
  const auto* w = std::get_if<WhatIfWidget>(&step_of("options-greeks", "move").widget);
  REQUIRE(w != nullptr);
  CHECK(w->market == "A");
  CHECK(w->forward == 100);
  REQUIRE(w->legs.size() == 1);
  CHECK(w->legs[0].kind == "call");
  CHECK(w->legs[0].strike == 100);
  CHECK(w->legs[0].days == 30);
  CHECK(w->legs[0].iv_pct == 50);
  CHECK(w->legs[0].entry == 5.71);
  CHECK(academy::unlocked_sliders(w->unlocked) == std::vector<game::Slider>{game::Slider::Price, game::Slider::Time});
  const auto* b = std::get_if<BuilderWidget>(&step_of("strategies-shapes", "view").widget);
  REQUIRE(b != nullptr);
  CHECK(b->forward == 620);
  CHECK(b->strikes.size() == 9);
  CHECK(b->days == 30);
  CHECK(b->iv_pct == 45);
  CHECK(b->tmpl.empty());
}

TEST_CASE("lesson widgets: the builder's legs on the synthetic chain, priced at the model to the cent") {
  BuilderWidget w;
  w.forward = 620;
  w.strikes = {580, 590, 600, 610, 620, 630, 640, 650, 660};
  w.days = 30;
  w.iv_pct = 45;
  const auto legs = academy::builder_academy_legs(game::template_legs(game::Template::CoveredCall, w.strikes, w.forward, 1), w);
  REQUIRE(legs.has_value());
  REQUIRE(legs->size() == 2);
  CHECK((*legs)[0].kind == game::LegKind::Perp);
  CHECK((*legs)[0].entry == 620);  // the perpetual at the forward
  CHECK((*legs)[1].strike == 630);
  CHECK((*legs)[1].days == 30);
  CHECK((*legs)[1].iv_pct == 45);
  CHECK((*legs)[1].entry == 0);
  game::WhatIfResult priced;
  priced.legs.resize(2);
  priced.legs[0].value = "620";
  priced.legs[1].value = "13.4567";
  const auto p = academy::priced_legs(*legs, priced);
  CHECK(p[0].entry == 620);
  CHECK(p[1].entry == 13.46);
  CHECK_FALSE(academy::builder_academy_legs(std::nullopt, w).has_value());
}

TEST_CASE("lesson widgets on the engine: options-greeks' lab states the engine's value and Greeks; only its sliders move") {
  Real r;
  r.open_at("options-greeks", "move");
  REQUIRE(r.vm->lesson_lab().result().has_value());
  // The lesson text: 5,71 with delta 0,53, gamma 0,028, theta −0,095 a day and vega 0,114 a point.
  CHECK(num(r.leg().value, 2) == doctest::Approx(5.71));
  CHECK(num(r.leg().delta, 2) == doctest::Approx(0.53));
  CHECK(num(r.leg().gamma, 3) == doctest::Approx(0.028));
  CHECK(num(r.leg().theta, 3) == doctest::Approx(-0.095));
  CHECK(num(r.leg().vega, 3) == doctest::Approx(0.114));
  const ScreenModel m = r.vm->screen_model();
  CHECK(m.is_visible(names::kLessonWhatIfBox));
  CHECK_FALSE(m.is_visible(names::kLessonBuilderBox));
  CHECK(m.text_of(names::kLessonWhatIfCaption).find("laborat\xC3\xB3rio") != std::string::npos);
  const game::Texts t = r.vm->game().texts();
  CHECK_FALSE(r.vm->lesson_lab().slider_view(t, game::Slider::Iv).enabled);  // fixed in this lesson
  CHECK(r.vm->lesson_lab().slider_view(t, game::Slider::Iv).label.find("fixo nesta li\xC3\xA7\xC3\xA3o") != std::string::npos);
  r.slide(game::Slider::Iv, 5);
  CHECK(r.vm->lesson_lab().shift.iv == 0);
  r.slide(game::Slider::Price, 2);  // 102: 6,83, delta 0,58
  CHECK(num(r.leg().value, 2) == doctest::Approx(6.83));
  CHECK(num(r.leg().delta, 2) == doctest::Approx(0.58));
  r.slide(game::Slider::Price, 0);
  r.slide(game::Slider::Time, 5);  // five days on: 5,22
  CHECK(num(r.leg().value, 2) == doctest::Approx(5.22));
  // Back to the step: the lab starts again from zero.
  r.vm->lesson_next();
  r.vm->lesson_prev();
  r.settle();
  CHECK(r.vm->lesson_lab().shift == game::LabShift{});
  CHECK(num(r.leg().value, 2) == doctest::Approx(5.71));
}

TEST_CASE("lesson widgets on the engine: the vega transfer's number is the lab's P&L at +4 points of IV") {
  Real r;
  r.open_at("options-greeks", "vega");
  REQUIRE(r.vm->lesson_lab().result().has_value());
  CHECK(num(r.leg().value, 2) == doctest::Approx(11.33));
  CHECK(num(r.leg().vega, 3) == doctest::Approx(0.383));
  r.slide(game::Slider::Price, 3);  // locked: only IV moves here
  CHECK(r.vm->lesson_lab().shift.price == 0);
  r.slide(game::Slider::Iv, 4);
  const double pnl = num(r.vm->lesson_lab().result()->net.pnl, 2);
  CHECK(pnl == doctest::Approx(1.54));
  CHECK(pnl == doctest::Approx(r.vm->shell().lesson.step()->question->value));
}

TEST_CASE("lesson widgets on the engine: strategies-shapes' builder prices each template and analyses it") {
  Real r;
  r.open_at("strategies-shapes", "view");
  const academy::LessonBuilder& b = r.vm->lesson_builder();
  const game::Texts t = r.vm->game().texts();
  CHECK_FALSE(b.result().has_value());
  CHECK(b.view(t).pick == r.vm->tr("academy.builder.pick"));
  CHECK_FALSE(b.view(t).short_enabled);
  CHECK(r.vm->screen_model().is_visible(names::kLessonBuilderBox));
  INFO(r.vm->screen_model().text_of(names::kLessonBuilderCaption));
  CHECK(r.vm->screen_model().text_of(names::kLessonBuilderCaption).find("30 dias") != std::string::npos);
  r.vm->lesson_builder_pick(game::Template::BullCall);
  r.settle();
  REQUIRE(b.result().has_value());
  const game::WhatIfResult& bull = *b.result();
  REQUIRE(bull.legs.size() == 2);
  // Bought at the model: no P&L now, a debit spread's bounded loss and profit, one breakeven.
  CHECK(fmt::num(bull.net.pnl) == doctest::Approx(0).epsilon(0.02));
  REQUIRE(bull.expiry.has_value());
  REQUIRE(bull.expiry->max_loss.has_value());
  REQUIRE(bull.expiry->max_profit.has_value());
  CHECK(fmt::num(*bull.expiry->max_loss) + fmt::num(*bull.expiry->max_profit) == doctest::Approx(10).epsilon(1e-9));  // strike width
  CHECK(bull.expiry->breakevens.size() == 1);
  academy::LessonBuilderView v = b.view(t);
  CHECK(v.has_result);
  CHECK(v.pick.empty());
  REQUIRE(v.analysis.size() == 3);
  CHECK(v.table.size() >= 4);
  CHECK(v.templates[0].selected);
  // The short straddle: both written, the loss unbounded above.
  r.vm->lesson_builder_pick(game::Template::Straddle);
  r.vm->lesson_builder_short(true);
  r.settle();
  REQUIRE(b.result().has_value());
  CHECK(b.short_side());
  v = b.view(t);
  CHECK(v.templates[4].label == r.vm->tr("strategy.short_straddle"));
  CHECK_FALSE(b.result()->expiry->max_loss.has_value());
  CHECK(v.analysis[2].second.find(r.vm->tr("lab.unboundedAbove", {{"price", ""}}).substr(0, 8)) != std::string::npos);
}
