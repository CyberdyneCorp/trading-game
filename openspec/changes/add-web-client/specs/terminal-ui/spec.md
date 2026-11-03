# Spec Delta

## Purpose

Defines the client-independent user-experience contract of the retro trading terminal — layout, visual system, order ticket, required states, accessibility and localization — that web, desktop and iPad implement natively.

## ADDED Requirements

### Requirement: Terminal layout on large screens
On screens at least 1024 points wide in landscape, the terminal SHALL show: a top bar (market alias, mode label, timeframe, step counter, offline/sync status, advance controls); a dominant chart (candles, volume, entry/stop/target/liquidation lines, option strikes on demand); a right rail (equity, wallet balance, available balance, position side and size, leverage, liquidation price and distance, next funding rate, planned risk, mission objective); a lower area (order ticket, option chain, fills, plan, journal); and a bottom strip (knowledge cards, released news).

#### Scenario: Liquidation always visible
- **WHEN** the player has an open perpetual position
- **THEN** its liquidation price and distance from last close SHALL be visible in the right rail and as a chart line

### Requirement: Visual system
Clients SHALL use the tokens background `#0B1010`, panel `#121C1B`, accent `#39FF88`, text `#E3F5EA`, attention `#FFC857`, info `#4FD8FF`, a monospace font for all numbers and tables and a pixel font only for titles. Scanlines, bloom and noise SHALL be optional and off when the OS requests reduced motion or the user disables them. Clients SHALL NOT use flicker effects. Text contrast SHALL meet WCAG 2.2 AA, measured.

#### Scenario: Reduced motion
- **WHEN** the OS reduced-motion setting is on
- **THEN** CRT effects and animated transitions SHALL be disabled

### Requirement: Gain/loss not encoded by color alone
Every gain, loss, long, short, buy or sell indication SHALL include a sign, label or icon in addition to color, and a colorblind-safe palette option SHALL be available.

#### Scenario: Monochrome check
- **WHEN** the UI is rendered in grayscale
- **THEN** long vs short and gain vs loss SHALL remain distinguishable

### Requirement: Distinct actions and acknowledgements
Submit order, close position and advance market SHALL be visually and spatially distinct controls. Order acknowledgement ("accepted") and execution ("filled", "partially filled") SHALL be separate messages, and rejections SHALL show the engine reason in plain language.

#### Scenario: Rejection reason shown
- **WHEN** an order is rejected with `insufficient_margin`
- **THEN** the UI SHALL explain the required and available margin

### Requirement: Order ticket for perpetuals
The ticket SHALL accept size as quantity, notional, % of equity or risk budget; SHALL require a leverage within the mission cap; and SHALL show before submission: estimated entry, initial margin, fees, liquidation price, planned risk to stop, and a warning when the stop lies beyond the liquidation price. The phrase "maximum guaranteed loss" SHALL NOT be used; the term is "planned risk".

#### Scenario: Live recalculation
- **WHEN** the player changes leverage from 3× to 10×
- **THEN** margin and liquidation price in the ticket SHALL update before submission

### Requirement: Stop and target tools
Stop-loss and take-profit fields (new orders and protection of an open position) SHALL offer one-click presets — stop at 1× and 2× ATR(14), beyond the 10-bar swing low/high, at 1% and 2%; target at 1R, 2R and 3R of the stop distance — and a "mark on chart" mode where a click on the price pane sets the level (Escape cancels; Space SHALL NOT advance while marking). Each level SHALL show its distance from the entry in price, percent and ATR multiples, and the target SHALL show its reward/risk ratio. A stop on the profit side or a target on the loss side SHALL be flagged and block submission. Draft entry, stop, target and estimated liquidation SHALL be previewed on the chart before submission. An open position without a reduce-only stop or trailing stop SHALL show an "unprotected" warning. Presets SHALL be disabled while their input (e.g. ATR) is warming up.

#### Scenario: ATR stop with a 2R target
- **WHEN** the player clicks "2×ATR" and then "2R" on a buy ticket
- **THEN** the stop SHALL be two ATRs below the estimated entry, the target twice that distance above it, and the ticket SHALL show "R:R 1:2.0"

#### Scenario: Stop on the wrong side
- **WHEN** a buy ticket has a stop above the estimated entry
- **THEN** the ticket SHALL explain the problem and the submit button SHALL be disabled

### Requirement: Indicator panes
The chart SHALL offer SMA20, EMA50 and session VWAP as price overlays and a lower pane selectable between RSI(14) with 30/70 guides, MACD(12,26,9) with histogram, ADX(14) with +DI/−DI and a 25 guide, or volume only. Values SHALL come from the engine (spec indicators) and appear in the hover readout formatted for the locale.

#### Scenario: Switch oscillator
- **WHEN** the player selects MACD in the lower pane
- **THEN** the pane SHALL draw the MACD line, signal and histogram for the visible bars and the readout SHALL show the hovered values

### Requirement: Option chain and option ticket
The option chain SHALL list listed series by expiry and strike with bid, ask, IV and delta, labeled "simulated premiums". The option ticket SHALL show premium, max loss for buys, margin for writes, breakeven, a payoff-at-expiry diagram and Greeks before submission.

#### Scenario: Payoff preview
- **WHEN** the player selects a long put
- **THEN** a payoff diagram at expiry with breakeven SHALL be shown before submission

### Requirement: Required states
Clients SHALL implement distinct states for: loading, no data, indicator warming up, order rejected, partial fill, insufficient capacity, insufficient margin, liquidation warning, liquidated, margin call, option expired, download interrupted, incompatible pack, session expired, sync pending and device conflict.

#### Scenario: Warming up indicator
- **WHEN** an enabled indicator has not completed warm-up
- **THEN** the chart SHALL show a "warming up (n bars)" state instead of a value

### Requirement: Accessible chart
The chart SHALL have an equivalent accessible table of closed bars, a text summary and per-candle keyboard focus. Assistive technologies SHALL never receive data for bars after the current step.

#### Scenario: Screen reader and future bars (UX01)
- **WHEN** a screen reader traverses the chart table
- **THEN** the last row SHALL be the current closed bar

### Requirement: Localization and numbers
All user-facing strings SHALL come from i18n catalogs (PT-BR first, en-US second). Numbers SHALL be formatted from fixed-point values using the declared scale, with locale-aware separators for display only; parsing and storage SHALL be locale-independent.

#### Scenario: Locale does not change results
- **WHEN** the UI language switches from PT-BR to en-US mid-run
- **THEN** state and digest SHALL be unchanged and only formatting SHALL differ

### Requirement: Simulation labels
Synthetic bars, branch segments, model-priced option premiums, capacity approximations and ambiguous intrabar fills SHALL be visibly labeled wherever they are shown.

#### Scenario: Branch label
- **WHEN** the chart shows bars after a branch point
- **THEN** those bars SHALL be marked as simulated
