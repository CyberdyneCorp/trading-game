# Spec Delta

## Purpose

Defines the client-independent user-experience contract of the retro trading terminal — layout, visual system, order ticket, required states, accessibility and localization — that web, desktop and iPad implement natively.

## ADDED Requirements

### Requirement: Terminal layout on large screens
On screens at least 1024 points wide in landscape, the chart and right rail SHALL sit side by side (below 1280 the lower area folds into two columns and the page scrolls; below 1024 every panel stacks at its natural height without overlapping), and the terminal SHALL show: a top bar (market alias, mode label, timeframe, step counter, offline/sync status, advance controls: an advance-one-bar button and auto-advance; five bars stay on `Shift+Space` without a button); a dominant chart (candles, volume, entry/stop/target/liquidation lines, option strikes on demand); a right rail (equity, wallet balance, available balance, position side and size, leverage, liquidation price and distance, next funding rate, planned risk, mission objective); a lower area (order ticket, option chain, fills, plan, journal); and a bottom strip (knowledge cards, released news).

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
Submit order, close position and advance market SHALL be visually and spatially distinct controls. Order acknowledgement ("accepted") and execution ("filled", "partially filled") SHALL be separate messages, and rejections SHALL show the engine reason in plain language. The event feed and order list SHALL name each order's type (MKT, LMT, STOP, TP, TRAIL) and each option contract by kind, strike and expiry (e.g. "C 100 · exp 96"), also after the order is gone or the series expired. A fill with no order (the engine's forced buy-back of written options in a margin call) SHALL read as a forced buy-back, not as an order number.

#### Scenario: Rejection reason shown
- **WHEN** an order is rejected with `insufficient_margin`
- **THEN** the UI SHALL explain the required and available margin

### Requirement: Order ticket for perpetuals
The ticket SHALL accept size as quantity, notional, % of equity or risk budget; SHALL require a leverage within the mission cap (a typed value above it is clamped to the cap); and SHALL show before submission: estimated entry, initial margin, fees, liquidation price, planned risk to stop, and a warning when the stop lies beyond the liquidation price. The phrase "maximum guaranteed loss" SHALL NOT be used; the term is "planned risk".

#### Scenario: Live recalculation
- **WHEN** the player changes leverage from 3× to 10×
- **THEN** margin and liquidation price in the ticket SHALL update before submission

### Requirement: Stop and target tools
Stop-loss and take-profit fields (new orders and protection of an open position) SHALL offer one-click presets — stop at 1× and 2× ATR(14), beyond the 10-bar swing low/high, at 1% and 2%; target at 1R, 2R and 3R of the stop distance — and a "mark on chart" mode where a click on the price pane sets the level (Escape cancels; Space SHALL NOT advance while marking). Each level SHALL show its distance from the entry in price, percent and ATR multiples, and the target SHALL show its reward/risk ratio. A stop on the profit side or a target on the loss side SHALL be flagged and block submission. Draft entry, stop, target and estimated liquidation SHALL be previewed on the chart before submission. The price scale SHALL follow the candles: a level within half the candle range of them widens the scale, a farther level SHALL be pinned to the pane edge with an arrow instead of squashing the candles, and price tags SHALL never overlap. An open position without a reduce-only stop or trailing stop SHALL show an "unprotected" warning. Presets SHALL be disabled while their input (e.g. ATR) is warming up. After a fill the new-order stop and target SHALL be cleared, so the next order is not sized to a previous trade's stop.

Stop and target lines SHALL be draggable with the mouse (pointer), by the line or its price tag, and marked as draggable (⇕). Dragging a draft SL?/TP? line SHALL edit the ticket field live, snapped to the tick, and Escape during the drag SHALL restore the previous value. Dropping a working reduce-only STOP or TP line SHALL move that order: a replacement for the remaining quantity is placed first, and the old order SHALL be cancelled only if the replacement was accepted, so a rejected move never leaves the position unprotected. A drop on the wrong side of the current price (a stop that would trigger, or a target that would fill, at the next open) SHALL be refused with an explanation, leaving the order unchanged. The price scale SHALL stay fixed during a drag. Trailing stops, entry orders and option orders SHALL NOT be draggable.

#### Scenario: ATR stop with a 2R target
- **WHEN** the player clicks "2×ATR" and then "2R" on a buy ticket
- **THEN** the stop SHALL be two ATRs below the estimated entry, the target twice that distance above it, and the ticket SHALL show "R:R 1:2.0"

#### Scenario: Stop cleared after a fill
- **WHEN** a bracket entry with a stop and a target fills
- **THEN** the ticket's stop and target fields SHALL be empty and the feed SHALL list the attached legs as STOP and TP

#### Scenario: Drag a working stop
- **WHEN** a long position has STOP #2 and the player drags its line lower and releases
- **THEN** a new reduce-only stop at the dropped price SHALL replace #2, and #2 SHALL be cancelled

#### Scenario: Drag a stop past the price
- **WHEN** the player drops a long position's stop above the current price
- **THEN** the move SHALL be refused with an explanation and the original stop SHALL stay working

#### Scenario: Stop on the wrong side
- **WHEN** a buy ticket has a stop above the estimated entry
- **THEN** the ticket SHALL explain the problem and the submit button SHALL be disabled

### Requirement: Indicator panes
The chart SHALL offer SMA20, EMA50, session VWAP and Bollinger(20, 2) as price overlays and a lower pane selectable between RSI(14) with 30/70 guides, MACD(12,26,9) with histogram, ADX(14) with +DI/−DI and a 25 guide, or volume only. Values SHALL come from the engine (spec indicators) and appear in the hover readout formatted for the locale.

#### Scenario: Switch oscillator
- **WHEN** the player selects MACD in the lower pane
- **THEN** the pane SHALL draw the MACD line, signal and histogram for the visible bars and the readout SHALL show the hovered values

### Requirement: Candle pattern cheat sheet
The terminal SHALL offer a candle pattern reference (button and `P` key) listing at least the common single, double and triple candle patterns (hammer, inverted hammer, shooting star, hanging man, engulfing, piercing line, dark cloud cover, harami, tweezers, morning/evening star, three white soldiers, three black crows, doji, spinning top, marubozu, three inside/outside up/down, kickers, abandoned babies, rising/falling three methods) and, in a separate tab, chart patterns (double top/bottom, head and shoulders and its inverse, bull/bear flags, ascending/descending/symmetrical triangles), each with a drawing including its usual trend context, shape, what it suggests and what confirms it, filterable by bias (bullish, bearish, indecision). It SHALL state that patterns alone are weak signals. Each candle pattern drawing SHALL animate what can follow it, on a scale fixed for the whole loop: for a bullish or bearish pattern, first the textbook case (a confirmation candle closing beyond the pattern, its trigger level dashed, then follow-through) and then the failure (no confirmation, a close through the stop level); for an indecision pattern, a break up and then a break down. Each stage SHALL be captioned. With reduced motion the drawing SHALL show the textbook case as a still picture. While open it SHALL be modal: Space SHALL NOT advance the simulation behind it.

Patterns detected by the engine SHALL be marked on the chart (a bracket under bullish, over bearish patterns spanning their bars) and named for the hovered or latest bar in a row of fixed height that is always reserved, so naming a pattern never moves or resizes the chart; selecting a name or a marked bar SHALL open the sheet focused on that pattern. Markers SHALL be toggleable.

#### Scenario: Marker opens its card
- **WHEN** the latest bar completes a "three outside up" and the player selects its name
- **THEN** the cheat sheet SHALL open with that card highlighted

#### Scenario: Filter bearish patterns
- **WHEN** the player opens the sheet and selects "bearish"
- **THEN** only bearish patterns SHALL be listed, each with its drawing and confirmation hint

#### Scenario: Pattern outcome animation
- **WHEN** the sheet is open on the hammer card
- **THEN** the drawing SHALL add a confirmation candle closing above the hammer and two follow-through candles, captioned as confirmed, and then show the failure closing below the hammer's low, captioned as a failure with the stop line

### Requirement: Auto-advance
The terminal SHALL offer an auto-advance mode (button and `A`) that advances one bar per tick at a selectable speed (1× = 1.5 s, 2× = 0.8 s, 4× = 0.4 s, 10× = 0.15 s per bar; default 2×) using the same command as a manual advance. It SHALL wait without advancing while any dialog, calibration question, chart pick or pending command blocks play, and resume afterwards. With "pause on events" enabled (the default) it SHALL stop by itself on a fill, liquidation, margin call, option settlement or newly released news, and say why; it SHALL always stop when the mission ends. While it runs, Space SHALL stop it instead of advancing.

#### Scenario: Pause on fill
- **WHEN** auto-advance is running and a pending market order fills on the next bar
- **THEN** auto-advance SHALL stop after that bar and show a notice naming the reason ("Auto paused: order filled")

#### Scenario: Never skip a question
- **WHEN** a calibration question is due during auto-advance
- **THEN** no bar SHALL advance until it is answered, after which auto-advance SHALL continue

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

### Requirement: Music and sound effects
The client SHALL play background music by mood: the menu theme on the menu, briefing and debrief; the in-game theme while trading; and the tense theme while the account is in danger (an open position within 5% of its liquidation price, equity under 2× the options maintenance margin, or a margin call or liquidation in the last 3 bars). Leaving the tense theme SHALL require the danger to clear by a margin (hysteresis), so a price at the threshold does not flip the music every bar. Each mood SHALL alternate between its tracks, and mood changes SHALL crossfade. When an advance closes trades, the client SHALL play a win sting if their realized P&L nets positive and a loss sting otherwise; a liquidation SHALL always play the loss sting. Events already in a resumed run's history SHALL NOT play. Audio SHALL start only after the first user gesture (browser autoplay rules), SHALL be mutable from the top bar and with `M`, SHALL offer separate music and effects volumes, and mute and volumes SHALL persist per viewer. Audio files SHALL NOT be part of the offline precache; each file SHALL be cached on first play in a cache kept across app updates and served offline with byte-range support.

#### Scenario: Danger changes the music
- **WHEN** the mark price moves within 5% of an open position's liquidation price
- **THEN** the music SHALL crossfade to the tense theme, and SHALL return to the in-game theme only once the distance exceeds 8%

#### Scenario: Closing a trade
- **WHEN** an advance closes a position with a profit
- **THEN** the win sting SHALL play once, and replaying the run from storage SHALL NOT play it again

#### Scenario: Muted stays muted
- **WHEN** the player mutes with `M` and reloads the page
- **THEN** no audio SHALL start

