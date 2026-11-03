# Spec Delta

## Purpose

Defines the learning path from candles and orders through perpetual futures and options, and the rules that unlock riskier instruments and higher leverage only through demonstrated understanding.

## ADDED Requirements

### Requirement: Ordered learning stages
The curriculum SHALL contain stages in this dependency order: Foundations, Structure, Orders, Futures I (contract, long/short), Futures II (margin, leverage, liquidation), Futures III (funding), Risk sizing, Options I (payoffs), Options II (IV and Greeks), Options III (writing and margin), Patterns & Indicators, Evaluation, Events. Each lesson SHALL follow concept, demonstration, decision, feedback and a transfer exercise on a different market.

#### Scenario: Transfer on new market
- **WHEN** a lesson's transfer exercise starts
- **THEN** it SHALL use a scenario window not used by that lesson's demonstration, including warm-up bars

### Requirement: Progressive unlocks
Shorting, leverage caps above 2×, options buying and options writing SHALL be unlocked in Campaign only after completing the corresponding stage's assessment. Unlocks SHALL NOT be purchasable. Sandbox SHALL offer all educational tools with explicit settings, with runs labeled as sandbox.

#### Scenario: Writing locked
- **WHEN** a player who has not passed Options III starts a campaign mission
- **THEN** options writing SHALL be disabled in that mission

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

### Requirement: Next lesson recommendation
After a debrief, the system SHALL recommend a lesson mapped from observable errors (trigger vs execution confusion, oversizing, stop beyond liquidation, ignoring funding, ignoring theta/IV, trading without a plan).

#### Scenario: Stop beyond liquidation
- **WHEN** a run had a liquidation with a stop beyond the liquidation price
- **THEN** the recommended lesson SHALL be Futures II
