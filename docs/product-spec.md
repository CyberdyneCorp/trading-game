# Cyberdyne TradeLab — Product Specification v0.2

*2026-11-02 · supersedes v0.1 (PT-BR draft). Normative requirements live in `openspec/`; this document is the vision and the map.*

An educational trading game: short missions, initially anonymous historical markets, a retro terminal interface. The player learns to read candles, form hypotheses, size positions and manage risk in **linear perpetual futures** (long and short, with leverage) and **European options** (buying and writing calls/puts), in markets that may disagree with their analysis.

Working name: **Cyberdyne TradeLab**. Commercial name, monetization and final identity remain open.

## 0. What changed from v0.1

| Area | v0.1 | v0.2 | Why |
|---|---|---|---|
| Instruments | Spot long only; short/leverage/options out of MVP | **No spot.** Linear perpetual futures (long/short, isolated margin, funding, liquidation) + European cash-settled options (long and short), model-priced | Product goal is to teach derivatives; spot adds a third accounting model without teaching value |
| Shared engine | Rust → WASM + Swift/Kotlin bindings | **C++20** → WASM (Emscripten), Swift C++ interop, a C ABI + Python ctypes binding (server), JNI (later) | Desktop client is C++20 anyway: one systems language, the desktop links the engine directly |
| Clients | Web → Android → iOS | **Web (SvelteKit) → Desktop (C++20, Dear ImGui + SDL3) → iPad/iPhone (SwiftUI) → Android (later)** | User priority |
| Backend | Unspecified stack | Python + FastAPI (hexagonal), PostgreSQL, object storage, on Coolify; replays runs with the same engine via the python binding | Same engine on the server = authoritative re-simulation |
| Auth | "Inspect CyberdyneAuth first" | Concrete: OIDC auth-code + PKCE (S256 mandatory), web via BFF with HttpOnly cookies, native apps as public clients, client roles + entitlements from token claims | Contract inspected (see `openspec/changes/add-cyberdyne-identity-and-backend/design.md`) |
| Determinism | "Same digest on all platforms" | Made explicit: int64 fixed-point, own PRNG, own `exp/log/normcdf` kernels, `-ffp-contract=off`, no `-ffast-math`, golden vectors run on every binding in CI | Options pricing needs transcendental math, which `libm` does not make bit-identical across WASM/ARM/x86 |
| Language | PT-BR | Specs/code in English; game UI PT-BR first with i18n, en-US second | — |
| Losing everything | Not defined | **Game over**: liquidation forfeits the position's isolated margin; when the player can no longer afford the minimum order the run ends `bankrupt`. In Campaign, capital carries over between missions and bankruptcy ends the campaign run. Lessons and unlocks are never lost | Product decision 2026-11-02 |
| Option settlement | — | Venue-realistic: TWAP over a 30-min window before expiry (Deribit-style); fallback flagged `settlementApproximated` | Product decision 2026-11-02 |
| Liquidation reference | Last price | Mark price when the scenario has mark bars (venue-realistic), else last price flagged | Market4Me provides mark/index history |
| Market data | Binance/Kraken direct | **Market4Me** bulk exports (point-in-time, `available_at`, revisions, checksummed Parquet), via CyberdyneAuth service client | Internal platform already ingests the data |
| Hosting | — | Coolify: `tradelab[-dev].coolify.cyberdynecorp.ai` (web), `tradelab-api[-dev]…` (API); PostgreSQL; MinIO for packs | Cyberdyne convention |

## 1. Vision and principles

**Promise:** learn to decide under uncertainty — not memorize signals that supposedly guarantee profit.

The game must be fun even when the player loses virtual money: the reward comes from understanding what happened, preserving capital, recognizing an invalidated hypothesis and improving decisions on new scenarios.

Mandatory principles:

- Money, positions, synthetic news and rewards are exclusively virtual. No connection that executes real orders.
- Technical analysis organizes hypotheses; it does not predict the future.
- A good decision can lose; a bad one can win. The debrief separates process from outcome.
- Not trading is a valid decision. No reward for turnover or for higher leverage.
- Results always include costs (fees, spread, slippage, funding, option premium decay) and simulation limits.
- Educational mechanics and simulation work without an LLM or a permanent connection.
- Retro aesthetics never compromise legibility, accessibility or numeric precision.
- **Leverage is taught as a risk amplifier, not a reward multiplier.** Leverage caps are mission parameters, rise with curriculum progress, and are never sold or granted as rewards.

Target audience: adults, beginner to intermediate. A school edition requires separate age, privacy and language review.

## 2. Creative reference and differentiation

[Insider Trading (Steam)](https://store.steampowered.com/app/3166810/Insider_Trading/) is a roguelike deckbuilder, not a trading simulator. We borrow its atmosphere, tension of sequential decisions and run-based progression — never its art, brand, text or layouts. Raven's UI demo ([video](https://www.youtube.com/watch?v=woRba5SXxKg)) is a reference for the desktop look (dense, crisp, GPU-rendered panels).

Differences:

- In replay, cards never change history.
- Cards are knowledge, questions and tools — never profit powers.
- Mission goals prioritize discipline, understanding and survival; profit is not a universal win condition.
- Events that move prices exist only in a clearly labeled synthetic mode.
- No mechanic suggests insider information or market manipulation is acceptable.

## 3. Core loop

A standard mission lasts ~10–20 minutes (to be validated in playtests).

1. **Choose mission** — topic, difficulty, length, historical or synthetic.
2. **Briefing** — virtual capital, costs, leverage cap, allowed instruments, objectives, masked market.
3. **Observe** — closed candles, volume, enabled indicators, funding rate, option chain (if enabled), released news.
4. **Plan** — hypothesis, invalidation, confidence, size, exit; for options also the scenario that makes the trade lose.
5. **Act** — place, modify or cancel orders; or wait.
6. **Advance** — one bar or a batch; orders execute per engine rules; funding accrues; options decay and may expire.
7. **React** — new information may confirm or contradict the hypothesis.
8. **Review** — debrief with decisions, fills, costs, liquidation distance, Greeks timeline, alternatives, market reveal.

Missions never force a trade to progress. Observe and wait have their own feedback.

*Example:* "Market K7" shows a possible breakout. The player opens a 3× long with a stop below the range. The breakout fails, the stop fills with slippage, the loss is within planned risk. The liquidation price was never approached. The debrief credits risk control despite the negative result.

## 4. Game modes

- **Academy** — short interactive lessons: concept, demo, decision, feedback, transfer exercise on another market.
- **Blind historical replay** — perp OHLCV (+ funding where licensed) for BTC/ETH/SOL from one venue. Ticker and dates hidden until the debrief. Option premiums are always model-priced and labeled as such.
- **Event lab** — fully synthetic scenarios or counterfactual branches of a historical segment; all prices after the branch point are simulated and labeled.
- **Campaign** — mission sequences across regimes: trend, range, false breakout, gap, low liquidity, volatility shock, funding squeeze, IV crush.
- **Sandbox** — free replay, explicit settings, strategy comparison. Rewind creates a new attempt marked *assisted practice*.
- **Comparable challenges** (later) — same initial conditions for everyone; online progressive delivery only; offline/assisted runs in separate categories.

## 5. Curriculum

| Stage | Content | Evidence of learning |
|---|---|---|
| Foundations | OHLC, body, wicks, volume, timeframe, price vs return | Read a bar; separate known from future information |
| Structure | Trend, range, support/resistance as zones | Record hypothesis and invalidation |
| Orders | Market, limit, stop-market, take-profit, trailing, bracket/OCO, reduce-only | Predict trigger, possible fill and cost |
| Futures I | Contract, notional, long vs short, P&L symmetry, fees | Compute P&L of a long and a short by hand |
| Futures II | Margin, leverage, maintenance margin, liquidation price, mark price | Choose leverage so the stop sits well before liquidation |
| Futures III | Funding rate, carry, crowded positioning | Explain who pays funding and its cost over a hold |
| Risk | Sizing by risk budget, exposure, drawdown, R multiple | Size without exceeding the proposed budget |
| Options I | Calls/puts, strike, expiry, intrinsic vs time value, payoff diagrams | Draw the payoff of a long call and a long put |
| Options II | Implied volatility, Greeks (delta, gamma, theta, vega), IV crush | Predict the sign of P&L for a move + IV change |
| Options III | Writing options, margin on short options, covered/defined-risk spreads | Explain why short options have unbounded or large risk |
| Patterns & indicators | Doji, hammer, engulfing, inside bar, breakouts; SMA, EMA, RSI, ATR (MVP), MACD, Bollinger (later) | Explain the calculation, lag and limits |
| Events | Expectation vs surprise, liquidity, uncertain reactions | Adjust hypothesis without "good news = up" |
| Evaluation | Expectancy, costs, sample size, regimes, overfitting | Compare many attempts; never conclude from one trade |

Events comes before Evaluation: the capstone mission assesses Evaluation (design D8.4 in `add-scenarios-and-gameplay`). Lessons include failure examples and high-cost markets. Evaluation windows never overlap tutorial windows (including indicator warm-up).

## 6. Gamification without distorting learning

- **Knowledge cards** explain an indicator, compare hypotheses, recall a risk rule or open an exercise. In guided mode they may reveal an explanation, never a future price.
- **Progression** unlocks chapters, cosmetic skins, reports, regimes, instruments (shorting → options buying → options writing) and higher leverage caps — only through demonstrated understanding, never purchase.
- **Mission score:** risk 35%, plan coherence 25%, understanding 25%, probabilistic calibration 15% (Brier score on resolved binary questions). Configurable weights; non-applicable dimensions are renormalized.
- Net P&L, drawdown, costs, funding paid, turnover and peak leverage shown separately and never dominate the grade.
- Liquidation is a mission event with a dedicated explanation, not a "game over" that destroys permanent progress.
- No paid loot boxes, betting, convertible balance, rewards for trade frequency or "win it back" pressure.

## 7. Visual direction and UX

Original retro financial terminal: charcoal background, phosphor green accent, amber for attention, cyan for information. Red/green are never the only gain/loss signal (signs, labels, patterns, icons). Monospace font for prices and tables; pixel font only for titles. Scanlines, bloom and noise are subtle and can be disabled. No flicker; respect reduced motion.

Tokens: background `#0B1010`, panel `#121C1B`, accent `#39FF88`, text `#E3F5EA`, attention `#FFC857`, info `#4FD8FF`. Contrast is measured, not assumed.

**Desktop / web / iPad landscape layout:** top bar (market alias, mode, timeframe, turn, offline status, time controls); dominant chart (candles, volume, order and liquidation lines); right rail (equity, available/used margin, position, leverage, liquidation price and distance, funding, planned risk, objective); below chart (order ticket, option chain, fills, plan, journal); bottom strip (knowledge cards, news feed).

"Accepted" ≠ "filled": order acknowledgement and execution are separate messages. Time stops at decision points; nothing executes until the player advances. Submit order, close position and advance market are visually distinct.

**Phone:** native tabs — Market, Trade, Portfolio, News, Learn. Ticket as bottom sheet. Never a shrunken desktop.

**Accessibility:** 44 pt (iOS) / 48 dp (Android) touch targets; full keyboard navigation on web and desktop; chart with accessible OHLC table, text description and per-candle focus. Screen readers never receive future bars.

Required states: loading, no data, no warm-up, order rejected (with reason), partial fill, insufficient capacity, insufficient margin, liquidation warning, liquidated, option expired, download interrupted, incompatible pack, session expired, sync pending, device conflict.

## 8. Historical data and masking

Official historical data comes from **Market4Me** (BTC/ETH/SOL USD-M perps from 2024-10-17: 1m→1w bars, funding, mark/index, open interest; Deribit DVOL for BTC/ETH). Packs are built at authoring time from pinned (`as_of`) bulk exports; every value carries its availability time and is shown only once available. Windows overlapping source gaps (e.g. the 2026-10-21 → 2026-11-02 ingest outage) are rejected. Players can also import private CSV (UTC timestamp, OHLCV + optional funding rate) through a mapping and validation wizard. Masking: rebase first open to 100, multiply all prices by the same positive factor; "credits" and virtual units; relative time; random stable alias per attempt. Never normalize with future min/max. Masking is a teaching device, not cryptographic anonymization. Known scenarios (imported or already played) are labeled.

**Licensing is a launch gate.** Binance public data terms restrict commercial use; Kraken files are a format reference, not a license. Market4Me has used Binance/Deribit data internally only; commercial use and offline redistribution are not cleared. Until legal review clears derived (rebased, masked) packs, published packs are synthetic and historical packs are dev/staging-only. Historical option chains are not required: premiums are always model-priced (IV from DVOL where available, else realized volatility).

## 9. Execution, futures and options (summary)

Normative detail: `openspec/changes/add-deterministic-engine/specs/`.

- Bar-close clock: decide at bar *t*, eligible from *t+1*. No retroactive fills.
- Orders: market, limit, stop-market, take-profit limit, trailing stop-market, bracket/OCO, reduce-only. Conservative OHLC intrabar policy with `ambiguousIntrabar` flags.
- **Perpetual futures:** linear, quote-margined, isolated margin per position, one-way mode (one net position per market), leverage capped per mission, maintenance margin tiers, liquidation price shown at all times, liquidation evaluated against the adverse bar extreme, funding every N bars from scenario data or a declared synthetic rule.
- **Mark price:** unrealized P&L, margin and liquidation use mark price when the scenario has it; fills use last price.
- **Game over:** liquidation forfeits isolated margin; a run that can no longer afford the minimum order ends `bankrupt`.
- **Options:** European, cash-settled (30-min TWAP settlement) on the perp price, strikes/expiries defined by the scenario, premiums from Black-76 with a scenario IV model (base level, term, skew, event shocks) that only uses past data; bid/ask spread around model mid; Greeks displayed; buyers' risk = premium; writers post margin and can be liquidated at account level.
- Costs: maker/taker fees, spread, slippage, tick and lot size, shared per-step capacity.

## 10. Sizing

Ticket accepts quantity, notional, % of equity or risk budget. Risk sizing uses entry-to-stop distance plus conservative cost estimate, rounds down to lot size and checks margin. The UI shows **planned risk**, never "maximum guaranteed loss", plus the liquidation price for the chosen leverage, and refuses to submit if the stop is beyond the liquidation price unless the player confirms the explicit warning.

## 11. Events and synthetic generation

Event types: rate announcement, fictional regulatory change, fictional exchange incident, network outage, liquidity in/out, fictional institutional adoption, denied rumor, funding squeeze, volatility event (IV crush/spike). Each event has release time, credibility, prior expectation, surprise, impact channels (drift, volatility, jumps, volume, spread, capacity, funding, IV), intensity and duration. Reactions depend on regime and surprise, include no reaction and reversal, are seeded and versioned, and never depend on the player's position. LLMs may help author narratives offline; they never decide fills, invent historical facts or run at play time.

The synthetic generator combines technical structure (range and trend anchors, momentum), a hidden fundamental fair value moved by news, crowding squeezes and seeded noise, calibrated so analysis gives a probabilistic edge, never a certain one. Design: `openspec/changes/add-scenarios-and-gameplay/design.md` (D3a); summary in the root `README.md`.

## 12. Debrief

Market identity and period (when allowed), historical/synthetic label; timeline of hypotheses, news, orders, fills, funding payments, margin changes; costs, net return, max drawdown, peak leverage, minimum liquidation distance, turnover; requested vs trigger vs executed price; ambiguity flags; comparison to staying flat and to a 1× buy-and-hold perp; for options, P&L attribution by delta / gamma / theta / vega (approximate, labeled); what was good management, what was luck, what needs repetition; recommended next lesson. No recommendation to buy any real asset.

## 13. Architecture

```
clients (web · desktop · iOS · Android later)
   └─ engine adapter (worker / direct link / Swift interop / JNI)
        └─ C++20 deterministic core  ◄── same core in services/api via the C ABI (Python ctypes)
clients ─► game API (FastAPI) ─► PostgreSQL · object storage
   └──────► CyberdyneAuth (OIDC)
```

- The engine API knows nothing about auth, network, UI or wall clock: validated scenario + commands in, state + events out.
- No client ever sends orders to an exchange.
- Detail: each change's `design.md`.

## 14. Data contracts (summary)

`ScenarioManifest`, `ScenarioData`, `Run`, `Command`, `ExecutionEvent` as in v0.1, extended with `instrumentSpecs` (perp + option series), `marginModel`, `fundingModel`, `ivModel`. Money as scaled int64 serialized as decimal strings. Normative: `openspec/changes/add-scenarios-and-gameplay/specs/scenario-packs/spec.md` and `.../add-cyberdyne-identity-and-backend/specs/game-api/spec.md`.

## 15–16. Identity, offline and sync

See `openspec/changes/add-cyberdyne-identity-and-backend/`. Summary: CyberdyneAuth OIDC; web through a BFF holding tokens server-side; native apps as public PKCE clients with Keychain/Keystore storage; game roles (`player`, `editor`, `admin`) as CyberdyneAuth client roles; optional entitlements for paid content (never result advantages). Offline play on desktop, iPad/iPhone and the installed web PWA after pack download; idempotent batch sync; server re-simulates every synced run.

## 17. Roadmap

| Phase | Scope | Exit criteria |
|---|---|---|
| 0 Contracts & spikes | Engine skeleton, determinism harness (native + WASM + python), golden vectors, pack format, CyberdyneAuth client registration in staging, chart library pick | Same digest for a 500-bar perp+options fixture on 3 targets |
| 1 Web vertical slice | One synthetic mission, perp long/short with market/stop/TP, liquidation, funding, debrief, local save, staging login | One mission end-to-end, reproducible, no lookahead |
| 2 Web MVP | 12 missions, ≥4 regimes, 3 event families, options buy+write, limit/trailing/OCO, indicators, journal, progression, catalog, sync, installable offline PWA | Execution, usability, content and security tests green |
| 3 Desktop | C++20 + Dear ImGui + SDL3 client, offline packs, sync | Same fixtures/digests as web; full mission offline |
| 4 iPad/iPhone | SwiftUI, Swift C++ interop, offline packs, sync | Same digests; airplane-mode mission |
| 5 Expansion | Stop-limit, multi-timeframe, editorial admin, online comparable challenges, Android | Per change |

Progress (2026-11-04): phase 0 done; phase 1 done except staging login; from phase 2, eight missions (6 perp, 2 options; batch A of the phase-2 content added m06 thin book, m07 size and exit, m08 regime switch, with a Markov regime generator and a seeded debrief reveal) with first-start tutorials, a campaign that opens with m06, options buy+write, trailing/brackets, indicators (incl. MACD, Bollinger, VWAP, ADX), candle-pattern detection, installable offline PWA and the sync API are built; advisor, points and intel perks are built (`add-advisor-intel-tutorials`); missions m09–m12 are designed (`add-scenarios-and-gameplay` D8, batch B) — see the root `README.md`.

**Out of MVP:** real money, broker connections, cross margin, hedge mode, portfolio margin, American options, multi-asset portfolios, social trading, global financial ranking, public authoring without moderation, any return promise.

## 18. Acceptance tests (cross-cutting)

SIM01–SIM08, DATA01–02, EDU01–02, OFF01–02, SYNC01, AUTH01, UX01, LIC01 from v0.1 remain, plus:

| ID | Test | Expected |
|---|---|---|
| FUT01 | Long and short of equal size on a symmetric path | Mirror P&L before costs |
| FUT02 | Bar low crosses a long's liquidation price | Liquidated at the liquidation price (or worse on gap), isolated margin lost, never more |
| FUT03 | Funding interval with positive rate | Longs pay, shorts receive, exact amount = notional × rate |
| FUT04 | Stop beyond liquidation price | Ticket warns; if submitted, liquidation fires first and is explained |
| OPT01 | Price option with same inputs on all targets | Bit-identical premium and Greeks |
| OPT02 | Expiry ITM / OTM | Cash settlement = intrinsic value; OTM expires worthless; no exercise decision required |
| OPT03 | Short call, adverse move | Margin call → account liquidation per policy; never negative balance shown as real debt |
| OPT04 | IV model at step t | Uses only data ≤ t |

Non-functional targets (to measure on reference hardware): p95 interaction < 100 ms, chart ~60 fps, scene ready < 3 s after pack read.

## 19. Metrics

Primary: improvement in decision quality on unseen scenarios (pre/post assessment). Secondary: lesson completion, order-interpretation accuracy, risk-budget adherence, liquidation rate trend (should fall with progress), sync friction. Never optimize for trade count or compulsive time. Telemetry: `lesson_started`, `decision_recorded`, `order_rejected_reason`, `liquidation_occurred`, `run_completed`, `debrief_viewed`, `pack_validation_failed`, `sync_conflict`. No real-portfolio data.

## 20. Open decisions

1. Final name and identity.
2. Legal review of commercial/offline rights for Binance/Deribit-derived packs sourced from Market4Me.
3. Age rating, languages after PT-BR/en-US, business model.
4. Chart rendering approach on web (custom canvas/WebGL vs library) — license, performance, accessibility.
5. Rubric weights, default costs, maintenance-margin tiers, options margin formula — domain review. (Decided 2026-11-02: perps use isolated margin per position, short options use account-level margin; bankruptcy ends the whole campaign run.)
6. Minimum devices, team and per-phase estimates.
7. Whether paid content exists (via CyberdyneAuth entitlements); never sell result advantages.
8. Review the two v0.1 reference videos before motion design.

This experience is not financial advice, strategy validation for real markets or a promise of profit. Certificates or grades do not attest professional investing ability.

## 21. References

- Insider Trading — https://store.steampowered.com/app/3166810/Insider_Trading/
- Raven GUI (desktop look) — https://www.youtube.com/watch?v=woRba5SXxKg
- v0.1 videos — https://www.youtube.com/watch?v=4EnGV6ffGTU · https://www.youtube.com/watch?v=ZqJZPI_fXww
- Binance public data — https://github.com/binance/binance-public-data (see TERMS_AND_CONDITIONS.md)
- Kraken OHLCVT — https://support.kraken.com/ca/articles/360047124832
- SEC bulletin on stop / stop-limit — https://www.investor.gov/introduction-investing/general-resources/news-alerts/alerts-bulletins/investor-bulletins-15
- CyberdyneAuth — https://github.com/CyberdyneCorp/CyberdyneAuth · OpenAPI https://auth.backend.coolify.cyberdynecorp.ai/openapi.json
