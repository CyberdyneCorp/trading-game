## ADDED Requirements

### Requirement: Stop moves in the debrief
The debrief SHALL report on each perpetual trade `stopMovedAway`: how many accepted moves of a reduce-only stop order moved its trigger away from the entry while the trade was open (lower for a long, higher for a short). Moves toward the entry, and moves of take-profits or trailing stops, SHALL NOT count. The field SHALL be report-only: it SHALL NOT change a score, a state digest or an events digest. Option trades SHALL omit it.

#### Scenario: Stop widened once
- **WHEN** a long is protected by a stop at 98 that the player moves to 96 and then to 97
- **THEN** the trade's `stopMovedAway` SHALL be 1

### Requirement: Per-run summary
When the debrief of one of the player's own runs loads (not assisted, not imported, not viewed), the client SHALL store a compact summary with the run. The summary SHALL hold the mission, status, initial capital, net result and score; each trade's net, R, risk as a share of initial capital, exit reason, plan respected, stop moved away and instrument; and the finding codes with their tone. Runs without a summary that reached a debrief SHALL be recomputable on request by verifying them as run files, which skips runs of another execution policy or whose replay does not match and reports how many were skipped.

#### Scenario: Summary written at the debrief
- **WHEN** a free-play run reaches its debrief
- **THEN** its run record SHALL carry the summary, and the statistics screen SHALL count it without replaying it

### Requirement: Journal statistics
The client SHALL offer a statistics screen over the player's own runs that reached a debrief. Assisted attempts, imported runs and runs left before their end SHALL be excluded. It SHALL show:
- closed trades, win rate, average R, the distribution of R in the buckets of design D7, and expectancy (mean R), with the trades that have no R counted apart;
- stops respected (plan respected over trades judged) and stops moved away (perpetual trades with a stop moved away over perpetual trades that report it);
- average risk per trade as a share of initial capital, the shares below 1%, within 1–2% and above 2%, and the runs warned for oversizing;
- liquidations and margin calls;
- the mistakes that repeat: warning and bad finding codes by the number of runs that had them, flagged when at least two runs did;
- the same core numbers per mission and per curriculum stage;
- the trend: the last 10 runs against the 10 before them, and each run in time order.

A mission filter SHALL narrow every number. With fewer than 30 closed trades, the screen SHALL say it is too early to draw a conclusion.

#### Scenario: Win rate and expectancy
- **WHEN** the counted runs have closed trades with R of +2, −1, −1 and +0.5
- **THEN** the screen SHALL show a win rate of 50% and an expectancy of +0.13R

#### Scenario: Assisted and imported runs excluded
- **WHEN** the player has one finished free-play run, one assisted attempt and one imported run
- **THEN** the statistics SHALL be computed from the free-play run only

### Requirement: Statistics tied to lessons
Each statistic SHALL name the lesson it measures and link to it, as listed in design D7:
- win rate, R and expectancy: Expectancy and drawdown;
- risk per trade: Sizing from the stop;
- stops: Plan, journal and biases;
- liquidations and margin calls: Margin, leverage and liquidation, or Writing options;
- each repeated mistake: the lesson its finding points to.

The texts SHALL describe process, not predict the market.

#### Scenario: Oversizing links to sizing
- **WHEN** the risk statistic shows trades above 2%
- **THEN** it SHALL link to the "Sizing from the stop" lesson
