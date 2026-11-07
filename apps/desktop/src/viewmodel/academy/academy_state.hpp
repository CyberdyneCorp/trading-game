// State of the Academy screens kept by AppViewModel (add-academy-and-strategies tasks D3, D6, D7, D8):
// the lesson in progress (its runner, ticket sandbox and typed answer), where it returns, the Academy
// screen's selection, the glossary's search and selected term, the guided first trade (the review prompt's
// dismissal is a display setting). Plain data; the rules live in viewmodel/academy_flow.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "viewmodel/academy/coach.hpp"
#include "viewmodel/academy/ticket_sandbox.hpp"

namespace tradelab::desktop {

struct AcademyState {
  // The lesson's own screen state (the runner itself is ShellState::lesson).
  std::uint8_t lesson_return = 4;  // the Screen a finished or left lesson goes back to (Curriculum)
  std::optional<academy::SandboxInputs> sandbox;
  // What the player typed in the sandbox's fields: quantity, leverage, stop, target (shown as typed;
  // read with either decimal mark).
  std::array<std::string, 4> sandbox_text;
  std::string typed;  // the answer typed to a number question

  int academy_selected = 0;  // row of the Academy screen's lesson list
  int due_selected = 0;      // row of its reviews due today

  std::string glossary_query;
  std::string glossary_term;          // the term shown ("" = none yet)
  std::uint8_t glossary_return = 0;   // the Screen leaving the glossary goes back to (Menu)
  int glossary_selected = -1;         // row of the search result list

  academy::CoachModel coach;
  std::uint8_t coach_screen = 0;  // the screen the coach saw last (its transitions drive the tour)
  int coach_seen = -1;            // the step it saw last
  std::string coach_stop;         // the ticket's stop text when the stop step showed
  int coach_bar = -1;             // the run's step when the advance step showed
};

}  // namespace tradelab::desktop
