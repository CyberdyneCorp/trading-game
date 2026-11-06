## ADDED Requirements

### Requirement: Guided first trade
The first time the player opens the terminal in a campaign on the device, the client SHALL start a guided tour of coach marks on the real terminal. In order, the marks SHALL point at:
1. the chart;
2. the order ticket;
3. the ticket's stop field;
4. the risk estimate;
5. the advance control;
6. the debrief, when that run's debrief opens.

Each mark SHALL show a short text in the current locale. The advance step SHALL complete when the player advances or chooses Next. The tour SHALL describe controls and process only, never the market or the mission's outcome.

#### Scenario: First campaign mission
- **WHEN** a player starts their first campaign run and the terminal opens
- **THEN** the first coach mark SHALL point at the chart, and after the advance step the remaining step SHALL be shown when that run's debrief opens

#### Scenario: Once per device
- **WHEN** the tour was completed or skipped and the player starts another campaign mission
- **THEN** no coach mark SHALL be shown

### Requirement: Skip and replay
Every coach mark SHALL have "Skip tour", and `Esc` SHALL skip it too. Skipping or finishing SHALL mark the tour done on the device. The help overlay and the briefing SHALL offer "Guided tour", which SHALL replay it in the current run, in any mode.

#### Scenario: Replay from help
- **WHEN** the player opens the help overlay in a free-play run and chooses "Guided tour"
- **THEN** the tour SHALL start at the chart step

### Requirement: Accessible coach marks
Coach marks SHALL be operable by keyboard alone and announced to screen readers:
- focus moves to the mark when it opens and returns to its target on Next;
- the target is outlined and described by the mark's text;
- each step is announced in a polite live region;
- with reduced motion, nothing animates.

While focus is on a mark, `Space` and `Enter` SHALL act on its buttons and SHALL NOT advance the market. The terminal SHALL stay usable while a mark is shown.

#### Scenario: Space on a coach mark
- **WHEN** focus is on a coach mark's Next button and the player presses `Space`
- **THEN** the tour SHALL go to the next step and the market SHALL NOT advance

#### Scenario: Automated accessibility check
- **WHEN** the accessibility checker runs with a coach mark shown
- **THEN** it SHALL report no violation
