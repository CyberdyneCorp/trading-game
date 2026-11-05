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
The client SHALL keep an advisor point balance per device, starting at 3 and capped at 10. A run's advice allowance SHALL be the balance at start. When a run ends, the balance SHALL become balance − advice used + points earned, applied exactly once per run even after reloads or replays. A run that can still be resumed and is replaced by starting another run SHALL be settled then, with the advice it used and no points earned. Practice runs on known scenarios (imported or loaded packs) SHALL start without advice and SHALL NOT change the balance.

#### Scenario: Settlement is idempotent
- **WHEN** a finished run's debrief is opened, the page is reloaded and the debrief is opened again
- **THEN** the balance SHALL have changed only once

#### Scenario: Abandoning a run does not refund its advice
- **WHEN** the player uses 2 reads in a run, goes back to the menu and starts a new run (also after a reload)
- **THEN** the new run SHALL start with the balance minus 2, and finishing it SHALL NOT charge the abandoned run again

#### Scenario: Practice on an imported market
- **WHEN** a run on an imported CSV pack finishes with a disciplined winning trade
- **THEN** no advisor was offered during the run and the balance SHALL be unchanged

### Requirement: Intel perks unlocked by completed missions
Perks SHALL unlock by the number of distinct built-in missions finished on the device (in any mode; repeats and imported markets do not count): Economic calendar at 1, Fast wire I at 2, Fast wire II at 4. Perks SHALL be modeled on legal products and SHALL NOT be described as insider information. Each unlocked perk SHALL be a toggle on the free-play briefing, on by default, each locked perk SHALL be listed there with its requirement, and the chosen perks SHALL be part of the run's start options and record (with both fast wires on, the lead is the longer one). Campaign missions and practice runs on known scenarios SHALL start without perks, so campaign runs stay comparable; the campaign briefing SHALL say so once a perk is unlocked.

#### Scenario: Locked perk
- **WHEN** the player has finished one mission
- **THEN** the briefing SHALL offer the calendar and SHALL show Fast wire I as locked with its requirement

#### Scenario: Campaign mission with perks unlocked
- **WHEN** a player who has finished four distinct missions starts a campaign mission
- **THEN** its briefing SHALL offer no perk toggle and the run SHALL start with no fast wire and no calendar

#### Scenario: Perk toggled off
- **WHEN** the player turns Fast wire I off on the briefing before opening the terminal
- **THEN** the run SHALL start without a news lead and its record SHALL say so

### Requirement: Economic calendar
With the calendar enabled, the state view SHALL list the decision step of each upcoming scheduled release (rate decisions) that is not visible yet, without its expectation, actual value or tone. Once a fast wire or the release makes the headline visible, it SHALL leave the calendar.

#### Scenario: Rate decision on the calendar
- **WHEN** m02 starts with the calendar enabled
- **THEN** the news panel SHALL show the bar of the coming rate decision and nothing about its content

### Requirement: Fast wire
With a fast wire of lead L (1 or 2 bars), a news item SHALL become visible at step release_step − L, marked as early until its public release step. Only the headline SHALL be shown early. Bars, gaps and reactions SHALL be identical with and without the perk, and no bar after the current step SHALL be revealed. This is the only specified exception to the look-ahead rule, and the run SHALL be labeled "with intel" in the debrief and the run record (the journal shows the label). Auto-advance SHALL pause when a headline arrives early, as it does on a release.

#### Scenario: Headline one bar early
- **WHEN** m01 runs with Fast wire I and the denial's release step is r
- **THEN** the denial headline SHALL be visible at step r − 1, marked early
- **AND** the bars SHALL be identical to the same seed without the perk

### Requirement: Re-simulation with the recorded start options
A run SHALL be re-simulated (on resume and by the server) with the start options recorded when it started: advice allowance, perks, and campaign capital and leverage cap. The server SHALL take them from the run's first sync, SHALL reject a later sync of the same run with different options as a conflict, SHALL refuse options outside what a client can start (allowance above 10, a lead above 2), and SHALL report whether the run was played with intel.

#### Scenario: Server replay of a run with perks
- **WHEN** a client syncs a run started with an allowance of 2, Fast wire I, the calendar and a campaign capital, with its digest
- **THEN** the server's digest SHALL match the client's and the run SHALL be listed "with intel"
