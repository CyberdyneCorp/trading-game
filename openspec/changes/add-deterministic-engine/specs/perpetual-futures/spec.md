# Spec Delta

## Purpose

Defines the linear perpetual futures contract the player trades long and short: positions, isolated margin, leverage, liquidation and funding, with exact and explainable accounting.

## ADDED Requirements

### Requirement: Contract model
Each scenario SHALL define one linear, quote-margined perpetual contract with contract multiplier, tick size, lot size, minimum quantity, maximum leverage, maintenance-margin tiers, maker/taker fees and funding interval (in steps). Profit and loss SHALL be linear in price: `pnl = side × quantity × multiplier × (exitPrice − entryPrice)` where `side` is +1 for long and −1 for short.

#### Scenario: Symmetric long and short (FUT01)
- **WHEN** a long and a short of equal quantity are opened at 100 and closed at 110 with zero costs
- **THEN** the long SHALL realize +10 × quantity × multiplier and the short SHALL realize −10 × quantity × multiplier

### Requirement: One-way position mode
The player SHALL hold at most one net position per market. An order on the opposite side SHALL first reduce the position; any excess (when not reduce-only) SHALL open a position on the opposite side, subject to the margin check. The entry price of an increased position SHALL be the quantity-weighted average of fills.

#### Scenario: Flip
- **WHEN** a player long 5 submits a non-reduce-only market sell of 8
- **THEN** 5 SHALL be closed with realized P&L and a short of 3 SHALL be opened if margin allows

### Requirement: Isolated margin and leverage
Each position SHALL have isolated margin. Initial margin SHALL equal `notional / leverage` where notional uses the fill price. Leverage SHALL be chosen per market from 1× up to the lower of the contract maximum and the mission cap. The player SHALL be able to add margin to a position at any decision point and to remove margin only while the position remains above initial margin.

#### Scenario: Leverage above mission cap
- **WHEN** the mission cap is 5× and the player selects 10×
- **THEN** the change SHALL be rejected with reason `leverage_above_cap`

### Requirement: Account balances
The engine SHALL expose wallet balance, unrealized P&L (marked at the last close), equity (wallet balance + unrealized P&L), position margin, order margin reserved, option margin, and available balance (wallet balance − position margin − order margin − option margin, floored at zero). Realized P&L, fees and funding SHALL each be reported separately and SHALL reconcile exactly with wallet balance changes.

#### Scenario: Exact reconciliation (SIM08)
- **WHEN** any sequence of fills, fees and funding payments is applied
- **THEN** initial balance + Σ realized P&L − Σ fees + Σ funding received − Σ funding paid + Σ option cash flows SHALL equal the final wallet balance exactly

### Requirement: Liquidation price
The engine SHALL compute and expose, for every open position, the liquidation price at which position margin plus unrealized P&L equals maintenance margin, using the tier for the position's notional. The value SHALL be recomputed after every fill, margin change and funding payment and SHALL be shown at every decision point.

#### Scenario: Liquidation price exposed
- **WHEN** a 10× long is opened
- **THEN** its liquidation price SHALL be available in the state before the next decision point

### Requirement: Mark price
When the scenario includes mark-price bars, unrealized P&L, margin checks and liquidation SHALL use the mark price, and fills SHALL use the traded (last) price, as on real venues. When it does not, the engine SHALL use last-price bars for both and SHALL flag the scenario `markFromLastPrice`.

#### Scenario: Wick on last price only
- **WHEN** the last-price low touches a long's liquidation price but the mark-price low stays above it
- **THEN** the position SHALL NOT be liquidated

### Requirement: Liquidation
In OHLC mode a long SHALL be liquidated when the bar's low (of the mark price when available, else last price) reaches its liquidation price and a short when the bar's high reaches it, evaluated before any favorable exit in that bar. The position SHALL be closed at the liquidation price, or at the open if the bar gapped beyond it. The player's loss SHALL be capped at the position's isolated margin: the entire isolated margin SHALL be forfeited, and any shortfall SHALL be absorbed by a simulated insurance fund, never debited from the player. All reduce-only orders for the market SHALL be cancelled. A `liquidated` event SHALL record trigger, fill price, margin lost and the `ambiguousIntrabar` flag when applicable.

#### Scenario: Liquidation in a bar (FUT02)
- **WHEN** a long's liquidation price is 91 and the next bar's low is 90
- **THEN** the position SHALL be liquidated, the isolated margin SHALL be forfeited and wallet balance SHALL NOT go below what remains after losing that margin

#### Scenario: Stop beyond liquidation (FUT04)
- **WHEN** a long has a stop at 88 and its liquidation price is 91, and the bar's low is 87
- **THEN** liquidation SHALL occur first and the stop SHALL be cancelled
- **AND** the debrief SHALL explain that the stop sat beyond the liquidation price

### Requirement: Funding
At every funding boundary the engine SHALL transfer `quantity × multiplier × markPrice × fundingRate` for each open position: when the rate is positive longs pay and shorts receive; when negative, shorts pay and longs receive. Funding SHALL be taken from or added to the position's isolated margin, which SHALL trigger a liquidation-price recomputation. The funding rate SHALL come from the scenario's funding series or from its declared synthetic funding rule, and the rate applying at the next boundary SHALL be visible at the preceding decision point.

#### Scenario: Positive funding (FUT03)
- **WHEN** the funding rate is +0.01% and a long of notional 10,000 crosses a funding boundary
- **THEN** exactly 1 credit SHALL move from the long's isolated margin to the funding counterparty
- **AND** a short of the same notional SHALL receive exactly 1 credit

### Requirement: Bankruptcy ends the run
After each step, if the player has no open perpetual position, no open option position, no resting order that could open one, and available balance is below the initial margin plus fees of the minimum tradable quantity at the mission's leverage cap, the engine SHALL emit an `account_bankrupt` event, set the run status to `bankrupt` and reject every subsequent trading command with reason `run_bankrupt`.

#### Scenario: Losing all money is game over
- **WHEN** a liquidation leaves available balance below the margin needed for the smallest order
- **THEN** the run SHALL end with status `bankrupt` and the debrief SHALL open

#### Scenario: Still able to trade
- **WHEN** a liquidation leaves enough balance for at least the minimum order
- **THEN** the run SHALL continue

### Requirement: Non-negative balances
Wallet balance and available balance SHALL never be negative. Any operation that would make them negative SHALL be rejected or, for liquidation, bounded by the isolated margin.

#### Scenario: Gap past bankruptcy price
- **WHEN** a bar gaps far beyond a short's bankruptcy price
- **THEN** the player SHALL lose exactly its isolated margin and wallet balance SHALL remain non-negative
