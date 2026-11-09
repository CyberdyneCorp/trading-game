## ADDED Requirements

### Requirement: Progress version 4
The web client's stored progress SHALL move to version 4, migrated from versions 1–3 with defaults, keeping every earlier key. It SHALL add:
- the campaign run's mission order, with the 17-mission order for runs that predate it;
- `drillReviews` and `drillHistory`;
- `dailyChallenges`;
- `guessHistory`.

Run records MAY carry a `daily` link and an assisted link of kind `mistake`. The IndexedDB database version SHALL stay unchanged.

#### Scenario: Migration keeps the campaign
- **WHEN** a version 3 progress record with an active campaign at index 5 is loaded
- **THEN** it SHALL be migrated to version 4 with that campaign's order set to the 17-mission order and its next mission unchanged

### Requirement: Practice formats in the menu
The menu SHALL offer Drills, the Daily challenge and Guess rounds, reachable by keyboard and named for screen readers. None of them SHALL show a streak or pressure the player to return.

#### Scenario: Keyboard access
- **WHEN** a keyboard-only player opens the menu
- **THEN** Drills, Daily challenge and Guess rounds SHALL each be reachable and announced
