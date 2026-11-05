# Spec Delta

## Purpose

Defines the learning path from candles and orders through perpetual futures and options, and the rules that unlock riskier instruments and higher leverage only through demonstrated understanding.

## ADDED Requirements

### Requirement: Ordered learning stages
The curriculum SHALL contain stages in this dependency order: Foundations, Structure, Orders, Futures I (contract, long/short), Futures II (margin, leverage, liquidation), Futures III (funding), Risk sizing, Options I (payoffs), Options II (IV and Greeks), Options III (writing and margin), Patterns & Indicators, Events, Evaluation. Evaluation comes last because comparing many attempts across regimes and events is the capstone (design D8.4). Each lesson SHALL follow concept, demonstration, decision, feedback and a transfer exercise on a different market.

#### Scenario: Transfer on new market
- **WHEN** a lesson's transfer exercise starts
- **THEN** it SHALL use a scenario window not used by that lesson's demonstration, including warm-up bars

### Requirement: Progressive unlocks
Shorting, leverage caps above 2×, options buying and options writing SHALL be unlocked in Campaign only after completing the corresponding stage's assessment. Unlocks SHALL NOT be purchasable. Sandbox SHALL offer all educational tools with explicit settings, with runs labeled as sandbox.

A stage's assessment is completed by finishing (not going bankrupt in) its assessment mission in a campaign run. The stage's own assessment mission SHALL have that stage's unlocks, since it is where they are taught and assessed; every other campaign mission SHALL have only the unlocks of stages already passed. A locked leverage SHALL be enforced by the engine (the run's leverage cap is lowered to 2× through a start option), and the client SHALL list in the briefing what is locked and which stage unlocks it. Free play keeps each mission's own rules and does not pass stages.

#### Scenario: Writing locked
- **WHEN** a player who has not passed Options III starts a campaign mission other than the Options III assessment
- **THEN** options writing SHALL be disabled in that mission

#### Scenario: Leverage gated until Futures II
- **WHEN** a player who has not passed Futures II starts the Futures I campaign mission, whose own cap is 5×
- **THEN** the engine SHALL refuse leverage above 2× in that run, and the briefing SHALL say leverage above 2× unlocks with Futures II
- **AND** once Futures II is passed, a later campaign run SHALL start that mission with its own 5× cap

### Requirement: Curriculum as data
The stage graph (stages, their prerequisites, assessment missions and unlocks) and the unlock rules SHALL be data, not code paths per stage. A stage with no assessment content yet SHALL be shown as coming soon and SHALL NOT block the stages after it. The client SHALL show a curriculum map with each stage's status (passed, open, locked, coming soon), its assessment missions and its unlocks, and the player's progress.

#### Scenario: Stage without content
- **WHEN** a stage has no assessment mission (Foundations, taught only by its Academy lesson) and the stages before it are cleared
- **THEN** it SHALL show as coming soon and the stage after it (Structure) SHALL be open

### Requirement: Failure examples
Every pattern and indicator lesson SHALL include at least one example where the pattern fails and one high-cost market example. Illustrative examples SHALL be labeled as illustrative and SHALL NOT imply a real success frequency.

#### Scenario: Failing pattern shown
- **WHEN** the engulfing-pattern lesson is completed
- **THEN** the player SHALL have seen at least one engulfing pattern followed by an adverse move

### Requirement: Progress is never destroyed
Liquidations, losses, bankruptcy (game over) or failed survival challenges SHALL NOT remove purchases, unlocked content or permanent progress; they MAY end the current mission or campaign run.

#### Scenario: Liquidated in campaign
- **WHEN** a player is liquidated in a campaign mission
- **THEN** the mission MAY be failed but previously unlocked stages SHALL remain unlocked

#### Scenario: Campaign run ended
- **WHEN** a campaign run ends by bankruptcy or by the player's choice
- **THEN** every stage passed before SHALL stay passed, and the next campaign run SHALL keep its unlocks

### Requirement: Next lesson recommendation
After a debrief, the system SHALL recommend a lesson mapped from observable errors (trigger vs execution confusion, oversizing, stop beyond liquidation, ignoring funding, ignoring theta/IV, trading without a plan).

#### Scenario: Stop beyond liquidation
- **WHEN** a run had a liquidation with a stop beyond the liquidation price
- **THEN** the recommended lesson SHALL be Futures II

#### Scenario: Ignoring theta or IV
- **WHEN** a run lost money on options and theta, or vega, explains at least half of that loss in the Greek attribution, with no liquidation, stop beyond liquidation, margin call, unprotected or oversized entry, or bankruptcy
- **THEN** the debrief SHALL say which Greek cost the money and the recommended lesson SHALL be Options II (IV and Greeks)

### Requirement: Phase-2 mission graph
The built-in curriculum SHALL assess its stages with twelve missions (design D8.4):

| Stage | Missions |
|---|---|
| Foundations | none (Academy lesson only) |
| Structure | m06 |
| Orders | m06 |
| Futures I | m01 |
| Futures II | m03 |
| Futures III | m02 |
| Risk sizing | m07 |
| Options I | m04 |
| Options II | m04 and m09 |
| Options III | m05 and m10 |
| Patterns & Indicators | m08 |
| Events | m11 |
| Evaluation | m12 |

The unlocks stay with Futures I (shorting), Futures II (leverage above 2×), Options I (buying options) and Options III (writing options). The campaign SHALL play m06, m01, m03, m02, m07, m04, m09, m05, m10, m08, m11, m12.

A mission that comes before an unlock's stage SHALL NOT allow that unlock in its own engine rules, so that every campaign lock is enforced by the engine without new start options:
- m06 allows no shorting and has a 2× cap;
- m09 allows no option writing.

The first mission of the campaign SHALL carry the introductory tutorial cards (reading the chart; plan, advisor and points).

#### Scenario: Campaign starts with structure and orders
- **WHEN** a player starts a new campaign run
- **THEN** its first mission SHALL be m06, long only with a 2× cap, and its tutorial SHALL introduce the chart, the plan and the advisor

#### Scenario: A stage with two missions
- **WHEN** a player has finished m04 but not m09 in a campaign run
- **THEN** Options I SHALL be passed, Options II SHALL stay open and Options III SHALL stay locked

#### Scenario: No lock the engine cannot enforce
- **WHEN** m09 is played in a campaign run before Options III is passed
- **THEN** the briefing SHALL list option writing as locked and the engine SHALL reject writing an option, because m09's own option rules forbid it

#### Scenario: Capstone last
- **WHEN** every stage before Evaluation is passed
- **THEN** Evaluation SHALL be open and m12 SHALL be the next campaign mission
