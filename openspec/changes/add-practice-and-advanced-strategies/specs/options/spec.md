## ADDED Requirements

### Requirement: Calendar margin declared per mission
When a mission declares `calendar_margin` (valid only with `spread_margin`), spread-margin pairing SHALL also let a long option of the same kind whose expiry is at or after the short's expiry cover the short.
- The requirement per paired unit SHALL be max(0, K_long − K_short) for calls and max(0, K_short − K_long) for puts.
- Candidates SHALL be tried same expiry first, then later expiries ascending, then by series id.
- A long that expires before the short SHALL never cover it.
- With the flag off, margin SHALL be identical to the spread-margin rule of add-academy-and-strategies.
- The state view SHALL report `missionRules.calendarMargin`.

#### Scenario: Calendar short needs no margin
- **WHEN** a mission with calendar margin holds a short near 100 call and a long far 100 call of equal quantity
- **THEN** the short's paired margin SHALL be 0

#### Scenario: Diagonal bounded by the strike gap
- **WHEN** the holdings are a short near 100 call and a long far 105 call of 10 units
- **THEN** the paired requirement SHALL be at most 5 × 10

#### Scenario: Flag off unchanged
- **WHEN** m13–m17 are replayed with their goldens
- **THEN** every option book and digest SHALL be unchanged

### Requirement: Multi-expiry what-if analytics
When the option legs of a what-if valuation have different expiries, the reply SHALL include `horizon`: the position at the first expiry.
- Legs expiring then SHALL be at intrinsic value. Later legs SHALL be at model value with the run's IV model and the current step's base, or with the run-less leg's own IV plus the shift.
- `horizon` SHALL include the curve, the breakevens (on the price tick grid, the profitable-side tick nearest each crossing), `maxProfit`, `maxLoss` (`null` when the asymptotic slope above every strike is unfavorable) and `unboundedAbove`.
- It SHALL use no data after the current step and SHALL NOT change any run, log or digest.

#### Scenario: Calendar loss bounded by the debit
- **WHEN** the legs are a short near 100 call entered at 2.00 and a long far 100 call entered at 3.50 (a net debit of 1.50 per unit)
- **THEN** `horizon.maxLoss` SHALL be at most 1.50 per unit times the units

#### Scenario: Breakevens against brute force
- **WHEN** the horizon breakevens of a calendar are compared with a scan of every price tick
- **THEN** they SHALL be equal

### Requirement: P&L heatmap
A what-if request MAY ask for a `heatmap` of up to 61 prices by 25 times. The reply SHALL give the position's P&L against its entries at each price and time:
- legs past their expiry at intrinsic value;
- the other legs valued as in the run what-if, with today's IV base and no look-ahead.

#### Scenario: Time zero equals today
- **WHEN** a heatmap row is computed at zero elapsed time
- **THEN** each cell SHALL equal the `today` curve at the same price

### Requirement: Valuation of the run's holdings
A run what-if request with `holdings: true` SHALL value the run's own perpetual and option positions at their average entries. The reply SHALL add a `risk` block:
- net delta in units and notional;
- the P&L from gamma of a 1% move;
- vega per volatility point;
- theta per bar;
- the next funding payment estimate;
- each of these in basis points of equity;
- the mission's risk limits with `exceeded` flags.

Amounts SHALL be `Money` at 1e-8, rounded half-even, and basis points int32, rounded half-even.

#### Scenario: Holdings equal explicit legs
- **WHEN** the holdings what-if and a what-if of the same positions passed as explicit legs run at the same step
- **THEN** their per-leg and net values and Greeks SHALL be equal

### Requirement: Limit legs in the combo dry run
The combo dry run SHALL accept per-leg `type` (market or limit), `premium` and `tif`, and SHALL apply them with the same command functions as a placement, reporting each leg's acceptance or reason without changing the run.

#### Scenario: Matches a real placement
- **WHEN** a limit iron condor with `ioc` is dry-run and then placed at the same step
- **THEN** every leg's acceptance, reason and reserved amounts SHALL match, and the dry run SHALL leave the state digest unchanged
