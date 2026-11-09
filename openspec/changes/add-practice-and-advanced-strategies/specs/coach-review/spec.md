## ADDED Requirements

### Requirement: Engine coach review
The engine SHALL offer a coach review of a finished run through the C ABI and every binding. It SHALL be refused while the run is active, and it SHALL NOT change any run, log or digest. It SHALL replay the run's own log on a fresh state of the same scenario, seed and start options, and evaluate its rules at each decision step using only the state as of that step: bars up to that step, indicators at that step, releases announced by that step without perks, and the positions, orders and plans.

It SHALL return at most 12 moments ordered by step (at most 9 warnings and 3 good moments). Each moment SHALL have a step, a code, a tone, its evidence values, and the lesson and glossary ids. The codes SHALL be:
- warnings: `entry_without_stop`, `stop_moved_away`, `added_against_structure`, `risk_above_budget`, `held_into_release_unhedged`, `revenge_trade`, `fomo_entry`, `averaging_down`, `risk_limit_exceeded`;
- good moments: `cut_at_invalidation`, `stop_to_breakeven_after_1r`, `partial_at_target`, `stayed_flat_before_release`.

#### Scenario: Added risk while the trend broke
- **WHEN** a player adds to a long at a step where the close is below SMA(20) and below the lowest low of the previous 10 bars
- **THEN** the review SHALL include `added_against_structure` at that step, with the close, the SMA and the low as evidence

#### Scenario: No look-ahead
- **WHEN** the bars after step `s` are replaced
- **THEN** every moment at or before step `s` SHALL be unchanged

#### Scenario: Refused while active
- **WHEN** the coach review is requested for an active run
- **THEN** the engine SHALL refuse it and change nothing

### Requirement: Coach mode
From a finished run's debrief, the client SHALL offer "Review with the coach". It SHALL step through the moments, and for each one:
- show the run's chart only up to the moment's bar;
- show the moment's text in the shown locale with the evidence numbers;
- offer "Retry from here" for warnings.

The reveal SHALL be linked only after the last moment.

#### Scenario: Future hidden at a moment
- **WHEN** coach mode shows a moment at step 84
- **THEN** no bar after step 84 SHALL be drawn on its chart
