// How a test answers an Academy lesson step (unit, headless and windowed tests share it): the right
// choice, the typed number in the locale, or the sandbox edit that reaches a `set` target; and a wrong
// answer of each kind, to see the feedback first.
#pragma once

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include "model/content.hpp"
#include "model/i18n.hpp"

namespace tradelab::desktop::test_support {

// A number written the way a player types it in the locale ("2,6" in PT-BR).
inline std::string typed(double v, Locale locale) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.10g", v);
  std::string s = buf;
  if (locale == Locale::PtBR) {
    for (char& c : s) {
      if (c == '.') c = ',';
    }
  }
  return s;
}

// The sandbox edit that answers a set question: which field (qty, leverage, stop, target) and its text.
struct SandboxEdit {
  std::string field;
  std::string text;
};

inline std::optional<SandboxEdit> set_answer(const LessonStep& step, Locale locale) {
  const auto* w = std::get_if<TicketWidget>(&step.widget);
  if (w == nullptr || !step.question || step.question->type != StepQuestion::Type::Set) return std::nullopt;
  const StepQuestion& q = *step.question;
  if (q.field == "qty" || q.field == "leverage" || q.field == "stop" || q.field == "target") {
    return SandboxEdit{q.field, typed(q.value, locale)};
  }
  if (q.field == "riskAtStop" && w->stop && w->editable("qty")) {
    return SandboxEdit{"qty", typed(q.value / std::abs(w->entry - *w->stop), locale)};
  }
  if (q.field == "notional" && w->editable("qty")) return SandboxEdit{"qty", typed(q.value / w->entry, locale)};
  return std::nullopt;
}

// A sandbox edit that misses the target (to see the wrong feedback first).
inline std::optional<SandboxEdit> set_wrong(const LessonStep& step, Locale locale) {
  auto right = set_answer(step, locale);
  if (!right) return std::nullopt;
  const auto* w = std::get_if<TicketWidget>(&step.widget);
  if (right->field == "leverage") return SandboxEdit{"leverage", std::to_string(w->leverage == 1 ? 2 : 1)};
  return SandboxEdit{right->field, right->field == "qty" ? "1" : typed(w->entry, locale)};
}

inline std::string number_answer(const LessonStep& step, Locale locale) { return typed(step.question->value, locale); }

inline std::string number_wrong(const LessonStep& step, Locale locale) {
  return typed(step.question->value + 10 * (step.question->tolerance + 1), locale);
}

inline int choice_wrong(const LessonStep& step) {
  return (step.question->answer + 1) % static_cast<int>(step.question->choices.size());
}

}  // namespace tradelab::desktop::test_support
