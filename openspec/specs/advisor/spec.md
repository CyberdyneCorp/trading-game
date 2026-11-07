# advisor Specification

## Purpose
Defines the in-game advisor: honest, rationed advice computed only from data the player can already see, logged as a command so replays and scores stay deterministic, and shown in the terminal and the debrief.

## Requirements

### Requirement: Advice from visible data only
The engine SHALL compute advice at the current decision step only from data visible to the player at that step: closed bars up to the step, released news (including early headlines delivered by an active perk), the published funding rate, candle patterns ending at or before the step, and the release steps of releases announced by then (never their outcome). It SHALL NOT use hidden generator state (fair value, segments, scripted reactions) or any bar after the step.

#### Scenario: Future bars do not change advice
- **WHEN** two scenarios are identical up to step s and differ afterwards
- **THEN** advice requested at step s SHALL be identical in both

### Requirement: Advice content
Advice SHALL contain a stance (long, short or wait), a confidence in basis points between 5000 and 6500, a suggested protective stop price, a suggested size risking 1% of equity at that stop, and the list of reasons that produced it. Each reason SHALL say which way it voted. When a credible news surprise was released within the last 3 bars, advice SHALL say that the news is already priced and SHALL NOT vote in the news' direction because of it. When a release announced ahead (add-scenarios-and-gameplay G3) is due within the next 3 bars, advice SHALL say that an announced event is ahead and that reason SHALL pull the read one step toward wait, without voting either way.

#### Scenario: Announced release ahead
- **WHEN** a release announced 20 bars ahead is due 2 bars after the current step
- **THEN** the reasons SHALL include "event ahead" with its release step and that reason SHALL vote for neither side

#### Scenario: Range with an oversold RSI
- **WHEN** ADX(14) is below 20 and RSI(14) is below 30 with no other votes
- **THEN** the reasons SHALL include the range regime and the RSI extreme, and the stance SHALL NOT be short

#### Scenario: Shock just priced
- **WHEN** a credible -2 surprise was released at the previous step
- **THEN** the reasons SHALL include "news already priced" and that headline SHALL NOT be a short vote

### Requirement: Advice is a rationed, logged command
Requesting advice SHALL be an engine command with a sequence number, recorded in the run's command log and replayed like any other command. It SHALL NOT advance time or change orders, positions or balances. Each accepted request SHALL use one unit of the run's advice allowance, which is set at start. A request with no allowance left SHALL be rejected with `NoAdviceLeft` and SHALL change no state.

#### Scenario: Out of points
- **WHEN** a run started with allowance 1 has already received one piece of advice
- **THEN** a further request SHALL be rejected with `NoAdviceLeft`
- **AND** the state digest SHALL be unchanged by the rejected request

### Requirement: Honest advisor
Across the built-in missions (every built-in mission × 60 seeds, advice every 5 bars, outcome measured from the next open to 5 bars later), long and short advice SHALL be right in 45% to 65% of cases in every mission. The advisor SHALL choose "wait" in at least 20% of requests, and its mean stated confidence SHALL be within 8 percentage points of its measured hit rate.

#### Scenario: Advisor is not an oracle
- **WHEN** the realism test measures the advisor on m03
- **THEN** its hit rate SHALL be at most 65%

### Requirement: Advice in the terminal and the debrief
The terminal SHALL offer an advisor control that shows the remaining points and is disabled at 0. Advice SHALL be displayed with its stance, confidence, stop, size and localized reasons, and with a reminder that it is a probabilistic read and not an instruction. Applying the suggested stop or size to the ticket SHALL be one action, but SHALL NOT place an order by itself. Applying SHALL set the ticket to a market order (the read is sized for a market entry) and SHALL keep the leverage typed in the ticket. When the suggested size needs more margin than is available at that leverage, applying it SHALL raise the ticket's leverage to the lowest whole value within the mission cap that fits, or, if none does, reduce the size to what fits at the cap; it SHALL never lower it. The advisor panel SHALL show its whole read (stance, levels, apply action, reasons and reminder) without a scroll box of its own. The debrief SHALL list each piece of advice (step, stance, confidence) with its outcome 5 bars later, and the advisor's hit rate in the run.

#### Scenario: Suggested size beyond the margin at 1x
- **WHEN** a read suggests 120.4 units at a price near 102 with 10,000 available and the ticket at 1x
- **THEN** applying it SHALL set the ticket to 2x with the same size, and the ticket SHALL be sendable

#### Scenario: Applying keeps the typed leverage
- **WHEN** the player typed 3x and chose a limit order, then applies a read whose size fits at 3x
- **THEN** the ticket SHALL stay at 3x with the suggested size, and SHALL switch to a market order

#### Scenario: Advice in the debrief
- **WHEN** a run with two pieces of advice is finished
- **THEN** the debrief SHALL list both, each marked right, wrong or "wait"
