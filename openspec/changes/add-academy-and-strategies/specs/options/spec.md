## ADDED Requirements

### Requirement: Report-only what-if valuation
The engine SHALL offer two what-if valuations, through the C ABI and every binding:
- one without a run, from explicit legs (kind, side, quantity, strike, time to expiry, implied volatility, entry price; an optional perpetual leg);
- one for a run, from series ids and the perpetual, using the run's IV model with the volatility base published at the current step.

Both SHALL:
- take a shifted forward, an elapsed time and an IV shift in volatility points (floored at 5% per leg);
- return, per leg and for the position, the model value, the P&L against the entries and delta, gamma, theta and vega;
- when every option leg shares one expiry, return the breakevens, the maximum profit and loss at expiry (`null` when unbounded; prices never go below 0) and the payoff curves at expiry and at the shifted time.

They SHALL use the same kernel and quantization as the quotes (design D4), SHALL NOT change any run, log, checkpoint or digest, and SHALL use no data after the current step.

#### Scenario: Agrees with the quote (OPT05)
- **WHEN** the run what-if is computed with no shift for a listed series at a decision step
- **THEN** its value rounded half-even to the premium tick SHALL equal the quote's mid, and its delta, gamma, theta and vega SHALL equal the quote's bit for bit, on every target

#### Scenario: Exact expiry analytics
- **WHEN** the legs are an iron condor 90/95/105/110 entered for a net credit of 3 per unit
- **THEN** the breakevens SHALL be 92 and 108, the maximum loss 2 per unit and the maximum profit 3 per unit, each times the units

#### Scenario: No look-ahead (OPT06)
- **WHEN** the bars and the IV base after step `s` are replaced
- **THEN** the run what-if at step `s` SHALL be unchanged for every shift

#### Scenario: Pure
- **WHEN** both what-if calls run on an active run
- **THEN** the run's state digest and event log SHALL be unchanged

### Requirement: Combo dry run
The engine SHALL offer a dry run of an ordered list of option and perpetual market orders against a run's current state. For each leg it SHALL return accepted or the rejection reason the real command would return, the quote it would cross, the premium, fee and margin reserved, and the available balance after it, plus totals. It SHALL apply the same validation and margin functions as the commands, on a copy of the state, and SHALL leave the run unchanged.

#### Scenario: Matches real placement
- **WHEN** a list of legs is dry-run and then placed for real at the same step in the same order
- **THEN** each leg's acceptance or reason, and the available balance after it, SHALL be equal

#### Scenario: State untouched
- **WHEN** a dry run is made
- **THEN** the run's state and its state digest SHALL be identical before and after

### Requirement: Defined-risk spread margin
A scenario's option spec MAY declare `spread_margin`. When it does, each short option position SHALL be paired, greedily in series-id order, with long positions of the same kind and expiry. The margin of a paired quantity SHALL be the smaller of:
- its per-leg margin;
- its worst loss at expiry per unit, times the units: for calls max(0, K_long − K_short), for puts max(0, K_short − K_long).

Within one expiry, when every losing put pair's short strike is at or below every losing call pair's short strike, the two sides SHALL be margined at the larger of their sums, since both cannot lose at once. Otherwise they are margined at their total.

At acceptance, working buy orders placed earlier at the same step SHALL count as longs for pairing. At execution only filled longs SHALL count. Without the flag, margin SHALL be computed per leg exactly as before, and every existing golden SHALL be unchanged. The briefing's mission rules SHALL state which margin rule applies.

#### Scenario: Debit spread needs no margin
- **WHEN** a mission with `spread_margin` holds a long 100 call and a short 105 call of the same expiry and quantity
- **THEN** the short call's margin SHALL be zero

#### Scenario: Credit spread margined at its width
- **WHEN** a mission with `spread_margin` holds a short 105 call and a long 110 call of the same expiry and quantity 2 with multiplier 1
- **THEN** the short call's initial and maintenance margin SHALL each be at most 10

#### Scenario: Iron condor at its larger side
- **WHEN** a mission with `spread_margin` holds the iron condor 90/95/105/110 at quantity 1
- **THEN** the margin of its short legs SHALL be 5, not 10

#### Scenario: Cover not filled
- **WHEN** a short call's protective long was cancelled at the open before the short executes
- **THEN** the short SHALL be checked against its per-leg margin and cancelled with `insufficient_margin` if that does not fit

#### Scenario: Flag off
- **WHEN** m05 and m10 are replayed from their goldens
- **THEN** every digest SHALL be unchanged
