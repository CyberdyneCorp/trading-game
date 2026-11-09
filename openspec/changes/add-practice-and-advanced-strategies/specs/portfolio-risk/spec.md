## ADDED Requirements

### Requirement: Portfolio view
The terminal SHALL offer a portfolio view, with a summary in the rail, listing:
- the perpetual position and each option position, with quantity, value, P&L, delta, gamma, theta and vega;
- the totals: net delta in units and notional, the P&L from gamma of a 1% move, vega per volatility point, theta per bar and the next funding payment.

Every number SHALL come from the engine's valuation of the run's holdings, and the client SHALL compute none. The view SHALL refresh on every state change and drop replies to older requests.

#### Scenario: Engine numbers
- **WHEN** the player holds a long straddle and a short perpetual hedge
- **THEN** the view's net delta SHALL equal the engine's `risk.deltaUnits`, and the perpetual line SHALL show delta 1 per unit

#### Scenario: Next funding shown
- **WHEN** the player holds a short perpetual and the published next funding rate is positive
- **THEN** the view SHALL show the next funding payment as received, with the engine's amount

### Requirement: Per-mission risk limits
A mission MAY declare risk limits in basis points of equity:
- `deltaBp` on |net delta notional|;
- `gammaBp` on the P&L from gamma of a 1% move;
- `vegaBp` on |vega per point|;
- `thetaBp` on theta burn per bar.

The briefing SHALL list them. When a total exceeds its limit, the portfolio view SHALL mark it and the rail SHALL show a warning naming the limit. A warning SHALL NOT block any order. Missions without declared limits SHALL show only their existing limits.

#### Scenario: Delta above the limit
- **WHEN** m18's net delta notional exceeds 15% of equity
- **THEN** the portfolio view SHALL mark net delta and the rail SHALL warn that the delta limit is exceeded, and orders SHALL still be accepted

### Requirement: Hedge to neutral
The portfolio view SHALL offer "Hedge delta". It proposes a perpetual order for the opposite of the net delta in units, rounded toward zero to the lot, through the normal ticket with the plan dialog and a confirmation. It SHALL never be sent automatically.

#### Scenario: Hedge quantity
- **WHEN** the net delta is +10.37 units and the lot is 0.1
- **THEN** the proposed order SHALL be a sell of 10.3 units, sent only after the player confirms
