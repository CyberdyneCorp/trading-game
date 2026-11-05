## ADDED Requirements

### Requirement: Advisor points earned by process
The engine SHALL report the advisor points earned in a run: 1 for each closed trade that had a recorded plan and a protective stop order working while it was open, 1 more if that trade's net P&L was positive, and 2 if the run finished (not bankrupt). The total SHALL be capped at 6 per run. Points SHALL NOT be purchasable.

#### Scenario: Disciplined winning trade
- **WHEN** a run closes one trade that had a plan and a stop, with positive net P&L, and the run finishes
- **THEN** the run SHALL earn 4 points (1 + 1 + 2)

#### Scenario: Unplanned trade earns nothing
- **WHEN** a trade without a plan closes with a profit
- **THEN** that trade SHALL earn no points

### Requirement: Local point balance
The client SHALL keep an advisor point balance per device, starting at 3 and capped at 10. A run's advice allowance SHALL be the balance at start. When a run ends, the balance SHALL become balance − advice used + points earned, applied exactly once per run even after reloads or replays. Practice runs on known scenarios (imported or loaded packs) SHALL start without advice and SHALL NOT change the balance.

#### Scenario: Settlement is idempotent
- **WHEN** a finished run's debrief is opened, the page is reloaded and the debrief is opened again
- **THEN** the balance SHALL have changed only once

#### Scenario: Practice on an imported market
- **WHEN** a run on an imported CSV pack finishes with a disciplined winning trade
- **THEN** no advisor was offered during the run and the balance SHALL be unchanged

### Requirement: Intel perks unlocked by completed missions
Perks SHALL unlock by the number of distinct missions finished on the device: Economic calendar at 1, Fast wire I at 2, Fast wire II at 4. Perks SHALL be modeled on legal products and SHALL NOT be described as insider information. Each unlocked perk SHALL be a toggle on the briefing, on by default, and the chosen perks SHALL be part of the run's start options and record.

#### Scenario: Locked perk
- **WHEN** the player has finished one mission
- **THEN** the briefing SHALL offer the calendar and SHALL show Fast wire I as locked with its requirement

### Requirement: Economic calendar
With the calendar enabled, the state view SHALL list the decision step of each upcoming scheduled release (rate decisions) without its expectation, actual value or tone.

#### Scenario: Rate decision on the calendar
- **WHEN** m02 starts with the calendar enabled
- **THEN** the news panel SHALL show the bar of the coming rate decision and nothing about its content

### Requirement: Fast wire
With a fast wire of lead L (1 or 2 bars), a news item SHALL become visible at step release_step − L, marked as early until its public release step. Only the headline SHALL be shown early. Bars, gaps and reactions SHALL be identical with and without the perk, and no bar after the current step SHALL be revealed. This is the only specified exception to the look-ahead rule, and the run SHALL be labeled "with intel" in the debrief and the run record.

#### Scenario: Headline one bar early
- **WHEN** m01 runs with Fast wire I and the denial's release step is r
- **THEN** the denial headline SHALL be visible at step r − 1, marked early
- **AND** the bars SHALL be identical to the same seed without the perk
