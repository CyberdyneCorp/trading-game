## ADDED Requirements

### Requirement: Drill content format
Drills SHALL be data files, one per skill, holding templates. A template SHALL contain:
- an id, the skill, the stage and the lesson ids it reviews;
- parameter ranges on the tick or lot grid;
- an answer given as a lesson check kind with parameter references, and a tolerance;
- per locale, a prompt with placeholders, an explanation and optional choices.

The skills SHALL cover payoffs, breakevens, reading Greeks, Greek signs, sizing, liquidation prices, funding, R multiples, hedging, calendars and trailing triggers. Answers SHALL be computed by the same check functions that the lesson tests compare with the engine (option values and Greeks through the engine's what-if). A template whose engine call the pinned build does not support SHALL be skipped.

#### Scenario: Liquidation item
- **WHEN** a liquidation template draws entry 100, leverage 10 and a long side
- **THEN** the expected answer SHALL be the value the `liquidation` check produces for those parameters with the mission defaults

#### Scenario: Every number in an explanation is produced
- **WHEN** the drill files are checked
- **THEN** every number in an explanation SHALL come from the item's parameters or its answer

### Requirement: Drill session
A drill session SHALL hold 6 items with a two-minute timer that is shown but never fails the session: unanswered items count as skipped. A setting SHALL turn the timer off. A wrong answer SHALL show the explanation with the worked numbers. The end screen SHALL show the first-try results and link each item's lesson. There SHALL be no streak and no reward for frequency.

#### Scenario: Time runs out
- **WHEN** the two minutes end with two items unanswered
- **THEN** the session SHALL end with those two items marked skipped, and no penalty beyond that

### Requirement: Items fed by spaced review
A session SHALL choose items in this order:
1. up to 3 items from skills tied to lessons whose review is due;
2. then skills due in the drill schedule (boxes of 1, 3, 7 and 21 days: all right moves a skill up, any wrong sends it back to box 1);
3. then the skills with the lowest first-try rate over the last 30 sessions.

Drill answers SHALL NOT complete a lesson review.

#### Scenario: Due lesson first
- **WHEN** the `options-payoff` lesson review is due today
- **THEN** the session SHALL include at least one payoff or breakeven item

### Requirement: Deterministic drill items
Item parameters SHALL be drawn from mulberry32, seeded with FNV-1a 32 of the drill version, the local date and the session index. Decimal parameters SHALL be built from integer multiples of the step. Web and desktop SHALL generate the same items and answers for the same seed, which a shared parity fixture checks.

#### Scenario: Same items on both clients
- **WHEN** web and desktop generate session 1 of the same date
- **THEN** their items, parameters and answers SHALL be identical
