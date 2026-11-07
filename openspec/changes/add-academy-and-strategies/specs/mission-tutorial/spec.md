## ADDED Requirements

### Requirement: Strategy tutorials state the margin rule
The tutorial of every mission that allows option writing SHALL include a card stating how that mission margins short options:
- per leg (m05, m10, m12);
- or defined-risk spread margin (m13, m14, m16, m17).

m14's card SHALL say that the covered call's short call is margined on its own, because the perpetual is margined separately. Like every tutorial, the cards of m13–m17 SHALL NOT reveal whether the mission's event, rally, crash or breakout happens on a seed.

#### Scenario: Covered call margin
- **WHEN** the player reads m14's tutorial
- **THEN** a card SHALL say that the short call needs its own margin even while the long perpetual covers it in P&L

#### Scenario: No spoiler
- **WHEN** m15's tutorial is shown
- **THEN** no card SHALL state that a crash will happen; it MAY say that one may or may not happen

### Requirement: Tutorial terms link the glossary
Terms in tutorial cards SHALL be able to link glossary terms, shown as in-place definitions on hover, focus or tap.

#### Scenario: Linked term in a card
- **WHEN** a tutorial card links "theta"
- **THEN** focusing it SHALL show theta's short glossary definition
