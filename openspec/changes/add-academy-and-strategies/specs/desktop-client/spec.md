## ADDED Requirements

### Requirement: Academy and strategies on the desktop
The desktop client SHALL offer what the web offers from this change:
- the Academy with every lesson and its widgets, completion, retakes and the spaced-review prompt;
- the what-if lab in the Academy and in the terminal;
- the strategy builder with the same submission order and partial-failure handling;
- the guided first trade;
- the glossary screen with in-place definitions;
- missions m13–m17 with their tutorials, briefing margin rule and strategy comparison.

Their texts and lesson data SHALL be generated from the web's content files, never retyped. Their numbers SHALL come from the linked engine's what-if and dry-run calls, run on the simulation worker like every engine call. Progress for lessons, reviews, the tour and the glossary SHALL persist in the user-data folder.

#### Scenario: Same numbers as the web
- **WHEN** the desktop and the web show the what-if lab for the same legs and shifts
- **THEN** every value, P&L and Greek SHALL be identical

#### Scenario: Lesson by keyboard
- **WHEN** the headless test drives a lesson with a ticket widget by keyboard only
- **THEN** it SHALL reach the end screen with the first-try score

#### Scenario: Windowed captures per platform
- **WHEN** the windowed suite runs on macOS and on Linux
- **THEN** it SHALL capture the Academy, the lab, the builder, the tour and the glossary, and compare them with that platform's reference images
