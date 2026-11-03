# Spec Delta

## Purpose

Defines how player orders on perpetual futures and options are accepted, triggered, filled, charged and cancelled, including the conservative policy used when only OHLC bars are available.

## ADDED Requirements

### Requirement: Supported order types
The engine SHALL support market, limit, stop-market, take-profit limit, trailing stop-market and bracket (entry + OCO exit pair) orders on perpetual futures, and market and limit orders on options. Any order MAY be flagged reduce-only. Unsupported types SHALL be rejected with reason `unsupported_order_type`.

#### Scenario: Stop-limit not available
- **WHEN** a stop-limit order is submitted
- **THEN** it SHALL be rejected with reason `unsupported_order_type`

### Requirement: Order lifecycle
Every order SHALL be in exactly one of: `accepted`, `partially_filled`, `filled`, `cancelled`, `rejected`, `expired`, `triggered` (stop/trailing awaiting fill). Each transition SHALL emit an execution event with order id, step, event sequence, new status, filled quantity, fill price, fee, reason and approximation flags. Acceptance SHALL NOT imply execution.

#### Scenario: Accepted is not filled
- **WHEN** a limit order is submitted far from the market
- **THEN** an `accepted` event SHALL be emitted
- **AND** no `filled` event SHALL be emitted until a later step meets its fill conditions

### Requirement: Market orders
A market order SHALL fill starting at the first eligible price (next bar open) adjusted by half-spread and slippage against the player, limited by available capacity. Any remainder SHALL be cancelled (IOC) with reason `insufficient_capacity`.

#### Scenario: Partial market fill
- **WHEN** a market order exceeds the step's remaining capacity
- **THEN** it SHALL fill up to capacity and cancel the rest with reason `insufficient_capacity`

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
Exit orders of a bracket SHALL be reduce-only and SHALL share the filled entry quantity. Partial entry fills SHALL create protection only for the filled quantity. A fill on one exit SHALL reduce its sibling by the same quantity. When the position reaches zero, all reduce-only orders for that market SHALL be cancelled. A reduce-only order SHALL never increase or flip a position.

#### Scenario: Partial OCO (SIM06)
- **WHEN** an entry for 10 contracts fills 6, then the stop fills 4
- **THEN** the take-profit SHALL be reduced to 2 and the position SHALL be 2
- **AND** no order SHALL be able to flip the position to the opposite side

### Requirement: Conservative OHLC intrabar policy
When only OHLC data is available the engine SHALL: process the open (gaps) first; for an existing position whose stop and target are both reachable within the same bar, fill the stop; for an entry filled intrabar, not credit a favorable exit in the same bar but allow the adverse stop; evaluate liquidation against the adverse extreme before any favorable exit; never infer an unobserved path. Every fill decided under ambiguity SHALL carry the flag `ambiguousIntrabar`.

#### Scenario: Stop and target in same bar (SIM05)
- **WHEN** a bar's range contains both the stop and the take-profit of an open position
- **THEN** the stop SHALL fill, the take-profit SHALL be cancelled as sibling
- **AND** the fill SHALL be flagged `ambiguousIntrabar`

### Requirement: Costs
Each fill SHALL charge the scenario's maker or taker fee on notional, spread and slippage per the scenario cost model, and SHALL respect tick size and lot size. Quantities that do not meet lot size SHALL be rounded down; prices not on a tick SHALL be rejected for limit/stop orders with reason `invalid_tick`.

#### Scenario: Lot rounding
- **WHEN** a risk-sized order computes 16.66 contracts with a lot size of 0.1
- **THEN** the submitted quantity SHALL be 16.6

### Requirement: Shared capacity
The sum of all player fills in a step SHALL NOT exceed the step's simulated capacity. In OHLC mode capacity derived from bar volume is a retrospective approximation and SHALL be flagged `capacityFromBarVolume`; it SHALL NOT be shown to the player before the bar completes.

#### Scenario: Capacity exhausted
- **WHEN** two orders compete for capacity in one step
- **THEN** they SHALL be filled by price priority then sequence until capacity is exhausted

### Requirement: Margin check on acceptance
An order that would open or increase a position SHALL be rejected with reason `insufficient_margin` if available balance cannot cover its initial margin plus estimated fees at the order's worst admissible price. Reduce-only orders SHALL NOT require margin.

#### Scenario: Insufficient margin
- **WHEN** a player with 100 credits available submits a 10× long whose initial margin is 150
- **THEN** the order SHALL be rejected with reason `insufficient_margin` and no margin SHALL be reserved
