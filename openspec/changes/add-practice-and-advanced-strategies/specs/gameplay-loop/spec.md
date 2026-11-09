## ADDED Requirements

### Requirement: Strategy comparison of m18–m22
The debrief of m18–m22 SHALL include the report-only strategy comparison, computed by the engine after the run on this seed:
- **m18:** the unhedged straddle, hedged every 4 bars, and hedged on a delta band.
- **m19:** the unhedged short, the hedged carry at 5× and at 2× (naming a liquidation of the perpetual leg).
- **m20:** buy early and sell one bar before the release, buy early and hold through, buy one bar before and hold through, and a short straddle through the release where writing is allowed.
- **m21:** the at-the-money calendar, a diagonal one strike up, and a near short straddle.
- **m22:** all-in and all-out at 2R, the managed exit (half at +1R, stop to breakeven, trail the rest), and holding with the initial stop.

Delta hedges SHALL be perpetual orders that bring the net delta to zero, rounded toward zero to the lot, with the engine's costs.

#### Scenario: Hedged rows use real costs
- **WHEN** m18's comparison runs the band-hedged straddle
- **THEN** each hedge SHALL be an executed perpetual order whose fees and slippage are in the row's net P&L

### Requirement: Debrief views of biases and episodes
The debrief SHALL show each bias finding with its evidence (steps, sizes, prices) and a link to the moment on the run's chart, and SHALL show the position episodes with their partial exits and stop moves. In m23–m25 the objectives SHALL read these facts (met or missed). A psychology mission's briefing and tutorial SHALL NOT name the bias its market tempts.

#### Scenario: Bias with evidence
- **WHEN** an m25 run adds to a losing long beyond its planned risk
- **THEN** the debrief SHALL name averaging down, show the average entry before and after and the planned risk before and after, and mark the objective missed
