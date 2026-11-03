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
- **THEN** the same RSI rules SHALL win 50% ± 5% of the time

#### Scenario: Expected news is priced in
- **WHEN** an event's actual value equals its expectation
- **THEN** it SHALL NOT move the fair value, and price 25 bars later SHALL be up less often than after a positive surprise

#### Scenario: Breakout direction is not guaranteed
- **WHEN** a random-direction breakout follows a credible positive surprise
- **THEN** it SHALL go up in more than 60% and less than 85% of seeds

### Requirement: Funding and IV dynamics
Synthetic scenarios SHALL generate a funding-rate series from a declared rule linking it to recent drift and positioning parameters, and an IV base level linked to the generator's volatility state plus a declared premium, both computed only from state up to the current step.

#### Scenario: Funding squeeze regime
- **WHEN** a scenario declares a crowded-long regime
- **THEN** generated funding SHALL be positive on average during that regime within the declared bounds

### Requirement: Events
Each scripted event SHALL declare release step, lead time, credibility, prior expectation, surprise, impact channels (drift, volatility, jumps, volume, spread, capacity, funding, IV), intensity distribution and duration. Reactions SHALL be sampled from seeded distributions that include no reaction, reversal and "buy the rumor, sell the news" outcomes, and SHALL NOT be determined by the event text's sentiment alone.

#### Scenario: Same event, different reactions
- **WHEN** the same positive-surprise event is generated under 1,000 seeds
- **THEN** the share of negative next-bar returns SHALL be greater than zero and SHALL match the declared distribution within tolerance

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
