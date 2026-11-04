# Spec Delta

## Purpose

Defines the deterministic synthetic market generator used for event-lab scenarios, counterfactual branches and campaign regimes, including price, volume, funding and implied-volatility dynamics.

## ADDED Requirements

### Requirement: Regime-based price process
The generator SHALL produce positive OHLCV bars from a seeded return process with discrete regimes (at least trend-up, trend-down, range, high-volatility, low-liquidity), stochastic volatility and jumps. Regime parameters and transition rules SHALL be declared in the scenario and versioned.

#### Scenario: Positive, valid bars
- **WHEN** any seed is used
- **THEN** every generated bar SHALL satisfy low ≤ open, close ≤ high and low > 0

### Requirement: Readable market structure
The generator SHALL provide, per segment, a hidden fair value moved by fundamental drift, information jumps and news surprises weighted by credibility; range anchors with support/resistance walls; trend anchors with pullbacks to the average; momentum; volatility compression; segments whose direction is drawn per seed and tilted toward the fair value; and crowding squeezes against the side paying extreme funding. The resulting edges SHALL be probabilistic: under the generator's calibration tests, setups that match their regime SHALL win more often than chance and never with certainty, and a structure-free random walk SHALL show no edge.

#### Scenario: Range fade edge is modest
- **WHEN** a range segment is generated for 200 seeds and the player buys RSI(14) < 30 or sells RSI(14) > 70
- **THEN** the share of trades in profit five bars later SHALL lie between 58% and 72%

#### Scenario: No edge without structure
- **WHEN** a segment with no anchor and no fair-value pull is generated for 200 seeds
- **THEN** the same RSI rules SHALL win close to 50% of the time (the calibration test accepts roughly 35%–66%)

#### Scenario: Trend pullbacks, not tops
- **WHEN** a Trend-anchored uptrend segment is generated for 200 seeds
- **THEN** buying a close below EMA(20) SHALL be up five bars later in more than 57% and less than 72% of cases, and shorting RSI(14) > 70 SHALL win less than 57% of the time

#### Scenario: Expected news is priced in
- **WHEN** an event's actual value equals its expectation
- **THEN** it SHALL NOT move the fair value, and price 25 bars later SHALL be up less often than after a positive surprise (which SHALL leave price higher 25 bars later in more than 55% and less than 80% of seeds)

#### Scenario: Breakout direction is not guaranteed
- **WHEN** a random-direction breakout follows a compressing range and a credible positive surprise
- **THEN** it SHALL go up in more than 60% and less than 85% of seeds, after a negative surprise in more than 15% and less than 40%, and ATR(14) SHALL on average more than double from the squeeze to the end of the breakout

#### Scenario: Crowded longs get squeezed
- **WHEN** a segment declares a positive funding bias that pushes funding above `squeeze_funding`
- **THEN** bars falling more than 1.5% SHALL be more than three times as frequent as in the same segment without the bias, and these squeeze jumps SHALL NOT move the fair value

### Requirement: Funding and IV dynamics
Synthetic scenarios SHALL generate a funding-rate series from a declared rule linking it to recent drift and positioning parameters, and an IV base level linked to the generator's volatility state plus a declared premium, both computed only from state up to the current step. In generator v2 the rate for bar `i` is `clamp(funding_base + funding_sensitivity × 0.01 × EMA(returns) + funding_bias, ±0.3%)` from state at the close of bar `i−1`, quantized to 1e-6, where `funding_bias` is that of the segment bar `i−1` belonged to (a segment's bias SHALL first show in the rate published at the close of its first bar, never one bar before it starts); the IV base at bar `i` is the annualized, smoothed per-bar variance (segment volatility × compression × √(variance state)) × (1 + iv_premium) × segment and event IV multipliers, capped at 250%.

#### Scenario: Funding squeeze regime
- **WHEN** a scenario declares a crowded-long regime
- **THEN** generated funding SHALL be positive on average during that regime within the declared bounds

#### Scenario: Funding does not announce the next segment
- **WHEN** two scenarios differ only in the funding bias of a later segment
- **THEN** their funding rates SHALL be identical up to and including the rate for that segment's first bar, and differ from the next one

### Requirement: Events
Each scripted event SHALL declare release step, lead time, credibility, prior expectation, surprise, impact channels (drift, volatility, jumps, volume, spread, capacity, funding, IV), intensity distribution and duration. Reactions SHALL be sampled from seeded distributions that include no reaction, reversal and "buy the rumor, sell the news" outcomes, and SHALL NOT be determined by the event text's sentiment alone. The reaction SHALL start on the bar after publication. Independently of the transient reaction, a surprise SHALL move the hidden fair value by `fair_impact × surprise × credibility`; an expected outcome (surprise 0) SHALL leave it unchanged.

#### Scenario: Same event, different reactions
- **WHEN** the reaction of the same positive-surprise event (`p_none` 0.2, `p_align` 0.6) is sampled under 1,000 seeds
- **THEN** about 20% SHALL be no reaction, 48% along the surprise and 32% reversed, each within the test tolerance, so good news can still sell off

### Requirement: Player independence
Generated prices, events and liquidity SHALL NOT depend on the player's positions, orders or score.

#### Scenario: Different trades, same market
- **WHEN** two runs share a scenario and seed but have different commands
- **THEN** their bar series and events SHALL be identical

### Requirement: Branch labeling
In counterfactual branches, every bar after the branch point SHALL be marked synthetic, and clients SHALL display the synthetic label for those bars.

#### Scenario: Branch point
- **WHEN** a branch scenario reaches its branch step
- **THEN** all subsequent bars SHALL carry the synthetic flag

### Requirement: Calibration report
Each generator version SHALL ship a calibration report comparing return distribution, autocorrelation of absolute returns, volatility clustering and drawdown profile with authorized reference data, stating that it is an educational plausibility model, not a predictive one.

#### Scenario: Report required for release
- **WHEN** a new generator version is proposed for packs
- **THEN** it SHALL NOT be usable in published packs without its calibration report
