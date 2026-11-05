# Spec Delta

## Purpose

Defines the client-independent mission flow: briefing, observing, planning, acting or waiting, advancing, and the debrief that separates decision quality from outcome.

## ADDED Requirements

### Requirement: Mission briefing
Before the first decision point, the player SHALL be shown virtual capital, costs (fees, spread, funding interval), leverage cap, allowed instruments (long, short, options buying, options writing), objectives, mode (historical/synthetic/branch) and the market alias.

Objectives, instrument permissions, the leverage cap, the liquidation-warning distance, the drawdown budget and whether a stop beyond liquidation needs explicit confirmation SHALL be mission data in the engine (`MissionRules`), sent with the run state; clients SHALL only render them (objective codes as text in the player's language, numbers in the player's locale). A mission that declares no objectives (an imported pack) SHALL use the common ones: plan every entry, planned risk of at most 2% per idea, no liquidation.

#### Scenario: Instrument permissions visible
- **WHEN** a mission disables options writing
- **THEN** the briefing SHALL state it and the order ticket SHALL not offer selling options to open

#### Scenario: Objectives from the engine
- **WHEN** m02 declares the objective "keep the distance to liquidation above 10%" and a 10% warning distance
- **THEN** the briefing SHALL list that objective with the number formatted in the locale (10% in PT-BR and English), and the terminal SHALL warn when liquidation comes within 10% of the last close

### Requirement: Time stops at decisions
The market SHALL advance only when the player explicitly advances (one step or a declared batch). Submitting an order, closing a position and advancing SHALL be distinct actions.

#### Scenario: No implicit advance
- **WHEN** the player submits an order and does nothing else
- **THEN** the market SHALL stay at the same step and the order SHALL remain `accepted`

### Requirement: Plan before advance
When the player submits an order that opens or increases a position, the client SHALL request a structured plan (hypothesis direction, invalidation level, target or exit rule, confidence, planned risk); for options also the scenario in which the trade loses. The plan SHALL be recorded as an engine command at the current decision step, before the order it belongs to and before the next advance. Skipping the plan SHALL be allowed and SHALL be recorded; a skipped plan SHALL count as an unplanned entry, which lowers plan coherence (see mission-scoring) and shows the trade without a plan in the debrief. Free-text thesis and journal notes SHALL stay local and SHALL NOT be sent to the engine.

#### Scenario: Plan recorded before outcome
- **WHEN** the player records a plan and then advances
- **THEN** the plan SHALL be immutable and timestamped with the step at which it was made

### Requirement: Waiting is a decision
Advancing without trading SHALL be recorded as a `wait` decision and SHALL receive feedback in the debrief. No mission objective SHALL require a minimum number of trades.

The engine SHALL record, for every decision step it advances through, an explicit decision record: `act` when an accepted command placed, cancelled or moved an order or changed leverage or margin at that step; `hold` when no such command was sent while a position (perpetual or option) stayed open; `wait` otherwise. Plans, calibration answers, lesson answers and advice requests are not trades. The records SHALL be derived from the state at each step (not a state-digest input), kept in checkpoints, and listed in the debrief per decision step (scoring version 11).

#### Scenario: No trade when no hypothesis (EDU02)
- **WHEN** a player completes a mission without trading
- **THEN** the score SHALL NOT be reduced for lack of turnover

#### Scenario: Each decision is recorded
- **WHEN** the player advances once flat, then buys and advances, then advances again while holding
- **THEN** the debrief SHALL list the decisions as wait, act and hold for those three steps

### Requirement: Liquidation warnings
Clients SHALL warn at decision points when a position's liquidation price is within a mission-declared distance (a percentage of the last close, `MissionRules.liq_warning_bp`; 3% unless the mission declares another) of the last close, and SHALL warn on the order ticket when a stop lies beyond the liquidation price. When the mission asks for it (`confirm_stop_beyond_liq`, the default), the ticket SHALL require an explicit confirmation before sending such an order, for a new order and for a protective stop on an open position. The engine SHALL accept such an order with the warning `stop_beyond_liquidation`, and the risk score SHALL reflect it.

#### Scenario: Stop beyond liquidation warning
- **WHEN** a player places a long stop below the liquidation price
- **THEN** the ticket SHALL show a warning explaining liquidation will happen first and require explicit confirmation

### Requirement: Game over on bankruptcy
When the engine reports `account_bankrupt`, the client SHALL end the mission with a game-over screen that leads directly to the debrief. In Campaign, virtual capital SHALL carry over between the missions of a campaign run, and bankruptcy SHALL end the whole campaign run; a new campaign run SHALL start from the campaign's initial capital. Game over SHALL NOT remove completed lessons, unlocked stages, cosmetics or purchases.

A campaign run SHALL play the curriculum's assessment missions in stage order. Each mission's starting capital SHALL be the engine's final equity of the mission before, given to the engine as a run start option, so the engine remains the only source of balances and a replay reproduces the run. A run that ends (or is left) with less than the campaign minimum, 1% of the campaign's initial capital, SHALL end the campaign run like a bankruptcy and SHALL NOT pass its stages: the engine ends a run only when the minimum order is unaffordable at that mission's own leverage cap, and the next mission may cap leverage lower. The debrief SHALL say why the campaign run ends. Leaving a campaign mission for another run SHALL carry the equity it had when it was left, and the campaign run SHALL stay on that mission, so leaving never restores lost capital. Ending a campaign run by choice SHALL ask for confirmation, and its mission in progress SHALL end with it (no longer resumable as a campaign mission).

#### Scenario: Bankrupt mid-campaign
- **WHEN** the player goes bankrupt in mission 4 of a campaign run
- **THEN** the campaign run SHALL end with a game-over screen and debrief
- **AND** a new campaign run SHALL be offered with initial capital and all previously unlocked stages intact

#### Scenario: Liquidated below the campaign minimum
- **WHEN** a campaign mission ends with 0.61 of capital after a liquidation at 20x, and the engine still finishes the run because the minimum order is affordable at that mission's cap
- **THEN** the campaign run SHALL end as bankrupt, the mission's stage SHALL NOT be passed and no next mission SHALL be offered

#### Scenario: Ending a campaign run with a mission in progress
- **WHEN** the player ends the campaign run from the menu while its mission is resumable
- **THEN** the menu SHALL ask for confirmation first, and after it the mission SHALL no longer be offered for resume

#### Scenario: Capital carried
- **WHEN** the player finishes the first campaign mission with an equity of 9,420.50
- **THEN** the second campaign mission SHALL start with 9,420.50 of capital, shown in its briefing and used by the engine for margin, the score's risk limits and the buy-and-hold comparison

### Requirement: Assisted practice
Rewinding or changing seed SHALL create a new attempt flagged `assisted`, SHALL preserve the original attempt unchanged and SHALL exclude assisted attempts from non-assisted progress metrics.

A rewind SHALL rebuild the run from its own logged actions before the chosen decision step (same market and start options), so the new attempt resumes exactly there; changing seed SHALL start the same mission on another market. An assisted attempt SHALL start without the advisor, SHALL NOT count as completing a mission (or toward perks that count completed missions), SHALL NOT earn or spend advisor points and SHALL NOT be a campaign leg; a campaign mission in progress SHALL NOT be rewound (after its debrief it may). Assisted attempts SHALL be labeled as such in the terminal, the debrief and the journal.

#### Scenario: Rewind keeps original
- **WHEN** the player rewinds a completed run
- **THEN** the original run and its score SHALL remain available and unchanged

#### Scenario: Assisted attempt excluded from progress
- **WHEN** the player rewinds a free-play run of m01 to bar 3 and finishes the rewound attempt
- **THEN** the debrief SHALL label it assisted with no advisor points earned, and m01 SHALL NOT be marked completed by it

### Requirement: Debrief
On completion the debrief SHALL show: market reveal (if permitted) and historical/synthetic label; timeline of plans, news, orders, fills, funding, margin changes, liquidations and expiries; net P&L split into realized trading P&L, the P&L still open at the end (unrealized, options included), fees, spread/slippage, funding, option premium/settlement and, after a bankruptcy, the loss beyond the balance that the simulated fund covered; max drawdown, peak leverage, minimum liquidation distance, turnover; requested vs trigger vs executed price per fill; `ambiguousIntrabar` and capacity approximations; comparison with staying flat and with a 1× long held from the first eligible bar to the run's last bar (the game-over bar when the run ended early, never prices after it); for options, an approximate P&L attribution to delta, gamma, theta and vega, labeled approximate; separation of good process, favorable luck and items needing more samples; a recommended next lesson based on observable errors.

These numbers SHALL be computed by the engine (report-only: they SHALL NOT change the run state, its digests or the score dimensions); clients SHALL only format them, in the player's locale.

- **Spread/slippage** SHALL be the cost paid against a reference price, already contained in the fill prices: taker perpetual fills (market, stop, trailing stop) against the bar's open (market orders and gapped stops) or the stop's trigger; maker fills (limits, take-profits) and liquidations pay none; option fills against the mid of the quote they executed on.
- **Greek attribution** SHALL be computed bar by bar for every held option series by full revaluation (scoring v8), so a gap or a crash lands in delta and gamma with its real size and sign. Each bar is walked in three legs whose mids chain exactly: previous close → open and open → close (or → settlement price at expiry) at the previous step's quote (price only), then one quote step at that forward (time and IV; intrinsic value at expiry). A price leg's mid change SHALL be split into delta = start delta·ΔF and gamma = the rest; the time leg's into vega = average vega·ΔIV(points) and theta = the rest. A residual SHALL hold the rest of the option P&L before fees (the spread paid against the mid at fills, and rounding), so delta + gamma + theta + vega + residual equals the option P&L (realized, plus open positions at the final mid).
- **Leverage that keeps the stop in front**: for each perpetual trade, the stop SHALL be its first reduce-only stop order or, without one, its plan's invalidation; the liquidation price SHALL be computed at the trade's entry price and size and the highest leverage the position carried: the leverage in effect before its entry bar or, if higher, any leverage the player set while it was open (re-leveraging re-margins the open position; a change at the exit bar belongs to the next trade). When the stop sits at or beyond it, the debrief SHALL name the highest whole leverage (at least 1×) whose liquidation price stays beyond the stop, or say that not even 1× would.

#### Scenario: Spread and slippage shown separately
- **WHEN** the player buys at market and closes at market in a market that does not move, with no fees
- **THEN** the debrief SHALL show a spread/slippage cost equal to the loss, and a limit order filled at its price SHALL add nothing to it

#### Scenario: Greek attribution adds up
- **WHEN** the player holds a bought call to expiry in a flat market
- **THEN** delta, gamma and vega SHALL be zero, theta SHALL explain at least half of the loss, and the five lines SHALL sum exactly to the option P&L before fees

#### Scenario: A written put hit by a gap
- **WHEN** the player writes a put and the market gaps down far through the strike before the put is bought back
- **THEN** delta and gamma SHALL both be losses that explain the loss, and the residual SHALL be no larger than the spread paid at the fills

#### Scenario: Outcome lines add up with a position still open
- **WHEN** the player holds a long to the end of the mission
- **THEN** the outcome lines SHALL include the P&L still open at the end, so realized P&L, open P&L, fees and funding add up to the net result, and the timeline SHALL list each released headline at its bar

#### Scenario: Game over compares with the run, not the scenario
- **WHEN** a run ends bankrupt before the scenario's last bar
- **THEN** the market span and the buy-and-hold comparison SHALL end at the game-over bar, and the debrief SHALL say so

#### Scenario: Good process, negative result (EDU01)
- **WHEN** a breakout fails and a planned stop limits the loss within planned risk
- **THEN** the debrief SHALL credit risk management and SHALL NOT label the decision as a mistake solely due to the loss

#### Scenario: No credit for a broken process
- **WHEN** a run lost money and any trade had no plan, was oversized, had a stop beyond liquidation, was liquidated or was margin-called, or any option was written (a writer's loss is not limited by a stop or a premium)
- **THEN** the debrief SHALL NOT credit the loss to good process

#### Scenario: Good process, positive result
- **WHEN** a run made money and every trade had a plan and limited risk (the same conditions as a loss credited to good process)
- **THEN** the debrief SHALL credit the process and remind the player that one result alone does not prove an edge, so "What happened" is never empty after a disciplined win

#### Scenario: A profit without full credit still says why
- **WHEN** a run made money but a trade had no plan of its own, or an option was written (scoring v8)
- **THEN** the debrief SHALL warn that the profit is unproven (a trade without a plan) or that a written option's risk is limited by neither a stop nor the premium, and SHALL NOT describe the write as limited risk

#### Scenario: Re-leveraged past the stop
- **WHEN** a long entered at 2× with its stop 10% below is re-leveraged to 20× while open and then liquidated
- **THEN** the trade SHALL show 20×, its stop beyond the 20× liquidation price and the highest leverage that would have kept the stop in front, also in the liquidation and stop-beyond-liquidation findings

#### Scenario: Liquidation explained
- **WHEN** a run included a liquidation
- **THEN** the debrief SHALL show leverage, margin, liquidation price, the bar that triggered it and what leverage would have kept the planned stop in front of liquidation

### Requirement: Seeded market reveal
For a generated mission, the debrief's market reveal SHALL add what this seed drew to the mission's designed reveal text:
- the regime spans, shown as a band under the chart;
- each event's drawn actual, intensity and reaction.

Where the mission has regime spans, the debrief SHALL show a "signals by regime" table, computed by the engine as a report-only fact (scoring version 8): for RSI fade, Bollinger fade, MACD follow and SMA20 follow, the hits and samples in each span of this run. Each trade SHALL be tagged with the regime it was entered in. The client SHALL NOT request the reveal before the run has ended.

#### Scenario: Regime band after the run
- **WHEN** a run of m08 ends
- **THEN** the debrief SHALL show the drawn regime band under the chart and the signals-by-regime table, and each trade row SHALL name the regime it was entered in

#### Scenario: No reveal while playing
- **WHEN** a run is still active
- **THEN** no worker message or UI state SHALL carry regime spans or drawn outcomes for steps after the current one

### Requirement: Campaign report
The capstone mission's debrief (m12) SHALL show a campaign report over the settled missions of the current campaign run:
- number of trades, win rate, average R and expectancy in R;
- the share of costs (fees, spread/slippage, funding) in gross P&L;
- results by revealed regime.

The report SHALL be built from the engine debriefs already stored for those runs; the client SHALL NOT recompute their P&L. With fewer than 30 trades, the report SHALL say that there are too few trades to conclude anything about an edge.

#### Scenario: Too few trades
- **WHEN** a campaign run reaches the m12 debrief with 18 trades in all its missions
- **THEN** the campaign report SHALL show its numbers together with the warning that 18 trades are too few to conclude
