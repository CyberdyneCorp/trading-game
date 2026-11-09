## ADDED Requirements

### Requirement: Bias findings with evidence (scoring v14)
The debrief SHALL report, in every mission, these report-only findings. Each SHALL be computed only from the action log and the data visible at its step, and SHALL carry its evidence.
- **`revenge_trade`.** An exposure-increasing entry within 3 bars after a perpetual trade closed at a loss, either sized at least 1.5× that trade's planned risk (or quantity when unplanned) or made with no plan. Evidence: the losing trade, its exit step and loss, the entry step and the size ratio in basis points.
- **`fomo_entry`.** An entry in the direction of at least 4 consecutive same-direction closes, with the close more than 2 ATR(14) beyond SMA(20) at the entry step, and either no stop or a stop more than 2 ATR away. Evidence: the step, the extension in hundredths of ATR and the count of closes.
- **`averaging_down`.** An add in the same direction at a worse price than the average entry while unrealized P&L is negative, that raises the total planned risk above the first entry's plan or is made without a stop. Evidence: the steps, the average entry before and after, the unrealized loss and the planned risk before and after.

These findings SHALL NOT be score inputs and SHALL NOT enter any digest.

#### Scenario: Revenge trade named
- **WHEN** a long is stopped out for a loss of 100 at step 70 and a long sized at double the risk with no plan is entered at step 71
- **THEN** the debrief SHALL report `revenge_trade` naming steps 70 and 71 and a size ratio of 20,000 bp

#### Scenario: Planned re-entry is not revenge
- **WHEN** after a stop-out a new entry at the same planned risk with a recorded plan is made two bars later
- **THEN** no `revenge_trade` finding SHALL be reported

#### Scenario: No look-ahead
- **WHEN** the bars after an entry step are altered
- **THEN** the bias findings at that step SHALL be unchanged

### Requirement: Position episodes (scoring v14)
The debrief SHALL list perpetual position episodes from flat to flat:
- entries and partial exits;
- stop moves classified as to breakeven, toward the entry or away from it;
- whether a trailing stop was used;
- MFE and MAE in R;
- the combined R.

Episodes SHALL be report-only.

#### Scenario: Partial exit and breakeven
- **WHEN** a long of 30 at 100 with a stop at 96 sells 15 at 104 and moves the stop to 100
- **THEN** the episode SHALL list one partial exit at +1R on half and a stop move to breakeven

### Requirement: Risk-limit finding (scoring v14)
In a mission with risk limits, the debrief SHALL report `risk_limit_exceeded` for each limit exceeded at a close: the first step and the number of closes above it, measured after the close phase from the engine's Greeks. It SHALL be report-only.

#### Scenario: Limit exceeded twice
- **WHEN** m18's delta limit is exceeded at the closes of steps 80 and 81 only
- **THEN** the finding SHALL name the delta limit, first step 80 and a count of 2

### Requirement: Existing scores unchanged
Scoring v14 SHALL change no score input. Every existing golden run's score and dimension values SHALL be identical under v13 and v14.

#### Scenario: Golden scores
- **WHEN** every existing golden is scored under v14
- **THEN** each score and dimension SHALL equal its v13 value
