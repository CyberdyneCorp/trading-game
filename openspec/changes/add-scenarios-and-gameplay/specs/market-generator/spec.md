# Spec Delta

## Purpose

Defines the deterministic synthetic market generator used for event-lab scenarios, counterfactual branches and campaign regimes, including price, volume, funding and implied-volatility dynamics.

## ADDED Requirements

### Requirement: Regime-based price process
The generator SHALL produce positive OHLCV bars from a seeded return process with discrete regimes (at least trend-up, trend-down, range, high-volatility, low-liquidity), stochastic volatility and jumps. Regime parameters and transition rules SHALL be declared in the scenario and versioned. Regimes MAY be scripted segments or a seeded Markov chain (see "Markov regime chain").

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
- **THEN** it SHALL NOT move the fair value, and price 25 bars later SHALL be up less often than after a positive surprise (which SHALL leave price higher 25 bars later than the release close in more than 70% of seeds)

#### Scenario: Surprises are priced on arrival
- **WHEN** a credible positive surprise is released and the player buys at the next bar's open
- **THEN** price 25 bars later SHALL be above that open in more than 45% and less than 72% of seeds: the market reprices at once, and following the headline afterwards is no free trade

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
Each scripted event SHALL declare release step, lead time, credibility, prior expectation, surprise, impact channels (drift, volatility, jumps, volume, spread, capacity, funding, IV), intensity distribution and duration. Reactions SHALL be sampled from seeded distributions that include no reaction, reversal and "buy the rumor, sell the news" outcomes, and SHALL NOT be determined by the event text's sentiment alone. The reaction SHALL start on the bar after publication. Independently of the transient reaction, a surprise SHALL move the hidden fair value by `fair_impact × surprise × credibility`; an expected outcome (surprise 0) SHALL leave it unchanged. A share `news_gap` (default 80%) of that fair-value shift SHALL be priced at once, as a gap at the open of the bar after publication; the rest reaches price only through the fair-value pull and the transient reaction.

#### Scenario: Credible news gaps the next open
- **WHEN** a credible (100%) event with surprise −2 and `fair_impact` 0.05 is released under 50 seeds
- **THEN** the next bar SHALL open on average between 7% and 9% (log) below the release close

#### Scenario: Liquidity channels change costs, not prices
- **WHEN** an event declares a spread multiplier and a capacity multiplier
- **THEN** the bars and funding rates SHALL be identical to the same event without them, and orders filled on the event's reaction bars SHALL pay the multiplied half-spread and slippage and be limited to the multiplied capacity

#### Scenario: A funding shock is published with the headline
- **WHEN** an event declares a funding shift
- **THEN** the first shifted funding rate SHALL be the one published at the close of the release bar, and no earlier rate SHALL change

#### Scenario: Same event, different reactions
- **WHEN** the reaction of the same positive-surprise event (`p_none` 0.2, `p_align` 0.6) is sampled under 1,000 seeds
- **THEN** about 20% SHALL be no reaction, 48% along the surprise and 32% reversed, each within the test tolerance, so good news can still sell off

### Requirement: Per-seed schedule
A generator spec MAY declare a lead range `[lead_jitter_min, lead_jitter_max]`. For each seed the generator SHALL draw a lead from that range on its own PRNG stream, lengthen the first segment by it, shorten the last segment by the same amount (the total length is fixed), and shift every event whose reaction starts after the first segment by it, so a replay cannot learn on which bar a scripted move happens.

#### Scenario: Same length, moving story
- **WHEN** a spec with a lead range of −3..+5 bars is scheduled for 300 seeds
- **THEN** every lead in the range SHALL occur, the total number of bars SHALL stay the same, events inside the first segment SHALL keep their step and later events SHALL move by the lead

### Requirement: Mission realism
Over every built-in mission and 60 seeds, with each signal read at a bar's close and entered at the next bar's open:
- RSI(14) fading, Bollinger(20,2) fading, MACD histogram following and close-versus-SMA(20) following SHALL each be right more than 42% and less than 58% of the time five bars later;
- following any scripted headline from the next open SHALL be right less than 72% of the time ten bars later, and a credible (≥ 80%) surprise of size 2 or more SHALL gap the next open by more than 3% on average;
- a mission's biggest one-bar drop SHALL fall on its three most common bars in fewer than half of the seeds;
- per-bar returns SHALL show excess kurtosis above 1, first-order autocorrelation of absolute returns above 0.1 and of returns below 0.25.

`tools/market_study.py` prints the same measurements as the README tables. A mission added later joins these measurements unchanged; the shock-gap check covers every credible surprise of size 2 or more in any mission, not a fixed list.

#### Scenario: Shocks do not repeat on the same bars
- **WHEN** m03 and m05 are generated for 60 seeds
- **THEN** the bars holding their biggest drop SHALL be spread so that no three bars hold half of the seeds

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
- **THEN** it SHALL NOT be usable in published packs without its calibration report (the packer refuses to build generated bars for publishing when `docs/calibration/generator-v<N>.md` is missing)

### Requirement: Markov regime chain
A generator spec MAY declare a regime chain:
- state templates (segments);
- a transition matrix in per-mille with a zero diagonal;
- a dwell range in bars;
- initial weights;
- a total length;
- the position where the chain's segments are inserted.

The path SHALL be drawn per seed on its own PRNG stream with integer draws. It SHALL fill exactly the declared length, cutting the last visit. It SHALL be expanded into ordinary segments before the per-seed schedule is applied. Segments MAY carry a regime label (range, trend, high-volatility, low-liquidity, crowded, calm, shock), which SHALL NOT affect generation. A spec without a chain SHALL generate exactly what it generated before.

#### Scenario: Transition frequencies
- **WHEN** a chain with a declared matrix is drawn for 2,000 seeds
- **THEN** each transition's observed frequency SHALL be within 3 points of its declared value, every path SHALL have the declared length, and every visit except the last SHALL last within the dwell range

#### Scenario: Existing missions unchanged
- **WHEN** m01–m05 are generated by an engine that supports regime chains
- **THEN** their bars, news, funding, IV and golden digests SHALL be identical to before

#### Scenario: Funding does not announce the next regime
- **WHEN** two chain specs, generated with the same seed, expand to paths that differ only in the state of the visit that starts at bar k
- **THEN** their bars and IV SHALL be identical up to bar k−1, and their funding rates up to and including the rate for bar k (published at the close of bar k−1)

### Requirement: Drawn event outcomes and intensity
A scripted event MAY declare weights for its actual value (−2..+2) and an intensity range. For each seed, the generator SHALL draw both on their own PRNG stream, separate from the reaction stream:
- the drawn actual SHALL replace the declared one in the news item;
- the intensity SHALL scale the event's fair-value impact and its reaction drift.

An event that declares neither SHALL behave as before. A drawn actual equal to the expectation SHALL leave the fair value unchanged.

#### Scenario: Outcome distribution
- **WHEN** an event with weights 10/20/40/20/10% for −2..+2 is drawn for 2,000 seeds
- **THEN** each outcome's frequency SHALL be within 3 points of its weight, and every intensity SHALL lie within the declared range

#### Scenario: Same headline, different size
- **WHEN** two seeds draw the same actual with different intensities
- **THEN** the larger intensity SHALL produce a larger fair-value shift and opening gap

### Requirement: Announced events
A news item MAY declare an announcement lead. From the close of `release_step − lead`, the state view SHALL list it as upcoming, with its kind, release step and expectation (the consensus) only. Its tone, actual and credibility SHALL stay hidden until its release. The bars SHALL NOT depend on the lead.

#### Scenario: Consensus without the outcome
- **WHEN** an announced event's actual, tone or credibility is changed and nothing else
- **THEN** every state view before its release step SHALL be identical

#### Scenario: Visible from the announcement
- **WHEN** an event released at step 90 is announced 20 bars ahead
- **THEN** it SHALL be listed as upcoming from step 70, and not at step 69

### Requirement: Seeded reveal
The engine SHALL provide, for a built-in mission and seed, the realized regime spans and each event's drawn actual, intensity and sampled reaction:
- each span has its start, length, label, and the drawn direction when the direction is random;
- each reaction is none, along the surprise, or reversed.

The reveal SHALL be a pure function of the mission id and seed. It SHALL NOT be an input to the run state, the score or any digest. Clients SHALL request it only after the run has ended.

#### Scenario: Reveal matches the market
- **WHEN** the reveal of a mission with a regime chain is requested for a seed
- **THEN** its spans SHALL cover every bar exactly once, and regenerating the mission with that seed SHALL give the same spans

### Requirement: Mission learnability
Each built-in mission SHALL declare learnability bands. Each band states what a disciplined player can learn in that mission and that the outcome is not guaranteed. Engine tests SHALL check the bands over 60 seeds with scripted reference players entered at the next open. In every built-in mission:
- a 1× long held from the first eligible open SHALL be positive in fewer than 75% of seeds;
- every reference strategy that can lose SHALL lose in at least 15% of seeds;
- where the design names a disciplined reference rule (m07, m08, m12), it SHALL have a better mean return-to-maximum-drawdown ratio than that buy-and-hold.

The mission bands are listed in design D8.3.

#### Scenario: Buy-and-hold is not the answer
- **WHEN** each built-in mission is generated for 60 seeds and a 1× long is held from the first eligible open to the last close
- **THEN** it SHALL be positive in fewer than 75% of the seeds of every mission

#### Scenario: Indicators by regime (m08)
- **WHEN** m08 is generated for 60 seeds and RSI(14) extremes are faded inside the revealed range spans and inside the trend spans
- **THEN** the fade SHALL win between 55% and 72% of the time inside range spans and less than 50% of the time inside trend spans

#### Scenario: Straddles price the event (m09)
- **WHEN** m09 is generated for 60 seeds and an ATM straddle of the first expiry is bought 5 bars before the release and sold 3 bars after
- **THEN** it SHALL win in 25%–55% of seeds, more than 70% of the time when the drawn surprise is ±2, and less than 20% of the time when it is 0

#### Scenario: A protective put is a floor (m10)
- **WHEN** a 1× long is held in m10 together with an equal quantity of the second expiry's ATM put
- **THEN** its loss SHALL NOT exceed (entry − strike + premium + costs) × quantity in any seed
