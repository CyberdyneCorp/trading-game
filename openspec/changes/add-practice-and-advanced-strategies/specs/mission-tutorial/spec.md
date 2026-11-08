## ADDED Requirements

### Requirement: Tutorials of m18–m25 with art
Each of m18–m25 SHALL have 3 to 5 tutorial cards in PT-BR and en, shown before its first briefing. The new card art (`scalp`, `carry`, `parity`, `crush`, `calendar`, `scale`, `trail`, `cooldown`) SHALL be drawn the same way on web and desktop, with no label crossing a drawn line in either locale. Cards SHALL teach the rule (rehedging, carry against costs, isolated margin, IV crush, calendar margin, scaling and partial exits, cooldown and planning), and SHALL NOT describe the market's turns or name the bias a psychology market tempts. Every number on a card SHALL be checked by a test.

#### Scenario: No spoiler in a psychology mission
- **WHEN** m24's tutorial cards are checked
- **THEN** they SHALL NOT mention a rally, a reversal, FOMO or chasing by name, and SHALL teach planning entries before the move

#### Scenario: Art in both locales
- **WHEN** the `calendar` card art is drawn in PT-BR and in en
- **THEN** every label SHALL sit inside the art box without crossing a line

### Requirement: Trailing stop card
m22's tutorial SHALL include a trailing stop card (art `trail`: the trigger following the best close) that names both modes (price distance and % of the best close) and that the trigger never loosens. m07's existing card SHALL link the glossary term `trailing-stop`.

#### Scenario: Trailing card
- **WHEN** m22's tutorial is opened
- **THEN** one card SHALL explain the trailing stop with the `trail` art and link the glossary term
