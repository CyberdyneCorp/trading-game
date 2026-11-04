# Spec Delta

## Purpose

Defines the MVP technical indicators with exact, versioned formulas and warm-up rules so that every platform shows the same values and never displays an indicator before it is meaningful.

## ADDED Requirements

### Requirement: MVP indicator set
The engine SHALL provide SMA, EMA, RSI (Wilder smoothing), ATR (Wilder smoothing), session VWAP, Bollinger bands (SMA middle ± k population standard deviations, integer square root), MACD (line, signal, histogram), ADX with +DI/−DI (Wilder smoothing) and raw volume, each identified by name, parameters (period, price input) and an indicator version (currently 3). Changing a formula SHALL bump the indicator version. The run state exposed through the C ABI SHALL carry the indicator version with SMA(20), EMA(50), RSI(14), ATR(14), VWAP (24-bar sessions), Bollinger(20, 2), MACD(12, 26, 9) and ADX(14) with +DI/−DI, computed over closed bars up to the decision step.

#### Scenario: Indicator identity
- **WHEN** a run records an EMA(20, close) value
- **THEN** the run SHALL also record the indicator version used to compute it

### Requirement: Candle patterns
The engine SHALL detect candlestick patterns (single: doji, hammer, hanging man, inverted hammer, shooting star, marubozu; double: engulfing, harami, piercing line, dark cloud cover, tweezers, kicker; triple: morning/evening star, three white soldiers, three black crows, three inside/outside up/down, abandoned baby; five-bar: rising/falling three methods) with integer comparisons on closed bars, reporting each at the bar that completes it with its length and bias. Context-dependent patterns SHALL use the trend before the pattern (close before it versus five bars earlier, against the prior 10-bar average body), so the same shape can be a hammer after a decline and a hanging man after a rally. Pattern rules are versioned (`patternVersion`). The generator's calibration SHALL keep pattern reliability honest and stable, measured over 300 generated markets (every mission × 60 seeds) as the share of directional patterns whose close five bars later moved in the pattern's direction: isolated patterns 45–60% (close to chance, matching the cheat sheet's warning); patterns completing with RSI(14) below 30 (bullish) or above 70 (bearish) 45–62%; kickers, which come from news gaps, 40–70%: news is priced in the gap, so what follows it is close to chance.

#### Scenario: Same shape, different context
- **WHEN** a long-lower-shadow candle completes after a rally
- **THEN** it SHALL be reported as a hanging man and not as a hammer

#### Scenario: No lookahead
- **WHEN** the bar after `t` is altered
- **THEN** the patterns reported up to `t` SHALL be unchanged

#### Scenario: Pattern reliability calibration
- **WHEN** patterns are detected over 300 generated markets and scored five bars later
- **THEN** isolated patterns SHALL hit 45–60% of the time, patterns with an extreme RSI 45–62%, and kickers 40–70%

### Requirement: Warm-up
An indicator SHALL emit no value until it has received at least its declared warm-up number of bars (SMA: period; EMA: period, seeded with the SMA of the first period; RSI and ATR: period + 1; VWAP: 1; Bollinger: period; MACD line: slow period; MACD signal and histogram: slow + signal − 1, the signal seeded with the SMA of the first `signal` line values; +DI/−DI: period + 1; ADX: 2 × period, seeded with the mean of the first `period` DX values). Before warm-up the value SHALL be absent (`null` in the C ABI series), and the number of remaining bars SHALL follow from the declared warm-up and the bars received. Scenario warm-up bars before the first decision point SHALL count toward warm-up but SHALL NOT be tradable.

#### Scenario: VWAP session reset
- **WHEN** a bar starts a new VWAP session (every 24 hourly bars from bar 0)
- **THEN** VWAP at that bar SHALL equal that bar's typical price (H+L+C)/3

#### Scenario: No value before warm-up
- **WHEN** an RSI(14) has received 10 closed bars
- **THEN** its value SHALL be absent (`null`) and its first value SHALL appear after 5 more bars

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
