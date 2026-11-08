## ADDED Requirements

### Requirement: Time in force
From execution policy v18, every order SHALL carry a time in force: `gtc` (the default and the only behavior before v18), `bars` with N ≥ 1, `ioc` or `fok`.
- **`gtc`.** An order SHALL work until it fills, is cancelled, the run ends or (options) its series enters the settlement window.
- **`bars`.** An order SHALL be eligible on bars placed_step+1 to placed_step+N. Any remainder at the close of bar placed_step+N SHALL be expired with reason `time_in_force`, after that bar's close-phase fills.
- **`ioc`.** A limit order SHALL be eligible only in the next bar's open phase (the perpetual open phase, or the option open phase for an option limit). It SHALL fill what the open's price and capacity allow, and its remainder SHALL be cancelled with reason `time_in_force` right after that phase.
- **`fok`.** A limit order SHALL have the same eligibility as `ioc`, SHALL fill its whole quantity in that phase or nothing, and SHALL be cancelled with reason `time_in_force` when it cannot fill completely after the orders served before it.

**Refusals.** The engine SHALL refuse with `invalid_time_in_force` any of these:
- `ioc` or `fok` on a market, stop-market, take-profit or trailing order;
- `bars` with N = 0 or beyond the run's last bar;
- any time in force on a bracket child.

A bracket entry with `bars` SHALL attach its children as `gtc`.

#### Scenario: IOC limit not reached at the open
- **WHEN** a buy limit with `ioc` at 99 is placed at bar t and bar t+1 opens at 100 and trades down to 98
- **THEN** the order SHALL be cancelled with `time_in_force` after the open phase and SHALL NOT fill intrabar

#### Scenario: FOK with too little capacity
- **WHEN** a `fok` limit for 50 units meets an open whose remaining capacity is 30 units
- **THEN** nothing SHALL fill and the order SHALL be cancelled with `time_in_force`

#### Scenario: Good for N bars
- **WHEN** a stop-market with `bars` 3 placed at step 10 has not triggered by the close of bar 13
- **THEN** it SHALL be expired with `time_in_force` at that close

#### Scenario: Refused combination
- **WHEN** a trailing stop is placed with `ioc`
- **THEN** the command SHALL be refused with `invalid_time_in_force` and the state SHALL be unchanged

### Requirement: Player-built OCO groups
From execution policy v18, a `LinkOco` command SHALL link two working orders of the run, perpetual or option, in any combination, into one group stored apart from bracket groups.
- **Refusals.** The command SHALL be refused with `invalid_oco`, changing nothing, when the two ids are the same, an order is unknown or not working, an order is a bracket child, or an order is already linked.
- **First fill.** When either linked order fills any quantity, the other's remainder SHALL be cancelled with `oco_sibling_filled`. When both are reduce-only exits of the same position, the sibling SHALL instead be reduced by the filled quantity.
- **Same bar.** Within one bar the bar's phase order and the policy v16 priority SHALL decide which order executes first, and its sibling SHALL be cancelled before its own turn.
- **Dissolving.** A player cancel or a time-in-force expiry of one linked order SHALL dissolve the link and leave the other working. `MoveOrder` SHALL keep the link.

#### Scenario: Breakout pair on one wide bar
- **WHEN** a linked buy-stop above and sell-stop below the range are both crossed by the next bar
- **THEN** exactly one SHALL fill, the one the bar's execution order reaches first, and the other SHALL be cancelled with `oco_sibling_filled`

#### Scenario: Partial exit keeps protection
- **WHEN** a linked reduce-only take-profit for 10 units fills 4 units while its linked reduce-only stop is for 10 units
- **THEN** the stop SHALL remain working for 6 units

#### Scenario: Cancel dissolves
- **WHEN** the player cancels one of two linked orders
- **THEN** the other SHALL keep working, unlinked

### Requirement: Policy v18 compatibility and versioning
Execution policy v18 SHALL be the only version change for time in force and OCO groups.
- The order's new fields SHALL be appended to the checkpoint serialization (format 6) and to the state digest.
- A log without the new fields SHALL execute exactly as under v17: every existing golden's event digest SHALL be unchanged, and its state digest SHALL change only through the policy number and the appended defaults.
- The C ABI SHALL add functions for the new fields without changing existing functions, so `kAbiVersion` stays 1.
- Run files and the shared action applier SHALL accept the `tif`/`tifBars` fields and the `oco` action.

#### Scenario: Event digests unchanged
- **WHEN** every existing golden's script is replayed under policy v18
- **THEN** its event digest SHALL equal its frozen v17 value

#### Scenario: Older checkpoint
- **WHEN** a client restores a format 5 checkpoint with a v18 engine
- **THEN** the restore SHALL be refused as a format mismatch and the client SHALL replay the run on its pinned build
