## ADDED Requirements

### Requirement: Shared glossary
The game SHALL ship a glossary of trading terms as one data source, read by every client. Each term SHALL have, in PT-BR and en:
- a name and its aliases;
- a short definition of at most two sentences;
- a longer explanation;
- optionally, a worked example.

Each term SHALL also name the curriculum stage that teaches it and its related terms. Every term the UI marks, and every term a lesson or tutorial links, SHALL exist in both locales. A worked example's numbers SHALL be verified by the same checks as lesson numbers.

#### Scenario: Linked term missing
- **WHEN** a lesson links a term id that the glossary does not define in both locales
- **THEN** the content tests SHALL fail

### Requirement: Glossary screen
The client SHALL offer a glossary screen from the menu and the Academy. It SHALL be searchable in the current locale, ignoring accents and case, list terms alphabetically, and show each term's stage and related terms.

#### Scenario: Search without accents
- **WHEN** the PT-BR player searches "liquidacao"
- **THEN** the term "Liquidação" SHALL be listed

### Requirement: In-place definitions
Marked terms in the UI SHALL show their short definition in place:
- on pointer hover and on keyboard focus, as a tooltip tied to the term for screen readers, kept inside the viewport and never clipped by the panel the term sits in;
- on tap on touch screens.

A "more" link SHALL open the glossary at that term. Marked terms SHALL include at least the ticket's fields and estimates, the rail's account figures, the option chain's column headers, the strategy builder's outputs and the debrief's findings.

#### Scenario: Definition on focus
- **WHEN** keyboard focus reaches the ticket's liquidation estimate
- **THEN** the short definition of "liquidation price" SHALL be shown and exposed as the control's description
