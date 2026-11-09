## ADDED Requirements

### Requirement: Round flow
A "guess the next move" round SHALL:
1. pick a round market, seed and decision step from mulberry32 (the step in [warm-up + 20, total − 13]);
2. start an engine run and advance it with no actions to that step;
3. show the chart and indicators up to that bar only;
4. ask the player to commit a decision (long, short or no trade), the probability that the close 12 bars later is higher (5% to 95% in steps of 5) and, for a trade, an invalidation price and a target price;
5. only after the commitment, advance 12 bars and reveal the path, whether the invalidation or the target came first, and the ATR.

The commitment SHALL NOT be editable after the reveal starts.

#### Scenario: No bar after the decision before commitment
- **WHEN** the round is waiting for the player's commitment
- **THEN** no bar after the decision step SHALL be drawn or available to the client view

### Requirement: Decision-quality score
A round SHALL be scored from 0 to 100 on decision quality, never on P&L, in integer arithmetic kept in hundredths:
- **Calibration (50):** 50 − (p − o)² / 100, with p in percent and o = 100 when the close was higher, else 0, floored at 0.
- **Plan (40, trades):** the invalidation on the losing side (10); its distance between 0.5 and 3 ATR(14) at the decision bar (15); reward to risk of at least 1.5 (15). A no-trade decision scores 40 when 40 ≤ p ≤ 60, else 0.
- **Consistency (10):** long with p ≥ 55, short with p ≤ 45, or no trade with 40 ≤ p ≤ 60.

Prices and ATR SHALL be compared in fixed point at 1e-8. Web and desktop SHALL compute the same score, which a shared parity fixture checks.

#### Scenario: Calibrated long
- **WHEN** a player goes long with p = 80, an invalidation 1 ATR below the close and a target 2 ATR above, and the close 12 bars later is higher
- **THEN** the score SHALL be 46 + 40 + 10 = 96

#### Scenario: P&L ignored
- **WHEN** two rounds have identical commitments and outcomes but the target is hit in one and not in the other
- **THEN** both SHALL get the same score

### Requirement: Round history
The client SHALL keep the last 200 rounds on the device (market, seed, step, decision, probability, score). Each round result SHALL be labeled one sample. After 20 rounds the screen SHALL show a calibration table of stated probability against observed frequency per 10% bucket.

#### Scenario: Calibration table
- **WHEN** a player has completed 20 rounds
- **THEN** the round screen SHALL show the calibration table
