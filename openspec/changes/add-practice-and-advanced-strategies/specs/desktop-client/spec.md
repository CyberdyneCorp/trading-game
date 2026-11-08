## ADDED Requirements

### Requirement: Desktop builder and terminal parity
The desktop SHALL ship every wave 1 feature with the web's behavior and numbers:
- time in force in the tickets, OCO linking in the orders panel, and the trailing stop's tabs, planned notice and chart label;
- builder limit legs and combo states, two-expiry legs with calendar and diagonal templates, roll and adjust;
- the heatmap and portfolio panels with risk-limit warnings and the hedge ticket.

Its action log SHALL store and replay the `tif`/`tifBars` fields and the `oco` action. The ports of the web's client logic SHALL be checked by web parity fixtures.

#### Scenario: Same calendar roll
- **WHEN** the same m17 seed is played on web and desktop with a calendar built, rolled and adjusted by the same actions
- **THEN** the dry runs, fills, heatmap cells, portfolio totals and final digest SHALL be identical

### Requirement: Desktop missions m18–m25
The desktop SHALL offer m18–m25 in the menu, the campaign (with the order snapshot) and the curriculum branches, with their briefings (risk limits, calendar margin), tutorials with the new art drawn as display lists, and debriefs (bias findings with evidence, episodes, the strategy comparison). Its progress row SHALL follow progress version 4.

#### Scenario: Same debrief
- **WHEN** m23 seed 3 is played with the same actions on web and desktop
- **THEN** the bias findings, their evidence and the score SHALL be identical

### Requirement: Desktop practice formats
The desktop SHALL offer daily drills, the daily challenge, guess rounds, coach mode and mistake replay with the web's content, engine calls and selection rules. Shared parity fixtures (`drills`, `daily`, `guess`) SHALL check that both clients produce the same items, challenges and scores. Every new screen SHALL be usable by keyboard, named for screen readers, and captured in windowed reference images on macOS and Linux.

#### Scenario: Same daily challenge
- **WHEN** web and desktop open the daily challenge on the same local date
- **THEN** both SHALL start the same mission with the same seed
