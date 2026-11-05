# Spec Delta

## Purpose

Defines how a mission attempt is graded so that the score rewards risk discipline, coherent planning, understanding and calibrated confidence rather than profit, turnover or leverage.

## ADDED Requirements

### Requirement: Score dimensions
The mission score SHALL be a weighted combination of risk (default 35%), plan coherence (25%), understanding (25%) and probabilistic calibration (15%), with weights declared per scoring version. Dimensions that do not apply SHALL be excluded and the remaining weights renormalized; no points SHALL be granted automatically for a non-applicable dimension. Scores SHALL be computed by the engine (`engine/src/scoring.cpp`, scoring version 11) from the command log with integer arithmetic only.

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

#### Scenario: Exit reason from the closing order
- **WHEN** a trade is closed by a trailing stop, or by a stop moved beyond the entry, in profit
- **THEN** its exit reason SHALL be `trailing` or `stop`, never `target`: the reason SHALL follow the type of the order that closed it (stop-market `stop`, trailing stop `trailing`, take-profit or limit `target`, market `manual`) (scoring version 10; before, any exit beyond the entry was `target`)

#### Scenario: An open trade's net includes its unrealized P&L
- **WHEN** a trade is still open at the end of the mission
- **THEN** its P&L SHALL include the unrealized P&L at the final mark, valued as in the account equity (the perpetual at the last close, a long option at the bid, a written option at the ask), so a single open trade's net equals the mission result (scoring version 5; before, an open trade showed only its fees and funding)

#### Scenario: A plan belongs to the order it was recorded for
- **WHEN** the player records a plan and sends an order, and on the bar that order fills records a second plan for a new order (a flip, an add, or a second option leg sent on one bar)
- **THEN** each trade SHALL use the plan recorded for its own entry order: a plan is matched to the next exposure-increasing order of the same kind (perpetual or option) and side placed in the same decision step, the plans and orders of one step pairing from the last one back (so a retry after a rejected order uses the newer plan); a trade's planned risk SHALL be the sum of the plans of the orders that opened or added to it, and its invalidation (stop and plan verdict) SHALL come from the first of them (scoring version 7; before, the latest plan up to the entry fill bar replaced the trade's own plan)

#### Scenario: A shortfall the fund covered is shown
- **WHEN** a margin-call buy-back or a settlement costs more than the balance and the wallet is floored at zero
- **THEN** the report SHALL include the written-off amount (the result minus the sum of the trades' nets, reported only when the simulated fund absorbed a shortfall), so the trade rows plus that line add up to the result (scoring version 7)

#### Scenario: Skipped plan has no R
- **WHEN** a trade's plan was skipped
- **THEN** the trade SHALL have no planned risk and no R multiple, even if the client sent a risk estimate with the skipped plan

#### Scenario: Waiting the whole mission (EDU02)
- **WHEN** the player never trades
- **THEN** risk SHALL be 100, plan coherence non-applicable and the total 100

### Requirement: Risk dimension
The risk dimension SHALL evaluate adherence to planned risk, leverage relative to the mission cap and to the stop distance, distance kept from liquidation, drawdown versus mission budget and exposure under declared uncertainty. Using higher leverage SHALL never increase the risk score.

Scoring version 3 SHALL start at 100 and subtract: 40 if any liquidation or option margin call (forced buy-back of written options) occurred; 20 per perpetual entry without a reduce-only stop or trailing stop placed around it (at most 40; option entries are not counted: a bought option's premium is its maximum loss, and written options are policed by margin calls); 20 if any trade that actually happened was entered with a planned risk above 2% of initial capital (the risk of the plan, or of the ticket estimate when sent without a plan, recorded for one of its entry orders; the plan of an order that was rejected and never filled does not count); 15 if any order was accepted with a stop beyond the liquidation price; 15 if the maximum drawdown exceeded 20% of initial capital; floored at 0.

Scoring version 11 SHALL judge the drawdown against the mission's declared drawdown budget (`MissionRules.drawdown_budget_bp`, 20% unless declared; m07 declares 10%) and SHALL additionally subtract: 10 if any perpetual trade's liquidation price, at the highest leverage the position carried, lay closer to its entry than twice its stop's distance (leverage too high for the stop distance: a gap one stop-distance beyond the stop would liquidate it; finding `leverage_vs_stop` unless the stop was already beyond liquidation); 10 if the closest liquidation distance measured at a close fell below the mission's liquidation-warning distance (finding `liq_buffer`). A breached drawdown budget SHALL be named by the finding `drawdown_budget`. The first two point to the Futures II lesson, the budget to Risk.

#### Scenario: Rejected order is not an entry
- **WHEN** an oversized order is rejected and the player retries with a size within 2% risk
- **THEN** the risk dimension and the findings SHALL NOT report an oversized entry

#### Scenario: Margin call scored like a liquidation
- **WHEN** written options are bought back by a margin call
- **THEN** the risk dimension SHALL lose 40 points and the debrief SHALL explain the margin call (bar, buy-back premium, loss) and recommend the risk-sizing lesson

#### Scenario: Leverage too high for the stop
- **WHEN** two attempts open the same long with the same stop 4% below the entry, one at 5x and one at 12x (liquidation about 7.4% below)
- **THEN** the 12x attempt SHALL lose 10 risk points with the `leverage_vs_stop` finding and the 5x attempt SHALL NOT

#### Scenario: Drawdown against the mission's budget
- **WHEN** a run's maximum drawdown is 12% of its initial capital
- **THEN** it SHALL lose 15 risk points in a mission with a 10% budget and none in a mission with the default 20%

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

Lesson questions SHALL be multiple choice or numeric (with an absolute tolerance), declared per mission with their answer key and points in reviewed data files (`engine/rubrics/lessons.json`) that carry a `reviewed` flag and the reviewer's name; question texts SHALL live in the client catalogs by question id. An answer SHALL be an engine command (`AnswerLesson`), recorded once per question while the run is active; skips SHALL be recorded. Understanding (scoring version 11) SHALL be the points of the right answers over the points of every question of the mission's rubric (skipped or unanswered questions earn 0), and SHALL apply only when the rubric is reviewed and the player answered or skipped at least one question. The debrief SHALL show each question, the player's answer and the key, also for an unreviewed rubric, which SHALL be labeled as not scored. Free-text answers reviewed by a person are a later step and SHALL NOT be scored automatically.

#### Scenario: Free text ignored
- **WHEN** a player writes a journal entry
- **THEN** it SHALL NOT change any score dimension

#### Scenario: Unreviewed rubric does not score
- **WHEN** the player answers a mission's lesson questions and the mission's rubric is not reviewed yet
- **THEN** the debrief SHALL show the answers and the key, and understanding SHALL stay non-applicable

#### Scenario: Reviewed rubric scores the answers
- **WHEN** a reviewed rubric has a 3-point numeric question answered within tolerance and a 1-point question skipped
- **THEN** understanding SHALL be 75

### Requirement: Server-authoritative scores
Scores used for progression and sync SHALL be recomputed by the server from the scenario, engine version and command log; client-reported scores and balances SHALL be ignored.

#### Scenario: Forged score
- **WHEN** a client submits a run with a modified score field
- **THEN** the server SHALL discard it and store its own recomputed score
