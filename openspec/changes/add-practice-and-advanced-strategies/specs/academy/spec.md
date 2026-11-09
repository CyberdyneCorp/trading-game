## ADDED Requirements

### Requirement: Lessons for the new stages
The Academy SHALL add six lessons in the existing format (concept, demonstration, decision, feedback, transfer), in PT-BR and en:
- `vol-gamma-scalping`: delta hedging, ½Γ(ΔS)², theta as rent, realized against implied;
- `carry-funding`: carry against costs, put-call parity at a zero rate, isolated margin on a hedged book, basis in real markets;
- `vol-events`: the IV bid and crush, the implied move, vega × IV change;
- `options-calendars`: calendar and diagonal spreads, the debit as the near-expiry bound, calendar margin;
- `management-scaling`: scaling, partial exits, breakeven stops, trailing, time in force and OCO;
- `mind-biases`: revenge trading, FOMO, averaging down, cooldown and planning.

They SHALL use only illustrative markets, never name a mission id, and never gate a stage.

#### Scenario: No mission named
- **WHEN** the lesson files are checked
- **THEN** no lesson SHALL name a mission id or describe a mission's outcome

### Requirement: Trailing stop lesson step
The `orders-execution` lesson SHALL gain a step on the trailing stop: a chart widget showing closes and the trigger ratchet, with a `trail` check, followed by a question on where the trigger sits after a stated sequence of closes.

#### Scenario: Ratchet checked
- **WHEN** the step states the trigger after closes 100, 103 and 101 for a long trailing stop of 2 at a tick of 0.5
- **THEN** the `trail` check SHALL produce 101 (best close 103 − 2), and the tests SHALL verify it against the web mirror and the engine

### Requirement: New check kinds
Lesson and drill content SHALL support the check kinds `trail`, `hedge` (units to neutralize), `scalp` (½Γ(ΔS)²), `calendar` (net debit) and `scale` (average entry and remaining risk). Each kind SHALL be verified by Vitest with the web mirrors, and the kinds the engine computes SHALL also be verified through the Python binding. Every number in a new lesson's texts SHALL be produced by a check, a widget's output, an answer or a declared given.

#### Scenario: Hedge check
- **WHEN** a lesson states that 50 units with delta 0.20 need 10 perpetual units sold to be neutral
- **THEN** a `hedge` check SHALL produce 10
