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
A stop-market order SHALL convert to a market order when triggered and SHALL NOT guarantee the stop price. A take-profit SHALL be a limit order on the exit side. If the bar opens beyond the stop price (gap), the fill SHALL be based on the open, not the stop price.

#### Scenario: Gap through stop (SIM04)
- **WHEN** a long has a stop at 95 and the next bar opens at 90
- **THEN** the stop SHALL fill from 90 adjusted for spread, slippage and fees
- **AND** the debrief data SHALL record requested trigger 95 and executed price

### Requirement: Trailing stop
A trailing stop-market SHALL track the best close observed since activation by a fixed distance or percentage. In OHLC mode, a trailing update computed at the close of bar `t` SHALL only be effective from bar `t+1`.

#### Scenario: Trailing uses closes
- **WHEN** a bar has a high that would have tightened the trail but closes lower
- **THEN** the trail SHALL be updated from the close, not the high

### Requirement: Brackets, OCO and reduce-only
Exit orders of a bracket SHALL be reduce-only and SHALL share the filled entry quantity. Partial entry fills SHALL create protection only for the filled quantity. A fill on one exit SHALL reduce its sibling by the same quantity. When the position reaches zero, all other reduce-only orders for that market SHALL be cancelled with reason `position_closed`, after the fill event that closed it; the order whose fill closed the position SHALL end `filled` and SHALL NOT be reported as cancelled. A reduce-only order SHALL never increase or flip a position.

#### Scenario: Partial OCO (SIM06)
- **WHEN** an entry for 10 contracts fills 6, then the stop fills 4
- **THEN** the take-profit SHALL be reduced to 2 and the position SHALL be 2
- **AND** no order SHALL be able to flip the position to the opposite side

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
Each fill SHALL charge the scenario's maker fee (limits and take-profits) or taker fee (market and stop fills) on notional, and taker fills SHALL pay half-spread plus slippage against the player, rounded onto the tick grid. Quantities that are not a multiple of the lot size (or below the minimum quantity for non-reduce-only orders) SHALL be rejected with reason `invalid_quantity`; client sizing helpers round down to the lot before submitting. Prices and trailing distances not on a tick SHALL be rejected with reason `invalid_tick`.

#### Scenario: Lot rounding
- **WHEN** a risk-sized order computes 16.66 contracts with a lot size of 0.1
- **THEN** the client SHALL submit 16.6
- **AND** an order submitted with 16.66 SHALL be rejected with reason `invalid_quantity`

### Requirement: Shared capacity
The sum of all player fills in a step SHALL NOT exceed the step's simulated capacity (the scenario's capacity fraction of bar volume, rounded down to the lot). In OHLC mode capacity derived from bar volume is a retrospective approximation: a fill limited by it SHALL be flagged `capacityFromBarVolume` (`capacityLimited` in the C ABI JSON), and capacity SHALL NOT be shown to the player before the bar completes.

#### Scenario: Capacity exhausted
- **WHEN** two orders compete for capacity in one step
- **THEN** they SHALL be filled by price priority then sequence until capacity is exhausted

### Requirement: Margin check on acceptance
An order that would open or increase a position SHALL be rejected with reason `insufficient_margin` if available balance cannot cover its initial margin plus estimated taker fees at the order's worst admissible price (its limit or trigger price; for market orders the last close plus twice the half-spread and slippage). Accepted resting orders SHALL reserve that margin until they fill or are cancelled, and the margin SHALL be checked again at fill. Reduce-only orders SHALL NOT require margin.

#### Scenario: Insufficient margin
- **WHEN** a player with 100 credits available submits a 10× long whose initial margin is 150
- **THEN** the order SHALL be rejected with reason `insufficient_margin` and no margin SHALL be reserved
