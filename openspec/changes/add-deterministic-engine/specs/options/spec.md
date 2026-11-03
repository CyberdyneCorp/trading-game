# Spec Delta

## Purpose

Defines European, cash-settled call and put options on the scenario's perpetual price, priced by a deterministic model so players can learn premiums, implied volatility, Greeks, buying and writing without licensed option-chain data.

## ADDED Requirements

### Requirement: Option series
A scenario MAY define option series, each with type (call or put), strike, expiry step, contract multiplier, premium tick size and lot size. Series SHALL become listed at a scenario-defined step, and strikes SHALL be expressed in the masked price space. All options SHALL be European and cash-settled; there is no early exercise.

#### Scenario: Series not yet listed
- **WHEN** the player submits an order on a series whose listing step is in the future
- **THEN** the order SHALL be rejected with reason `series_not_listed`

### Requirement: Model pricing
The model mid price of an option SHALL be computed with Black-76 using the perpetual's last close as forward, zero interest rate, time to expiry derived from remaining steps and the scenario's step duration, and volatility from the IV model. Bid and ask SHALL be the mid minus/plus a scenario spread (percentage of mid with a minimum of one tick), quantized to the premium tick, never below one tick. The UI-facing label for these prices SHALL state that they are simulated.

#### Scenario: Bit-identical premium (OPT01)
- **WHEN** the same option is priced from the same state on every supported target
- **THEN** mid, bid, ask and Greeks SHALL be bit-identical

### Requirement: Implied volatility model
Implied volatility for a series at step `t` SHALL be a function of a base volatility level, term structure, skew and smile in log-moneyness, and active event shocks. In historical replay the base level SHALL come from a scenario volatility-index series (e.g. DVOL) using the latest value whose availability time is at or before bar `t`, and when no such series exists (e.g. SOL) from realized volatility of closes up to bar `t` with a declared window and a declared premium factor, flagged `ivFromRealizedVol`; in synthetic mode it MAY be derived from the generator's volatility state at `t`. The IV model SHALL NOT use any data after bar `t`.

#### Scenario: No future in IV (OPT04)
- **WHEN** bars after `t` are replaced with a volatility spike
- **THEN** every option quote at or before step `t` SHALL be unchanged

### Requirement: Greeks
At each decision point the engine SHALL expose delta, gamma, theta (per step) and vega (per one volatility point) for every listed series and for the player's option positions, computed from the same model inputs as the premium and quantized to a declared scale.

#### Scenario: Greeks available with quote
- **WHEN** an option chain is displayed at a decision point
- **THEN** each series SHALL have premium and all four Greeks from the same model state

### Requirement: Buying options
Buying an option SHALL debit premium plus fees from wallet balance at fill and SHALL require no margin. The maximum loss of a long option position SHALL be the premium paid plus fees. Long options SHALL be marked at model bid for equity.

#### Scenario: Long option loss bounded
- **WHEN** a long call expires out of the money
- **THEN** the realized loss SHALL equal premium plus fees exactly

### Requirement: Writing options
Writing (selling) an option SHALL be allowed only when the mission enables short options. The seller SHALL receive premium minus fees and SHALL post option margin per the scenario margin formula, which SHALL at least cover the cost to buy back at model ask plus a percentage of the underlying adjusted for moneyness. Initial margin SHALL be checked at order acceptance; maintenance margin SHALL be checked every step.

#### Scenario: Short options disabled
- **WHEN** the mission does not enable short options and the player sells a call without holding a long in the same series
- **THEN** the order SHALL be rejected with reason `short_options_not_allowed`

### Requirement: Short option margin call and liquidation
When account equity excluding isolated perp margin falls below total option maintenance margin after a step, the engine SHALL emit a `margin_call` event and SHALL buy back short options at model ask, largest maintenance margin first, until the requirement is met or no short options remain. The account wallet balance SHALL never become negative; any remaining shortfall SHALL be absorbed by the simulated insurance fund and reported in the debrief.

#### Scenario: Adverse move on a short call (OPT03)
- **WHEN** a written call moves deep in the money and equity drops below option maintenance margin
- **THEN** a `margin_call` event SHALL be emitted and the short call SHALL be bought back at model ask
- **AND** wallet balance SHALL remain non-negative

### Requirement: Expiry and settlement
Settlement SHALL follow the venue convention closest to reality: the settlement price `S` is the time-weighted average price (TWAP) of the underlying over a scenario-declared settlement window ending at the expiry instant (default: 30 minutes of market time). The engine SHALL compute the TWAP from the finest resolution present in the scenario (sub-bars or ticks when licensed and included); when only bars coarser than the window are available, it SHALL use the arithmetic mean of the closes of the bars covering the window, or the expiry bar's close when the window is shorter than one bar, and SHALL flag the settlement `settlementApproximated`. At the expiry step the engine SHALL settle all positions in that series in cash at intrinsic value: calls pay `max(0, S − K)` and puts pay `max(0, K − S)` per unit, times multiplier. Out-of-the-money series SHALL expire worthless. Open orders in the series SHALL be cancelled with reason `expired`, and released option margin SHALL return to available balance. Orders in an expiring series SHALL be rejected from the start of the settlement window with reason `series_settling`.

#### Scenario: ITM and OTM expiry (OPT02)
- **WHEN** a long put with strike 100 expires with settlement 92 and a long call with strike 100 expires in the same step
- **THEN** the put SHALL credit 8 × quantity × multiplier and the call SHALL expire worthless

#### Scenario: TWAP settlement from sub-bars
- **WHEN** a scenario has 1-minute sub-bars and a 30-minute settlement window whose 30 closes average 101.5
- **THEN** the settlement price SHALL be 101.5 (quantized to tick) and SHALL NOT be flagged approximated

#### Scenario: Coarse bars
- **WHEN** a scenario only has 1-hour bars and the settlement window is 30 minutes
- **THEN** the settlement price SHALL be the expiry bar's close and the settlement SHALL be flagged `settlementApproximated`
