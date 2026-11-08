## ADDED Requirements

### Requirement: Markets of m18–m25 from existing capabilities
The markets of m18–m25 and of the round markets SHALL be built from the generator's existing capabilities (generator v5). A missing capability MAY be added only as a per-mission capability that changes no market that does not declare it. It SHALL then raise the generator version, which is not a digest input, and be recorded as an "As built" decision. Each new mission and round market SHALL add its own golden, and every earlier golden SHALL keep its event digest.

#### Scenario: Earlier markets unchanged
- **WHEN** m01–m17 are generated after m18–m25 are added
- **THEN** every bar, funding rate and IV base SHALL be identical

### Requirement: Learnability bands of m18–m25
Over seeds 1–60, the reference players of design D12 SHALL meet these bands, and the D8.3 rules SHALL hold (buy-and-hold positive in less than 75%; every strategy that can lose loses in at least 15%, except payoff properties):
- **m18:** the band-hedged straddle makes money in ≥ 70% of seeds where realized/implied ≥ 1.2 and loses in ≥ 70% where it is ≤ 0.85; its standard deviation is ≤ 0.6× the unhedged straddle's; hedging every bar pays ≥ 2× the fees of band hedging.
- **m19:** the hedged carry nets positive in ≥ 75% of seeds without a flip and negative in ≥ 60% of flip seeds; at 5× the perpetual leg is liquidated in ≥ 25% of squeeze seeds and at 2× in ≤ 5%.
- **m20:** IV after the release is ≤ 0.7× IV the bar before in every seed; buying early and selling one bar before beats holding through in ≥ 60%; holding through wins in ≥ 85% of seeds where the move exceeds the straddle's price and loses in ≥ 80% of the others.
- **m21:** the loss at the near expiry is ≤ the net debit plus costs in every seed; the calendar wins in ≥ 70% when the price ends within one strike step of the strike and loses in ≥ 75% when it ends beyond three steps.
- **m22:** the managed plan has a lower standard deviation of R and a win rate at least 10 points higher than all-in/all-out, which has the higher mean R in ≥ 60% of trend-far seeds.
- **m23:** a cooled-down re-entry loses less than an immediate double-size re-entry in ≥ 70%.
- **m24:** a planned range-break entry beats a chase entry in R in ≥ 65%, and the chase wins in ≥ 15%.
- **m25:** a single planned entry beats averaging down to the same stop in ≥ 65%; averaging down wins in ≥ 20%, and its worst seed is at least twice as bad.

A band SHALL change only through a recorded "As built" decision.

#### Scenario: Bands enforced
- **WHEN** a generator or mission change makes any m18–m25 band fail
- **THEN** the learnability test SHALL fail

### Requirement: Round markets
The engine SHALL provide four round markets (trend, range, breakout, event), listed with `"practice": "round"`. Menus, the campaign, the curriculum and the daily pool SHALL skip them. They SHALL pass the pooled realism bands, so that no single indicator predicts the next 12 bars.

#### Scenario: Not a mission
- **WHEN** the mission menu and the campaign are built
- **THEN** no round market SHALL appear in them
