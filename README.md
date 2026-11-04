# Cyberdyne TradeLab

Educational trading game: learn **perpetual futures** (long/short, leverage, funding, liquidation) and **European options** (buy and write calls/puts) in a retro terminal, on synthetic markets that follow readable rules but never guarantee an outcome. Virtual money only — no real orders, ever.

| Where | What |
|---|---|
| `docs/product-spec.md` | Product vision and roadmap |
| `openspec/changes/` | Normative requirements (OpenSpec), one change per area — see [Specs](#specs-openspec) |
| `engine/` | Deterministic C++20 simulation core — [`engine/README.md`](engine/README.md) |
| `bindings/python/` | Python (ctypes) binding over the engine C ABI, used by the API |
| `apps/web/` | SvelteKit web client (PWA, works offline) — [`apps/web/README.md`](apps/web/README.md) |
| `services/api/` | FastAPI backend: sync, server-side replay, progress — [`services/api/README.md`](services/api/README.md) |
| `tools/` | Lint helpers (banned-API check for the engine) |

## Play locally

Requires Node 22+, CMake, Ninja and Emscripten (`brew install emscripten`).

```bash
cd apps/web
npm install
npm run engine:build   # compiles engine/ to WebAssembly
npm run dev            # http://localhost:5173
```

Add `?seed=123` to the URL to replay the same synthetic market.

| Key | Action |
|---|---|
| `Space` | Advance 1 bar (pauses auto-advance while it runs) |
| `Shift+Space` | Advance 5 bars |
| `A` | Auto-advance on/off (speeds 1×/2×/4×/10× in the top bar) |
| `M` | Sound on/off (music and effects; volumes in the menu settings) |
| `B` / `S` | Ticket side: buy / sell |
| `X` | Close position |
| `P` | Candle and chart pattern cheat sheet |
| `?` | Shortcut help |
| `Esc` | Close dialogs, cancel "mark on chart" or a line drag |
| `Enter` | Submit the order (only when the submit button has focus — advancing never shares a key with submitting) |

Shortcuts are ignored while typing in a field, while a plan or calibration dialog is open, and while marking a level on the chart.

## Features

```mermaid
mindmap
  root((TradeLab))
    Instruments
      Linear perpetual futures
      Long and short
      Isolated margin and leverage caps
      Funding and liquidation
      European options
      Buy and write calls and puts
      Black-76 pricing and Greeks
    Orders
      Market, limit, stop
      Reduce-only stop and take-profit
      Brackets and trailing stop
      Stop presets ATR, swing, percent
      Targets by R multiple
    Analysis
      SMA, EMA, VWAP, Bollinger
      RSI, MACD, ADX panes
      ATR
      29 detected candle patterns
      Pattern cheat sheet
    Learning
      Five missions
      Honest advisor and points
      Pre-trade plan
      Calibration questions
      Process vs outcome debrief
    Play
      Auto-advance
      Music and sound effects
      Offline PWA
      PT-BR and English
      Reproducible seeds
```

| Area | Details |
|---|---|
| Missions | 5 synthetic missions (table below). Each has a first-start tutorial, a briefing, objectives, a leverage cap and a "what was really going on" reveal in the debrief. Missions played to the end are marked Completed in the menu. |
| Perpetual futures | Isolated margin, leverage up to the mission cap, funding every N bars, mark-to-market, liquidation, bankruptcy (game over when you can't afford the minimum order). |
| Options | Cash-settled European calls/puts priced with Black-76 on the perpetual, with an IV model, Greeks, option chain, writing with margin and margin calls. |
| Orders | Market / limit / stop entries, reduce-only stop-loss and take-profit (brackets, OCO), trailing stop, costs (fees, spread, slippage, bar-volume capacity). The ticket warns when a new leverage would re-margin the open position (liquidation before → after), and the option ticket refuses a buy whose premium, or a write whose margin, exceeds the available balance. |
| Stop/target tools | Stop presets (1×/2× ATR, swing low/high, 1%/2%), targets at 1R/2R/3R, "mark on chart" picking, stop/target lines draggable on the chart (a working STOP/TP is moved by replace-then-cancel), live reward/risk and an unprotected-position warning. |
| Indicators | SMA20, EMA50, VWAP (24-bar sessions) and Bollinger(20,2) overlays; RSI14, MACD(12,26,9) and ADX14/±DI panes; ATR14. All computed in the engine from closed bars only. |
| Candle patterns | 29 single- and multi-candle patterns detected in context (e.g. hammer vs hanging man), marked on the chart and linked to a cheat sheet that also covers 9 chart patterns. Each candle drawing animates the textbook outcome (confirmation candle, then follow-through) and the failure (close through the stop). |
| Coaching | Pre-trade plan (side, invalidation, target, risk), calibration questions ("what's the probability that…?"), debrief that scores **process, not P&L**. |
| Advisor | An honest coach in the engine: for 1 point it reads what is visible now (regime, RSI, Bollinger, MACD, ADX, the latest candle pattern, released news, funding) and answers long, short or wait with a confidence of 50–65%, a 2×ATR stop, a size risking 1% and its reasons. "Put stop and size in the ticket" fills the ticket without sending anything. The debrief lists every read and whether it was right 5 bars later. Points are earned by disciplined trades (see [Advisor points](#advisor-points)). |
| Auto-advance | Plays bars on a timer, waits for any open dialog, pauses on fills, liquidation, margin calls, option expiry and news. |
| Sound | Music follows the moment: menu theme, in-game theme, and a tense theme when you are near liquidation or under a margin call. Closing a trade plays a win or loss sting. `M` mutes; music and effects volumes are separate. Audio (`apps/web/static/audio`, MP3 96 kbps) is cached on first play, not at install. |
| Offline | Installable PWA; every action is committed to IndexedDB before it reaches the engine, so a reload resumes exactly where you were. |

### Missions

All synthetic, generated from the run seed, starting capital 10,000 (virtual).

| ID | Title | Instrument | Lesson | What the debrief reveals |
|---|---|---|---|---|
| `m01-false-breakout` | Range | Perpetual, up to 5× | Long and short, orders and stops | A false breakout: priced-in rumor, then a credible denial |
| `m02-funding-squeeze` | Momentum | Perpetual, up to 10×, funding every 4 bars | Funding and the cost of carry | Crowded longs, pullbacks to the EMA, then a squeeze |
| `m03-volatility-shock` | Quiet market | Perpetual, up to 20× | Margin, leverage and liquidation price | Volatility compression, then a gap shock |
| `m04-options-buyer` | Calls and puts | Options (buy) + perpetual up to 3× | Payoff, premium, IV and Greeks | IV run-up and crush around a rate decision; breakout direction not guaranteed |
| `m05-options-writer` | Selling premium | Options (buy and write) + perpetual up to 3× | Writing options, margin and margin calls | Rich premium in a range, then a jump before the first expiry forces a margin call on oversized writers |

## Architecture

One deterministic engine runs everywhere. Clients play offline; the server never trusts a client's balance or score — it replays the action log through the same engine and compares digests.

```mermaid
graph TB
    subgraph CORE["engine/ - C++20, deterministic"]
        GEN["Market generator"] --> SIM["Simulation step loop"]
        SIM --> IND["Indicators and patterns"]
        SIM --> SCO["Scoring and debrief"]
        SIM --> CAPI["C ABI tl_*"]
        IND --> CAPI
        SCO --> CAPI
    end

    CAPI --> WASM["WASM build"]
    CAPI --> PY["Python ctypes binding"]

    subgraph WEB["apps/web - SvelteKit PWA"]
        WORKER["Engine Web Worker"] --> VM["View-models"]
        VM --> VIEW["Svelte views and Canvas chart"]
        VM --> IDB["IndexedDB action log"]
    end

    subgraph API["services/api - FastAPI"]
        HTTP["HTTP routes"] --> APP["GameService"]
        APP --> REP["EngineReplayer"]
        APP --> DB["PostgreSQL"]
        HTTP --> OIDC["OIDC token verifier"]
    end

    WASM --> WORKER
    PY --> REP
    OIDC --> AUTH["CyberdyneAuth"]
    VM -.->|"sync - planned"| HTTP
```

| Layer | Stack | Notes |
|---|---|---|
| Engine | C++20, CMake presets | Fixed-point int64 (`Price`, `Qty`, `Money` at 1e-8, `Rate` at 1e-10), own `det::` math kernels, PCG32 RNG, no I/O/clock/threads. Bit-identical on native x86-64/arm64, WASM and Python, locked by golden digests. |
| C ABI | `engine/include/tradelab/c_api.h` | `tl_run_create`, `tl_place_order`, `tl_advance`, `tl_state` (JSON), `tl_checkpoint`, `tl_debrief`, … — the only surface bindings use. |
| Web | SvelteKit + Svelte 5 runes, TypeScript strict, MVVM | Engine in a Web Worker; Canvas2D chart; i18n PT-BR first, English second; Vitest + Playwright. |
| API | Python 3.12, FastAPI, SQLAlchemy, hexagonal (ports/adapters) | Idempotent batch sync, replay through the engine, progress, export and erase. Access tokens from CyberdyneAuth verified via discovery + JWKS. |
| CI | GitHub Actions | `engine` (Linux/macOS native, WASM, lint, clang-tidy, OpenSpec validate), `web` (check, unit, e2e), `api` (pytest on SQLite and PostgreSQL). |

### How a bar is played

Everything the player does is a command in a log. The engine is the only thing that changes state, so replaying the log with the same mission and seed reproduces the run exactly.

```mermaid
sequenceDiagram
    participant P as Player
    participant V as View-model
    participant D as IndexedDB
    participant W as Engine worker
    participant E as WASM engine
    P->>V: Space or auto-advance tick
    V->>D: append action to the run log
    V->>W: advance request
    W->>E: tl_advance then tl_state
    E-->>W: state JSON - bars so far, positions, orders, events, indicators
    W-->>V: typed response
    V-->>P: redraw chart, panels, event feed
```

Inside one step (`engine/src/advance.cpp`) the engine processes the next bar in a fixed order:

```mermaid
graph LR
    O["Open: gap fills and gap liquidation"] --> OP["Option open phase"]
    OP --> I["Intrabar: adverse exits, entries, favorable exits"]
    I --> C["Close: funding, liquidation, option expiry"]
    C --> T["Trailing stops and metrics"]
    T --> F["Finish or bankrupt check"]
```

Intrabar order is **conservative**: when a bar touches both your stop and your target, the engine assumes the worse one happened first and flags the fill as ambiguous. The player only ever sees closed bars — indicators and patterns never look ahead (property-tested by mutating the future and comparing the past).

### Sync and server replay (backend)

```mermaid
sequenceDiagram
    participant C as Client
    participant A as API
    participant R as Engine replayer
    participant S as PostgreSQL
    C->>A: POST /api/v1/sync/batches with Bearer token and Idempotency-Key
    A->>A: verify token - issuer, audience, type access
    A->>S: load stored actions for the run
    A->>A: accept the gap-free new prefix, reject edited history
    A->>R: replay mission, seed, full log
    R-->>A: digest, status, score
    A->>S: store actions, server digest, score
    A-->>C: per run - accepted count, status, digest match
```

The web client does not call the API yet (login via a BFF is the next milestone); the API itself is implemented and tested.

## How the market data is created

Markets are synthetic and generated by the engine from `(mission, seed)`. The goal is a market that **rewards good analysis on average without ever guaranteeing it**: technical structure, fundamental news and randomness all move the price, and calibration tests measure how much edge each one gives.

```mermaid
graph TD
    SEED["Mission plus seed"] --> SEG["Scripted segments: range, trend, compression, breakout"]
    SEED --> RNG["PCG32 streams: market and events"]
    SEG --> RET["Per-bar log-return"]
    RNG --> RET
    FAIR["Hidden fair value"] --> RET
    NEWS["News events"] --> FAIR
    NEWS --> RET
    STRUCT["Structure: level or trend anchor, momentum"] --> RET
    CROWD["Crowding: extreme funding causes squeezes"] --> RET
    RET --> BAR["OHLC bar, volume, funding, IV"]
    BAR --> STRUCT
    BAR --> CROWD
```

Each bar's log-return is the sum of:

| Component | Analysis it rewards | How it works |
|---|---|---|
| Segment drift and volatility | Reading the regime | Missions script segments (range, trend, quiet compression, breakout) with their own drift and vol; volatility clusters (GARCH-like) and has jumps. The first segment's length is drawn per seed, so later segments and news land on different bars each run. |
| Hidden fair value | Fundamental | A latent value moved by segment drift, by most of each information jump, and by news surprises. Price is pulled toward it slowly (`fair_pull`), so news has lasting effect. |
| News | Fundamental | Scheduled releases (headline, expected vs actual, credibility). A surprise moves fair value on the next bar, and 80% of that move is priced at once as an opening gap. A short, uncertain transient reaction follows. News before a breakout tilts its direction. |
| Level anchor | Technical (ranges) | In range segments price reverts to the range center and meets soft walls at the edges — fading extremes works more often than not. |
| Trend anchor and momentum | Technical (trends) | Trend segments revert to their 20-bar EMA (pullbacks get bought) and carry momentum. |
| Random-direction segments | Uncertainty | Breakouts pick a direction at random, tilted by the gap to fair value: the fundamental side wins more often, not always. |
| Crowding squeezes | Positioning | When funding is extreme, a sharp move against the crowded side becomes possible (a few percent per bar). Until it comes, the crowd is usually right. Squeezes don't move fair value. |
| Funding, volume, implied vol | Derived | Funding follows recent drift plus the segment's crowding, and is published from the last closed bar only. Volume grows with the size of the move. Implied vol prices smoothed recent volatility plus a premium. |
| Noise | Randomness | Normal variates from a dedicated PRNG stream. |

Nothing in the generator reads the future. Indicators, patterns, funding and implied vol are all computed from closed bars, and an order sent at a bar's close fills from the next bar's open.

### What can the player predict?

There are two ways to measure it.

- **Calibration tests** (`engine/tests/test_generator.cpp`, 200 seeds per check) use clean, single-regime markets to check that each structural term gives a small edge and never a certain one. For example, fading RSI extremes inside a range works 58–72% of the time, buying a trend pullback 57–72%, and a structure-free random walk gives RSI no edge (35–65%).
- **The missions themselves** mix regimes, so the player first has to tell which one they are in. `tools/market_study.py` plays every mission on 100 seeds without trading and measures each signal from the next bar's open, the earliest price a player can actually get. The tables below are its output. `engine/tests/test_mission_realism.cpp` asserts the bands (60 seeds), so CI fails if a generator change makes the missions predictable again.

**Technical signals** (5 missions × 100 seeds, outcome 5 bars later; the test requires 42–58% for RSI, Bollinger, MACD and SMA20):

| Signal | Samples | Goes the predicted way | Average move in its favor |
|---|---|---|---|
| MACD cross: follow | 3,077 | 51.9% | +0.25% |
| Last bar's color, next bar only | 41,984 | 51.4% | +0.04% |
| MACD histogram: follow its sign | 42,395 | 51.3% | +0.10% |
| Candle pattern: follow its bias | 6,810 | 50.8% | +0.04% |
| Close vs SMA20: follow | 42,388 | 49.2% | -0.01% |
| ADX > 25: follow the stronger DI | 21,479 | 48.8% | -0.05% |
| Bollinger: fade a close outside the bands | 5,796 | 48.5% | -0.23% |
| RSI < 30 buy, > 70 sell | 7,049 | 48.4% | -0.17% |
| Extreme funding (> 0.06%): fade the crowd | 5,584 | 37.7% | -0.40% |

What this means:

- **No indicator is a signal on its own.** Every one of them is within a few points of a coin flip, which is also what studies of real markets find.
- **Context is the skill.** Fading RSI works inside a range and fails inside a trend. The calibration tests show the edge is there once the regime is right.
- **Extreme funding is a warning, not a timing signal.** The crowd paying high funding is usually on the right side of the trend, until the squeeze. Fading it bar by bar loses more often than it wins.

**Candle patterns.** The engine detects 29 candle patterns on closed bars. `engine/tests/test_candle_patterns.cpp` measures them over 300 generated markets (5 missions × 60 seeds), counting a pattern as working when the close 5 bars later moved its way:

| Signal | Samples | Next 5 bars go the predicted way | Test band |
|---|---|---|---|
| Any pattern on its own | 5,919 | 50.8% | 45–60% |
| Pattern plus an extreme RSI (< 30 bullish, > 70 bearish) | 583 | 56.4% | 45–62% |
| Kickers (gaps, usually caused by news) | 34 | 55.9% | 40–70% |

Patterns alone are close to a coin flip, as in real markets, and the in-game cheat sheet says so plainly. Kickers are news gaps: the information is already in the gap, so what follows is not much better than a coin flip.

**The advisor.** The advisor (`engine/src/advisor.cpp`) combines the same reads the way a textbook would: in a trend (ADX > 25) it follows the stronger DI and the MACD histogram; in a range (ADX < 20) it fades an extreme RSI or a close outside the Bollinger bands; a candle pattern from the last 2 bars votes its way, twice as strongly when it reverses the move that led into it in the regime's favor. A credible surprise released in the last 3 bars is "already priced" and pulls the read toward wait, as does ATR at a 30-bar low. Crowded funding is shown as a warning but does not vote, since the crowd is usually right until the squeeze. Long or short needs at least two net votes, and the confidence is 50% + 4 points per net vote, capped at 65%. It only ever reads the visible market (property-tested by changing every later bar, funding and headline). `tools/market_study.py` asks it every 5 bars and `test_mission_realism.cpp` holds it to 45–65% right in every mission, wait at least 20% of the time, and a mean stated confidence within 8 points of its hit rate:

| Advisor, every 5 bars (100 seeds) | Long/short reads | Right | Mean stated confidence | Wait |
|---|---|---|---|---|
| m01-false-breakout | 438 | 48.6% | 58.2% | 71% |
| m02-funding-squeeze | 718 | 52.2% | 58.3% | 66% |
| m03-volatility-shock | 474 | 47.7% | 58.5% | 72% |
| m04-options-buyer | 432 | 60.6% | 58.3% | 71% |
| m05-options-writer | 485 | 48.0% | 58.3% | 71% |
| all missions | 2,547 | 51.4% | 58.3% | 70% |

The advisor is no oracle. It says "wait" most of the time and, when it commits, it is right about as often as it claims. Following trends fails in m01's false breakout and after m03's shock, and works in m04's breakout. Like every other tool, it is worth something only with a stop and a sensible size.

**News.** Credible news is priced on arrival: 80% of a surprise's effect on fair value opens the next bar as a gap. What is left is a small, uncertain drift. The table shows the gap, then what following the surprise's direction from that open earns over 10 bars. The test requires every row to be under 72%, and a credible shock to gap by more than 3%.

| News (mission, headline, surprise) | Gap at the next open | Goes the predicted way | Average move |
|---|---|---|---|
| m01 rumor, surprise 0 | 0.00% | 54% | +0.24% |
| m01 credible denial, surprise -2 | -4.12% | 57% | +0.36% |
| m02 inflow, surprise 0 | -0.01% | 71% | +1.12% |
| m02 rate decision, surprise -1 | -2.41% | 62% | +1.24% |
| m03 exchange incident, surprise -2 | -7.83% | 41% | -1.06% |
| m04 rate decision, surprise +1 | +2.50% | 65% | +3.29% |
| m05 network outage, surprise -2 | -7.99% | 62% | +2.11% |

So the edge belongs to whoever was positioned, with a stop, before the news. A trade placed after the headline is late, as in a real market. m02's inflow row is high because it lands during the uptrend: the trend does the work, not the headline.

**Timing.** Each mission's calm opening phase lasts a different number of bars per seed: m01 70–82, m02 56–70, m03 70–90, m04 64–76 and m05 66–82. Every later segment and headline moves with it, so the story is the same but the bar it turns on is not. The option missions keep their shocks before the first expiry.

### How close is this to a real market?

| Mission | Excess kurtosis | Autocorrelation of returns | Autocorrelation of absolute returns | Biggest drop: share of seeds on its 3 most common bars |
|---|---|---|---|---|
| m01-false-breakout | 7.2 | +0.05 | +0.17 | 33% |
| m02-funding-squeeze | 3.7 | +0.07 | +0.23 | 22% |
| m03-volatility-shock | 25.0 | +0.06 | +0.34 | 23% |
| m04-options-buyer | 6.6 | +0.18 | +0.33 | 18% |
| m05-options-writer | 29.4 | +0.01 | +0.31 | 24% |

- **Fat tails.** Excess kurtosis is well above 0. Real hourly crypto data usually sits around 5–20, and the shock missions are deliberately extreme.
- **Volatility clustering.** Absolute returns are autocorrelated at 0.17–0.34, similar to real markets: calm follows calm and storms follow storms.
- **Little bar-to-bar trend.** Return autocorrelation is about 0, as in real markets, except in m04, whose scripted breakout carries momentum (+0.18).

**What changed (generator, policy v10):**

| Measure | Before | Now |
|---|---|---|
| Following a credible shock headline from the next open, 10 bars | 86–98% right, +9 to +15% | 41–62% right, -1 to +2% |
| Gap at the open after a credible shock headline | 0.2–2.8% | 4–8% |
| m03 and m05 biggest drop on the same 3 bars | 96–99% of seeds | 23–24% of seeds |
| Return autocorrelation (m01, m03, m05) | +0.17 to +0.27 | +0.01 to +0.06 |

Outcomes are still scripted so each mission teaches its lesson. m01's breakout is back in the range 25 bars after it started in about 70% of seeds, and the denial gaps it down every time. m03 liquidates a 10× long in about 90% of seeds, and m05 margin-calls a writer of 300 puts in about 40%. Randomizing outcomes, such as a breakout that is sometimes real, is planned for a later difficulty mode.

Historical packs (masked real markets from Market4Me) are specified but not built yet.

## How the game works

```mermaid
stateDiagram-v2
    [*] --> Tutorial: first start of a mission
    [*] --> Briefing: later starts
    Tutorial --> Briefing: finished or skipped
    Briefing --> Tutorial: replay
    Briefing --> Plan: start mission
    Plan --> Trading: plan recorded or skipped
    Trading --> Trading: advance bars, place orders, answer questions
    Trading --> Debrief: last bar reached
    Trading --> Bankrupt: cannot afford minimum order
    Bankrupt --> Debrief
    Debrief --> [*]
```

1. **Tutorial** — the first time you start a mission on a device, 3–5 short illustrated cards explain its concept, what to watch and which controls to use (arrows or Enter to page, Esc to skip). They never hint at what the market will do. Replay them from the briefing.
2. **Briefing** — the mission's story, objectives, instrument and leverage cap. A warm-up history is already on the chart.
3. **Plan** — before trading, write the hypothesis: side, invalidation (stop), target and risk. You can also explicitly plan to stay out — not trading is a valid decision.
4. **Trading** — advance bar by bar (or with auto-advance). Place orders, move stops, read news, watch funding, ask the advisor for a read (1 point). The game occasionally asks calibration questions ("probability price is higher in 10 bars?").
5. **Debrief** — the engine scores the run on process:

| Dimension | Weight | What it measures |
|---|---|---|
| Risk | 35 | Penalties for a liquidation or an option margin call, entries without a stop, trades entered with planned risk above 2% of capital, stops beyond the liquidation price, drawdown above 20% |
| Plan | 25 | Share of entries covered by a plan, and plans that name an invalidation level |
| Understanding | 25 | Reserved for lesson questions; not scored yet |
| Calibration | 15 | 100 × (1 − 2 × Brier score) of your probability answers, floored at 0 (needs 3+ resolved answers). Always answering 50% scores 50: no information is neutral, not a bonus |

The total is the weighted average of the dimensions that apply to the run (no trades → no plan score). Net P&L is shown but is **not** the score: a disciplined losing trade can score well, a lucky reckless one scores badly. The debrief lists every trade — perpetual and options, with option trades settled at expiry — (R multiple, exit reason, plan respected; each trade's net includes its share of fees and the funding paid while it was open, and a trade still open at the end includes its unrealized P&L, so the trades add up to the result), findings, and a reveal of what the generator was doing.

Next to the score, the debrief shows outcome facts that are **not** score inputs (all computed by the engine, scoring v6):

- Net P&L split into realized trading P&L, fees, funding, option P&L, and a separate **spread and slippage** line: what taker fills paid against their reference price (the open, or the stop's trigger) and option fills against the quote's mid. It is already inside the fill prices, not an extra charge.
- Max drawdown, peak exposure, **turnover** (traded notional) and the **closest the mark came to liquidation**.
- For options, the **P&L by Greek** (delta, gamma, theta, vega and a residual), labeled approximate: bar by bar, from the Greeks of the quote at the start of each interval; the residual holds the spread paid and higher-order effects, so the five lines add up to the option P&L before fees.
- For a perpetual trade whose stop (stop order, else the plan's invalidation) sat at or beyond its liquidation price (at the highest leverage the position carried, including a re-leverage while it was open): the **highest leverage that would have kept the stop in front** of liquidation, also named in the liquidation and stop-beyond-liquidation findings.
- When time decay (theta) or a falling implied volatility (vega) explains at least half of an option loss, a finding says so and the next lesson is Options II (IV and Greeks).

### Advisor points

Points pay for advisor reads and are earned by process, never bought. The engine reports what a run earned (`advisorPointsEarned` in the debrief, scoring v6), so the server can verify it:

| Earned for | Points |
|---|---|
| Each closed perpetual trade that had a plan (planned risk) and a protective stop working while it was open | +1 |
| ...and that trade made money (net of fees and funding) | +1 |
| Finishing the mission (not bankrupt) | +2 |
| Cap per run | 6 |

The device keeps the balance (IndexedDB `meta.progress`): it starts at 3 and is capped at 10. A run's advice allowance is the balance when it starts; when the run ends the balance becomes balance − reads used + points earned, applied once per run even after a reload. The first mission's tutorial explains this. Each read is an engine command (`RequestAdvice`) in the action log, so a replay or the server's re-simulation reproduces it; with no allowance left it is rejected and changes nothing.

## Specs (OpenSpec)

Requirements live in `openspec/changes/`, one change per area; `openspec list` shows task progress and `openspec validate --all --strict` runs in CI.

| Change | Capabilities |
|---|---|
| `add-deterministic-engine` | simulation-core, order-execution, perpetual-futures, options, indicators |
| `add-scenarios-and-gameplay` | market-generator, scenario-packs, gameplay-loop, mission-scoring, curriculum |
| `add-web-client` | web-client, terminal-ui |
| `add-cyberdyne-identity-and-backend` | player-identity, game-api, offline-sync |
| `add-native-clients` | desktop-client, ios-client |
| `add-advisor-intel-tutorials` | mission-tutorial, advisor, progression-rewards |

## Status and roadmap

Built: the deterministic engine (perps, options, indicators, patterns, generator v2, execution policy v11, scoring v6 with the full debrief), the five missions with first-start tutorials, the advisor with points earned by disciplined trades, the web client (offline PWA, draggable stop/target lines, pattern cheat sheet with outcome animations) and the API (sync, replay, progress, export, erase).

Next:

- Legal intel perks unlocked by finished missions: economic calendar and fast wire (`add-advisor-intel-tutorials`, phase 3). The engine already takes the start options (`news_lead`, `calendar`) and the advisor already reads a fast-wire headline; the state view, the briefing toggles and the server re-simulation with stored options come with that phase.
- More content toward the phase-2 target (12 missions, ≥4 regimes), campaign runs and the curriculum map.
- Web login through a BFF (needs TradeLab clients registered in CyberdyneAuth) and wiring the client to sync.
- `user_deleted` webhook, Alembic migrations, rate limits; deployment on Coolify.
- Historical packs from Market4Me, CSV import, curriculum map.
- Desktop client (C++20, Dear ImGui + SDL3), then iPad/iPhone (SwiftUI).

## Tests

```bash
# engine (native) and its WASM build
cd engine && cmake --preset native-debug && cmake --build --preset native-debug && ctest --preset native-debug

# web
cd apps/web && npm run check && npm test && npm run test:e2e

# api (needs the engine C ABI library: see services/api/README.md)
cd services/api && uv run --python 3.12 pytest -q

# specs
openspec validate --all --strict
```
