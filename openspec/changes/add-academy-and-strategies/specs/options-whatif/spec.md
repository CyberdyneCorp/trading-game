## ADDED Requirements

### Requirement: What-if lab
The client SHALL offer an options what-if lab for a position of one or more option legs on one expiry, optionally with a perpetual leg. Sliders for the underlying price, the time elapsed toward expiry and an implied-volatility shift (in volatility points) SHALL update:
- the position's model value and P&L today;
- the P&L curve at expiry;
- the P&L curve at the slider's time and IV;
- each leg's and the position's delta, gamma, theta and vega.

The lab SHALL state that the pricing model is Black-76 at a zero interest rate and SHALL offer no rate input.

#### Scenario: Time slider to expiry
- **WHEN** the time slider reaches the expiry
- **THEN** the "today" curve SHALL coincide with the expiry payoff, and theta, gamma and vega SHALL be zero

#### Scenario: IV shift floored
- **WHEN** the IV shift would take a leg's implied volatility below 5%
- **THEN** the lab SHALL use 5% for that leg and show the floored value

### Requirement: Engine numbers only
Every value, P&L, Greek, breakeven and maximum profit or loss the lab shows SHALL come from the engine's pricing kernel through the what-if calls. The client SHALL only format them. For the same inputs the web and the desktop SHALL show the same numbers.

#### Scenario: Shared fixture
- **WHEN** the `whatif` golden fixtures are replayed by the native engine, WASM, Python and the desktop
- **THEN** every output string SHALL equal the fixture's expected string

### Requirement: Lab in the terminal agrees with the chain
In a run, the lab SHALL start from the player's open option positions or the builder's legs. It SHALL use the run's own implied-volatility model with the volatility base published at the current step (never a later one). With every slider at zero, each leg's value rounded to the premium tick SHALL equal that series' chain mid, and its Greeks SHALL equal the chain's. The lab SHALL say that its numbers assume today's volatility level.

#### Scenario: Zero shift equals the chain
- **WHEN** the lab opens on a position at a decision step with no shift
- **THEN** each leg's value SHALL round to the chain's mid and its delta, gamma, theta and vega SHALL equal the chain's

#### Scenario: No look-ahead
- **WHEN** the bars and the volatility base after the current step are replaced
- **THEN** the lab's numbers at the current step, for any slider position, SHALL be unchanged

### Requirement: Lab in the Academy
The Academy SHALL offer the lab with editable legs (kind, side, strike, quantity, days to expiry, IV) on an illustrative market, and lessons SHALL be able to embed it with chosen sliders unlocked.

#### Scenario: Lesson unlocks one slider
- **WHEN** a lesson step embeds the lab with only the IV slider unlocked
- **THEN** the price and time sliders SHALL be shown fixed and only IV SHALL be movable
