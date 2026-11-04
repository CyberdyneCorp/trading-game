# Spec Delta

## Purpose

Defines how a mission attempt is graded so that the score rewards risk discipline, coherent planning, understanding and calibrated confidence rather than profit, turnover or leverage.

## ADDED Requirements

### Requirement: Score dimensions
The mission score SHALL be a weighted combination of risk (default 35%), plan coherence (25%), understanding (25%) and probabilistic calibration (15%), with weights declared per scoring version. Dimensions that do not apply SHALL be excluded and the remaining weights renormalized; no points SHALL be granted automatically for a non-applicable dimension. Scores SHALL be computed by the engine (`engine/src/scoring.cpp`, scoring version 5) from the command log with integer arithmetic only.

Entries are exposure-increasing fills of the perpetual and of each option series (buying or writing to open). Plan coherence SHALL be non-applicable when the attempt has no entries. Otherwise, in scoring versions 1 and 2, it SHALL be `70 × min(plans, entries) / entries + 30 × plans with an invalidation level / plans`, where skipped plans do not count as plans, and 0 when no plan was recorded.

#### Scenario: Renormalization
- **WHEN** a mission has no calibration questions
- **THEN** the remaining weights SHALL be scaled to sum to 100%

#### Scenario: Options trades count
- **WHEN** the player only buys an option and holds it to expiry
- **THEN** it SHALL count as an entry (the attempt is not "waiting"), and the debrief SHALL list it as a trade in its series with its premium, settlement value, net result and exit reason `expiry`; a written option bought back by a margin call SHALL exit with reason `margin_call`

#### Scenario: Debrief trades add up to the result
- **WHEN** the player buys 10, sells 20 (closing the long and opening a short of 10) and closes the short, with funding paid in between
- **THEN** the flip's fee SHALL be split between the two trades in proportion to the quantity closed and opened, each trade's net SHALL be its realized P&L minus its fees plus the funding received (or minus the funding paid) while it was open, and the trades' nets SHALL sum to the mission result

#### Scenario: An open trade's net includes its unrealized P&L
- **WHEN** a trade is still open at the end of the mission
- **THEN** its P&L SHALL include the unrealized P&L at the final mark, valued as in the account equity (the perpetual at the last close, a long option at the bid, a written option at the ask), so a single open trade's net equals the mission result (scoring version 5; before, an open trade showed only its fees and funding)

#### Scenario: Skipped plan has no R
- **WHEN** a trade's plan was skipped
- **THEN** the trade SHALL have no planned risk and no R multiple, even if the client sent a risk estimate with the skipped plan

#### Scenario: Waiting the whole mission (EDU02)
- **WHEN** the player never trades
- **THEN** risk SHALL be 100, plan coherence non-applicable and the total 100

### Requirement: Risk dimension
The risk dimension SHALL evaluate adherence to planned risk, leverage relative to the mission cap and to the stop distance, distance kept from liquidation, drawdown versus mission budget and exposure under declared uncertainty. Using higher leverage SHALL never increase the risk score.

Scoring version 3 SHALL start at 100 and subtract: 40 if any liquidation or option margin call (forced buy-back of written options) occurred; 20 per perpetual entry without a reduce-only stop or trailing stop placed around it (at most 40; option entries are not counted: a bought option's premium is its maximum loss, and written options are policed by margin calls); 20 if any trade that actually happened was entered with a planned risk above 2% of initial capital (the risk of the latest plan, or of the ticket estimate when sent without a plan, recorded for that entry; the plan of an order that was rejected and never filled does not count); 15 if any order was accepted with a stop beyond the liquidation price; 15 if the maximum drawdown exceeded 20% of initial capital; floored at 0.

#### Scenario: Rejected order is not an entry
- **WHEN** an oversized order is rejected and the player retries with a size within 2% risk
- **THEN** the risk dimension and the findings SHALL NOT report an oversized entry

#### Scenario: Margin call scored like a liquidation
- **WHEN** written options are bought back by a margin call
- **THEN** the risk dimension SHALL lose 40 points and the debrief SHALL explain the margin call (bar, buy-back premium, loss) and recommend the risk-sizing lesson

#### Scenario: Leverage never rewarded
- **WHEN** two attempts have identical decisions except one used 10× and the other 3× with the same planned risk
- **THEN** the 10× attempt SHALL NOT score higher on risk

### Requirement: Outcome metrics are separate
Net P&L, drawdown, total costs (fees and, separately, spread/slippage), funding, turnover, peak leverage, the closest distance to liquidation and the option P&L attribution by Greek SHALL be displayed separately and SHALL NOT be score inputs except where a mission objective explicitly targets them (e.g. "stay within drawdown budget").

#### Scenario: Report-only facts do not move the score
- **WHEN** scoring version 4 adds the spread/slippage cost, the Greek attribution and the leverage that would have kept a stop in front of liquidation
- **THEN** the score dimensions SHALL be computed as in version 3 and the run's state and event digests SHALL NOT change

#### Scenario: Report-only fixes in version 5
- **WHEN** scoring version 5 adds an open trade's unrealized P&L to its net, judges the stop against the highest leverage the position carried, withholds good-process credit from written options and adds the good-process-win finding
- **THEN** the score dimensions SHALL be computed as in version 3 and the run's state and event digests SHALL NOT change

#### Scenario: Profit not dominant
- **WHEN** an attempt profits while violating its planned risk
- **THEN** its risk score SHALL reflect the violation regardless of profit

### Requirement: Calibration
Calibration SHALL use Brier scores over binary questions that resolve within the mission (e.g. "will the close in 5 steps exceed X?"), with at least three questions per scored mission. Questions SHALL be asked before the resolving bars are generated or revealed. In the built-in missions the engine SHALL ask "will the close 5 bars from now be above the current close?" at the first decision step and every 15 decision steps after it, while the resolving bar exists; the player answers a probability or skips, and the client SHALL NOT advance (manually or automatically, including batch advances, which stop at the question step) until the question is answered or skipped. Skipped answers SHALL be recorded but not scored. The calibration score SHALL be `max(0, 100 × (1 − 2 × Brier))` (scoring version 2): always answering 50% (Brier 0.25) is neutral at 50 rather than a bonus, and confidently wrong answers score 0.


#### Scenario: No-information answers are neutral
- **WHEN** the player answers 50% to every calibration question
- **THEN** the Brier score SHALL be 0.25 and the calibration score 50
#### Scenario: Minimum question count
- **WHEN** fewer than three non-skipped answers have resolved by the end of the attempt
- **THEN** the calibration dimension SHALL be non-applicable

### Requirement: No subjective LLM grading
Free-text justifications SHALL NOT be scored by an LLM. Understanding SHALL be scored from structured answers and rubrics authored and reviewed by humans.

#### Scenario: Free text ignored
- **WHEN** a player writes a journal entry
- **THEN** it SHALL NOT change any score dimension

### Requirement: Server-authoritative scores
Scores used for progression and sync SHALL be recomputed by the server from the scenario, engine version and command log; client-reported scores and balances SHALL be ignored.

#### Scenario: Forged score
- **WHEN** a client submits a run with a modified score field
- **THEN** the server SHALL discard it and store its own recomputed score
