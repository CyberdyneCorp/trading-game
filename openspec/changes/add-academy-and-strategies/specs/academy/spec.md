## ADDED Requirements

### Requirement: A lesson for every stage
The Academy SHALL have at least one lesson for every curriculum stage. Each lesson SHALL follow concept, demonstration, decision, feedback and a transfer exercise on a different illustrative market (design D2). The lessons SHALL cover at least:
- **Orders:** market, limit and stop orders, spread, slippage and capacity.
- **Futures:** long and short P&L; margin, leverage and liquidation; mark against last price; funding.
- **Risk:** sizing from the stop, R multiples, expectancy, drawdown and risk of ruin.
- **Options I:** call and put, premium, intrinsic against time value, payoff at expiry and breakeven.
- **Options II:** delta, gamma, theta and vega; implied volatility; time decay; IV crush around events.
- **Options III:** writing and margin, settlement, covered call, protective put and spreads.
- **Strategies:** the builder's structures, and defined against undefined risk.
- **Patterns & Indicators:** a failure example.
- **Events:** consensus against surprise, and gaps.
- **Evaluation:** thesis, invalidation, journal, FOMO, revenge trading, overconfidence and calibration.

Every lesson SHALL exist in PT-BR and en.

#### Scenario: Every stage has a lesson
- **WHEN** the lesson index is loaded
- **THEN** every stage of the curriculum SHALL have at least one lesson, and every lesson SHALL have texts in PT-BR and en for every step

#### Scenario: Transfer on another market
- **WHEN** a lesson's transfer step is shown
- **THEN** its chart or position SHALL use a market id or price level that none of that lesson's demonstration steps used

### Requirement: Interactive widgets and question types
Lesson steps SHALL be able to show:
- a chart with labeled levels;
- a payoff diagram of one to four option legs plus an optional perpetual leg;
- the options what-if lab;
- a perpetual ticket sandbox;
- the strategy builder without submission;
- a data table.

Questions SHALL be multiple choice, a typed number with a tolerance (either decimal mark), or a target to reach by moving a widget. A wrong answer SHALL show feedback explaining the mistake and let the player answer again. The end screen SHALL show the answers right on the first try.

#### Scenario: Sizing by moving the ticket
- **WHEN** a step asks to size a trade so that the risk at the stop is 1% of 10.000 with entry 100 and stop 96
- **THEN** the step SHALL count as right once the sandbox quantity is 25 (within one lot) and SHALL show the risk at the stop

#### Scenario: Wrong answer explained
- **WHEN** the player picks a distractor in a breakeven question
- **THEN** the feedback for that choice SHALL be shown and the question SHALL stay open for another answer

### Requirement: One content source for every client
Lesson structure and texts SHALL live in data files with PT-BR and en texts side by side, read by the web client and generated into the desktop client. A change to a lesson file without regenerating the desktop's content SHALL fail the desktop's drift check.

#### Scenario: Desktop drift
- **WHEN** a lesson's PT-BR text changes and the desktop generator is not run
- **THEN** the desktop i18n check SHALL fail naming the changed key

### Requirement: Lesson numbers are checked
Every option value, Greek, breakeven, maximum profit or maximum loss stated in a lesson SHALL be produced by the engine's pricing (through the what-if call) or verified against it by a test. Every perpetual figure stated (P&L, return, liquidation price, size from the stop, R, expectancy, drawdown recovery) SHALL be verified by a test. A number in an answer or in feedback that no check or widget produces SHALL fail the content tests.

#### Scenario: Wrong breakeven caught
- **WHEN** a lesson states that a call with strike 100 bought for 4 breaks even at 105
- **THEN** the content tests SHALL fail, since the computed breakeven is 104

### Requirement: Lessons do not spoil missions
A lesson SHALL NOT use a mission's market, name a mission id or describe a mission's outcome. Illustrative charts SHALL be labeled illustrative and SHALL draw only the closed bars of the step, with the future hatched.

#### Scenario: Mission named in a lesson
- **WHEN** a lesson file contains a mission id
- **THEN** the content tests SHALL fail

### Requirement: Completion, retakes and spaced review
Lesson completion SHALL be recorded on the device and SHALL survive reloads. A completed lesson SHALL be retakeable at any time. Completing a lesson SHALL schedule a review of its decision and transfer questions 1 day later:
- all right on the first try moves it to the next interval (3, 7, then 21 days), and after the 21-day review it is retired;
- any wrong answer sends it back to the 1-day interval.

When reviews are due, the menu SHALL show how many. The player SHALL be able to dismiss that prompt until the next day, and it SHALL never block play. The schedule uses the device clock and SHALL NOT affect any engine run.

#### Scenario: Review due
- **WHEN** a lesson was completed and one day has passed on the device clock
- **THEN** the menu SHALL show one lesson to review, and the review SHALL ask only that lesson's decision and transfer questions

#### Scenario: Review failed
- **WHEN** the player answers a review question wrong on the first try
- **THEN** the lesson's next review SHALL be due one day later

#### Scenario: Prompt dismissed
- **WHEN** the player dismisses the review prompt
- **THEN** the menu SHALL NOT show it again until the next calendar day, and the lesson SHALL stay due in the Academy screen

### Requirement: Academy screen
The client SHALL offer an Academy screen, from the menu and the curriculum map, listing lessons by stage with their status (new, done, review due), a start or retake control and a link to the glossary.

#### Scenario: Open from the curriculum map
- **WHEN** the player chooses a stage's lesson on the curriculum map
- **THEN** that lesson SHALL start in the runner, and returning SHALL go back to the map
