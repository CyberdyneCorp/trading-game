# Spec Delta

## Purpose

Defines how a mission attempt is graded so that the score rewards risk discipline, coherent planning, understanding and calibrated confidence rather than profit, turnover or leverage.

## ADDED Requirements

### Requirement: Score dimensions
The mission score SHALL be a weighted combination of risk (default 35%), plan coherence (25%), understanding (25%) and probabilistic calibration (15%), with weights declared per scoring version. Dimensions that do not apply SHALL be excluded and the remaining weights renormalized; no points SHALL be granted automatically for a non-applicable dimension. Scores SHALL be computed by the engine (`engine/src/scoring.cpp`, scoring version 1) from the command log with integer arithmetic only.

Plan coherence SHALL be non-applicable when the attempt has no exposure-increasing entries. Otherwise, in scoring version 1, it SHALL be `70 × min(plans, entries) / entries + 30 × plans with an invalidation level / plans`, where skipped plans do not count as plans, and 0 when no plan was recorded.

#### Scenario: Renormalization
- **WHEN** a mission has no calibration questions
- **THEN** the remaining weights SHALL be scaled to sum to 100%

#### Scenario: Waiting the whole mission (EDU02)
- **WHEN** the player never trades
- **THEN** risk SHALL be 100, plan coherence non-applicable and the total 100

### Requirement: Risk dimension
The risk dimension SHALL evaluate adherence to planned risk, leverage relative to the mission cap and to the stop distance, distance kept from liquidation, drawdown versus mission budget and exposure under declared uncertainty. Using higher leverage SHALL never increase the risk score.

Scoring version 1 SHALL start at 100 and subtract: 40 if any liquidation occurred; 20 per entry without a reduce-only stop or trailing stop placed around it (at most 40); 20 if any plan declared a planned risk above 2% of initial capital; 15 if any order was accepted with a stop beyond the liquidation price; 15 if the maximum drawdown exceeded 20% of initial capital; floored at 0.

#### Scenario: Leverage never rewarded
- **WHEN** two attempts have identical decisions except one used 10× and the other 3× with the same planned risk
- **THEN** the 10× attempt SHALL NOT score higher on risk

### Requirement: Outcome metrics are separate
Net P&L, drawdown, total costs, funding, turnover and peak leverage SHALL be displayed separately and SHALL NOT be score inputs except where a mission objective explicitly targets them (e.g. "stay within drawdown budget").

#### Scenario: Profit not dominant
- **WHEN** an attempt profits while violating its planned risk
- **THEN** its risk score SHALL reflect the violation regardless of profit

### Requirement: Calibration
Calibration SHALL use Brier scores over binary questions that resolve within the mission (e.g. "will the close in 5 steps exceed X?"), with at least three questions per scored mission. Questions SHALL be asked before the resolving bars are generated or revealed. In the built-in missions the engine SHALL ask "will the close 5 bars from now be above the current close?" at the first decision step and every 15 decision steps after it, while the resolving bar exists; the player answers a probability or skips, and the client SHALL NOT advance (manually or automatically, including batch advances, which stop at the question step) until the question is answered or skipped. Skipped answers SHALL be recorded but not scored. The calibration score SHALL be `100 × (1 − Brier)`.

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
