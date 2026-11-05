# Spec Delta

## Purpose

Defines how player orders on perpetual futures and options are accepted, triggered, filled, charged and cancelled, including the conservative policy used when only OHLC bars are available.

## ADDED Requirements

### Requirement: Supported order types
The engine SHALL support market, limit, stop-market, take-profit limit, trailing stop-market and bracket (an entry carrying an attached stop and/or take-profit that become an OCO exit pair) orders on perpetual futures, and market and limit orders on options. Any perpetual order MAY be flagged reduce-only; take-profit orders are always reduce-only. Unsupported types SHALL be rejected with reason `unsupported_order_type`.

#### Scenario: Stop-limit not available
- **WHEN** a stop-limit order is submitted
- **THEN** it SHALL be rejected with reason `unsupported_order_type`

### Requirement: Order lifecycle
Every order SHALL be in exactly one of: `accepted`, `partially_filled`, `filled`, `cancelled`, `rejected`, `expired`, `triggered` (stop/trailing awaiting fill; reserved, since in OHLC mode a stop executes within the bar that triggers it). Each transition SHALL emit an order event with order id, step, event sequence, new status, remaining quantity, requested price and reason, and each execution SHALL emit a fill event with quantity, fill price, requested price, fee, realized P&L and approximation flags. Acceptance SHALL NOT imply execution.

#### Scenario: Accepted is not filled
- **WHEN** a limit order is submitted far from the market
- **THEN** an `accepted` event SHALL be emitted
- **AND** no `filled` event SHALL be emitted until a later step meets its fill conditions

### Requirement: Market orders
A market order SHALL fill starting at the first eligible price (next bar open) adjusted by half-spread and slippage against the player, limited by available capacity. Any remainder SHALL be cancelled (IOC) with reason `insufficient_capacity`, except for a reduce-only market order (including "close position"): its capacity-limited remainder SHALL stay working and execute at the following bar opens until it is filled, the position is flat or the player cancels it.

#### Scenario: Partial market fill
- **WHEN** a market order exceeds the step's remaining capacity
- **THEN** it SHALL fill up to capacity and cancel the rest with reason `insufficient_capacity`

#### Scenario: Closing a position larger than a bar's capacity
- **WHEN** the player closes a long of 10 and only 4 contracts of capacity are available per bar
- **THEN** 4 SHALL fill at the next open and the close order SHALL stay partially filled, filling 4 and then 2 at the following opens until the position is flat

### Requirement: Limit orders
A limit order SHALL never fill at a price worse than its limit. Touching the limit price SHALL NOT guarantee a fill; the fill rule for OHLC bars SHALL require the price to trade through the limit by at least one tick unless the scenario declares a different touch policy. Resting limits SHALL keep priority by price then sequence; modifying price or increasing size SHALL reset priority.

#### Scenario: Never worse than limit (SIM07)
- **WHEN** a buy limit at 100 is eligible on a bar that opens at 98
- **THEN** it SHALL fill at no more than 100 (at 98 plus costs bounded by the limit)

### Requirement: Stop-market and take-profit
A stop-market order SHALL convert to a market order when triggered and SHALL NOT guarantee the stop price. Once triggered it SHALL follow the market-order capacity rule: a reduce-only stop (or trailing stop) cut short by capacity SHALL keep its remainder working at the following bar opens, never resting at its trigger again, and any other remainder SHALL be cancelled with reason `insufficient_capacity` (execution policy 15). A take-profit SHALL be a limit order on the exit side. If the bar opens beyond the stop price (gap), the fill SHALL be based on the open, not the stop price. This SHALL also apply to bracket legs attached by an entry filled at that open: a stop leg the open is already beyond (e.g. computed from the previous close) SHALL fill at the open, never at a trigger price the bar did not trade after the open.


#### Scenario: Bracket stop breached at the entry's open
- **WHEN** a market buy with a stop at 97 is placed at a close of 100 and the next bar opens at 90 (high 92)
- **THEN** the entry SHALL fill at the open and the stop leg SHALL fill at the open with the gap flag, not at 97
#### Scenario: Stop cut short by capacity
- **WHEN** a long of 10 has a reduce-only stop at 95, a bar trades through 95 with capacity for 4, and the next bar opens at 99 without reaching 95
- **THEN** 4 SHALL fill from the trigger on the first bar and 4 SHALL fill from the next open (99, adjusted for costs), with requested price 95, until the position is flat

#### Scenario: Gap through stop (SIM04)
- **WHEN** a long has a stop at 95 and the next bar opens at 90
- **THEN** the stop SHALL fill from 90 adjusted for spread, slippage and fees
- **AND** the debrief data SHALL record requested trigger 95 and executed price

### Requirement: Trailing stop
A trailing stop-market SHALL track the best close observed since activation by a fixed distance or percentage. In OHLC mode, a trailing update computed at the close of bar `t` SHALL only be effective from bar `t+1`. A percentage trail (execution policy 16) SHALL take its distance as a rate of each close in (0, 1), with the distance rounded up and the trigger rounded outward onto the tick (down for a sell, up for a buy); an order SHALL give exactly one of a distance or a rate, otherwise it SHALL be rejected with `invalid_tick`.

#### Scenario: Trailing uses closes
- **WHEN** a bar has a high that would have tightened the trail but closes lower
- **THEN** the trail SHALL be updated from the close, not the high

#### Scenario: Percentage trail
- **WHEN** a long places a 2% trailing stop at a close of 100 and the next bar closes at 110
- **THEN** the trigger SHALL be 98 and then 107.80, effective from the bar after that close

### Requirement: Brackets, OCO and reduce-only
Exit orders of a bracket SHALL be reduce-only and SHALL share the filled entry quantity. Partial entry fills SHALL create protection only for the filled quantity. A fill on one exit SHALL reduce its sibling by the same quantity. When the position reaches zero, all other reduce-only orders for that market SHALL be cancelled with reason `position_closed`, after the fill event that closed it; the order whose fill closed the position SHALL NOT be reported as cancelled when it filled completely. When its quantity was larger than the position it closed (e.g. a bracket leg left at its size after a partial close), its unfilled remainder SHALL be cancelled with reason `position_closed` on the same bar, right after its own fill update (execution policy v14), so no exit stays working without a position. A reduce-only order SHALL never increase or flip a position.

A working perpetual exit (a reduce-only stop-market or take-profit) MAY be moved to a new trigger with the `MoveOrder` command (execution policy v13): the order keeps its id, remaining quantity, bracket parent and OCO group, an order update with reason `order_moved` is emitted, and the same stop-beyond-liquidation warning as on placement applies. Moving an unknown or inactive order SHALL be rejected with `unknown_order`, any other order type with `unsupported_order_type`, and a price off the tick with `invalid_tick`.

#### Scenario: Partial OCO (SIM06)
- **WHEN** an entry for 10 contracts fills 6, then the stop fills 4
- **THEN** the take-profit SHALL be reduced to 2 and the position SHALL be 2
- **AND** no order SHALL be able to flip the position to the opposite side

#### Scenario: A moved bracket stop keeps its OCO link
- **WHEN** the stop leg of a bracket is moved closer to the market and then fills
- **THEN** it SHALL fill at its new trigger as the same order, and its take-profit sibling SHALL be cancelled with it

#### Scenario: An exit larger than the position is not left working
- **WHEN** a bracket long of 10 is partly closed by a market sell of 1, and its take-profit for 10 then reaches its price
- **THEN** the take-profit SHALL fill 9 and close the position, and on that same bar its remainder of 1 SHALL be cancelled (`position_closed`) after its fill update, together with the stop

#### Scenario: Closing order is filled, not cancelled
- **WHEN** a reduce-only stop fills and closes the position while its take-profit sibling is resting
- **THEN** the stop SHALL have no `cancelled` update, and the take-profit's `cancelled` (`position_closed`) update SHALL come after the stop's fill event

### Requirement: Conservative OHLC intrabar policy
When only OHLC data is available the engine SHALL: process the open (gaps) first; for an existing position whose stop and target are both reachable within the same bar, fill the stop; for an entry filled intrabar, not credit a favorable exit in the same bar but allow the adverse stop; evaluate liquidation against the adverse extreme before any favorable exit; never infer an unobserved path. Every fill decided under ambiguity SHALL carry the flag `ambiguousIntrabar`.

#### Scenario: Stop and target in same bar (SIM05)
- **WHEN** a bar's range contains both the stop and the take-profit of an open position
- **THEN** the stop SHALL fill, the take-profit SHALL be cancelled as sibling
- **AND** the fill SHALL be flagged `ambiguousIntrabar`

### Requirement: Costs
Each fill SHALL charge the scenario's maker fee (limits and take-profits) or taker fee (market and stop fills) on notional, and taker fills SHALL pay half-spread plus slippage against the player, rounded onto the tick grid. Quantities that are not a multiple of the lot size (or below the minimum quantity for non-reduce-only orders) SHALL be rejected with reason `invalid_quantity`; client sizing helpers round down to the lot before submitting. Prices and trailing distances not on a tick, and trailing rates outside (0, 1), SHALL be rejected with reason `invalid_tick`.

#### Scenario: Lot rounding
- **WHEN** a risk-sized order computes 16.66 contracts with a lot size of 0.1
- **THEN** the client SHALL submit 16.6
- **AND** an order submitted with 16.66 SHALL be rejected with reason `invalid_quantity`

### Requirement: Shared capacity
The sum of all player fills in a step SHALL NOT exceed the step's simulated capacity (the scenario's capacity fraction of bar volume, rounded down to the lot). In OHLC mode capacity derived from bar volume is a retrospective approximation: a fill limited by it SHALL be flagged `capacityFromBarVolume` (`capacityLimited` in the C ABI JSON), and capacity SHALL NOT be shown to the player before the bar completes. The step's phases keep the bar's own order (open, adverse exits, favorable exits, entries); within a phase, orders competing for capacity SHALL be served by price priority, then sequence (execution policy 16): the level the market reaches first (at the open, along the gap from the previous close; intrabar, nearest to the open), then the earlier command sequence (bracket legs carry their entry's), then the order id. Market orders at the open SHALL be served by sequence.

#### Scenario: Capacity exhausted
- **WHEN** two orders compete for capacity in one step
- **THEN** they SHALL be filled by price priority then sequence until capacity is exhausted

#### Scenario: Better price first, whatever the placement order
- **WHEN** a buy limit at 98 is placed before a buy limit at 99, and a bar falling from 100 to 97 has capacity for only one of them
- **THEN** the limit at 99 SHALL fill and the limit at 98 SHALL keep resting

### Requirement: Margin check on acceptance
An order that would open or increase a position SHALL be rejected with reason `insufficient_margin` if available balance cannot cover its initial margin plus estimated taker fees at the order's worst admissible price (its limit or trigger price; for market orders the last close plus twice the half-spread and slippage). Accepted resting orders SHALL reserve that margin until they fill or are cancelled, and the margin SHALL be checked again at fill. Reduce-only orders SHALL NOT require margin.

#### Scenario: Insufficient margin
- **WHEN** a player with 100 credits available submits a 10× long whose initial margin is 150
- **THEN** the order SHALL be rejected with reason `insufficient_margin` and no margin SHALL be reserved
