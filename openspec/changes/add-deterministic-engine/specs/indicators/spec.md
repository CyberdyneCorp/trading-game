# Spec Delta

## Purpose

Defines the MVP technical indicators with exact, versioned formulas and warm-up rules so that every platform shows the same values and never displays an indicator before it is meaningful.

## ADDED Requirements

### Requirement: MVP indicator set
The engine SHALL provide SMA, EMA, RSI (Wilder smoothing), ATR (Wilder smoothing) and raw volume, each identified by name, parameters (period, price input) and an indicator version. Changing a formula SHALL bump the indicator version.

#### Scenario: Indicator identity
- **WHEN** a run records an EMA(20, close) value
- **THEN** the run SHALL also record the indicator version used to compute it

### Requirement: Warm-up
An indicator SHALL emit no value until it has received at least its declared warm-up number of bars (SMA: period; EMA: period, seeded with the SMA of the first period; RSI and ATR: period + 1). Before warm-up the state SHALL report `warming_up` with the number of remaining bars. Scenario warm-up bars before the first decision point SHALL count toward warm-up but SHALL NOT be tradable.

#### Scenario: No value before warm-up
- **WHEN** an RSI(14) has received 10 closed bars
- **THEN** its value SHALL be absent and its state SHALL be `warming_up` with 5 bars remaining

### Requirement: Gaps in data
When the scenario marks a bar as a synthetic continuity bar (no trading), indicators SHALL apply the scenario's declared gap policy (skip or carry forward), and such bars SHALL provide zero executable capacity.

#### Scenario: Continuity bar
- **WHEN** a continuity bar occurs during an open market order
- **THEN** no fill SHALL occur in that bar and indicators SHALL follow the declared gap policy

### Requirement: Closed-bar only
Indicator values exposed at decision step `t` SHALL be computed from closed bars up to and including `t` only.

#### Scenario: Indicator independent of next bar
- **WHEN** bar `t+1` is altered
- **THEN** all indicator values at step `t` SHALL be unchanged
