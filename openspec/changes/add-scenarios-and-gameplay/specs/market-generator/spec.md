# Spec Delta

## Purpose

Defines the deterministic synthetic market generator used for event-lab scenarios, counterfactual branches and campaign regimes, including price, volume, funding and implied-volatility dynamics.

## ADDED Requirements

### Requirement: Regime-based price process
The generator SHALL produce positive OHLCV bars from a seeded return process with discrete regimes (at least trend-up, trend-down, range, high-volatility, low-liquidity), stochastic volatility and jumps. Regime parameters and transition rules SHALL be declared in the scenario and versioned.

#### Scenario: Positive, valid bars
- **WHEN** any seed is used
- **THEN** every generated bar SHALL satisfy low ≤ open, close ≤ high and low > 0

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
