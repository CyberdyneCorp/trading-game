# Spec Delta

## Purpose

Defines the client-independent mission flow: briefing, observing, planning, acting or waiting, advancing, and the debrief that separates decision quality from outcome.

## ADDED Requirements

### Requirement: Mission briefing
Before the first decision point, the player SHALL be shown virtual capital, costs (fees, spread, funding interval), leverage cap, allowed instruments (long, short, options buying, options writing), objectives, mode (historical/synthetic/branch) and the market alias.

#### Scenario: Instrument permissions visible
- **WHEN** a mission disables options writing
- **THEN** the briefing SHALL state it and the order ticket SHALL not offer selling options to open

### Requirement: Time stops at decisions
The market SHALL advance only when the player explicitly advances (one step or a declared batch). Submitting an order, closing a position and advancing SHALL be distinct actions.

#### Scenario: No implicit advance
- **WHEN** the player submits an order and does nothing else
- **THEN** the market SHALL stay at the same step and the order SHALL remain `accepted`

### Requirement: Plan before advance
When the player opens or increases a position, the client SHALL request a structured plan (hypothesis direction, invalidation level, target or exit rule, confidence, planned risk); for options also the scenario in which the trade loses. The plan SHALL be recorded with the decision step before the next advance. Skipping the plan SHALL be allowed but SHALL be recorded and SHALL make plan-coherence non-applicable for that trade rather than scored.

#### Scenario: Plan recorded before outcome
- **WHEN** the player records a plan and then advances
- **THEN** the plan SHALL be immutable and timestamped with the step at which it was made

### Requirement: Waiting is a decision
Advancing without trading SHALL be recorded as a `wait` decision and SHALL receive feedback in the debrief. No mission objective SHALL require a minimum number of trades.

#### Scenario: No trade when no hypothesis (EDU02)
- **WHEN** a player completes a mission without trading
- **THEN** the score SHALL NOT be reduced for lack of turnover

### Requirement: Liquidation warnings
Clients SHALL warn at decision points when a position's liquidation price is within a mission-declared distance (in ATR or percent) of the last close, and SHALL warn on the order ticket when a stop lies beyond the liquidation price.

#### Scenario: Stop beyond liquidation warning
- **WHEN** a player places a long stop below the liquidation price
- **THEN** the ticket SHALL show a warning explaining liquidation will happen first and require explicit confirmation

### Requirement: Game over on bankruptcy
When the engine reports `account_bankrupt`, the client SHALL end the mission with a game-over screen that leads directly to the debrief. In Campaign, virtual capital SHALL carry over between the missions of a campaign run, and bankruptcy SHALL end the whole campaign run; a new campaign run SHALL start from the campaign's initial capital. Game over SHALL NOT remove completed lessons, unlocked stages, cosmetics or purchases.

#### Scenario: Bankrupt mid-campaign
- **WHEN** the player goes bankrupt in mission 4 of a campaign run
- **THEN** the campaign run SHALL end with a game-over screen and debrief
- **AND** a new campaign run SHALL be offered with initial capital and all previously unlocked stages intact

### Requirement: Assisted practice
Rewinding or changing seed SHALL create a new attempt flagged `assisted`, SHALL preserve the original attempt unchanged and SHALL exclude assisted attempts from non-assisted progress metrics.

#### Scenario: Rewind keeps original
- **WHEN** the player rewinds a completed run
- **THEN** the original run and its score SHALL remain available and unchanged

### Requirement: Debrief
On completion the debrief SHALL show: market reveal (if permitted) and historical/synthetic label; timeline of plans, news, orders, fills, funding, margin changes, liquidations and expiries; net P&L split into realized trading P&L, fees, spread/slippage, funding and option premium/settlement; max drawdown, peak leverage, minimum liquidation distance, turnover; requested vs trigger vs executed price per fill; `ambiguousIntrabar` and capacity approximations; comparison with staying flat and with a 1× long held from the first eligible bar; for options, an approximate P&L attribution to delta, gamma, theta and vega, labeled approximate; separation of good process, favorable luck and items needing more samples; a recommended next lesson based on observable errors.

#### Scenario: Good process, negative result (EDU01)
- **WHEN** a breakout fails and a planned stop limits the loss within planned risk
- **THEN** the debrief SHALL credit risk management and SHALL NOT label the decision as a mistake solely due to the loss

#### Scenario: Liquidation explained
- **WHEN** a run included a liquidation
- **THEN** the debrief SHALL show leverage, margin, liquidation price, the bar that triggered it and what leverage would have kept the planned stop in front of liquidation
