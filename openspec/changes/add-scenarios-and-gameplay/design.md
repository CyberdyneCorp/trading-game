# Design: add-scenarios-and-gameplay

## Context

Scenarios must travel to browsers, desktops and iPads, work offline, be verifiable, and never leak the future through UI or accessibility. Scoring and debrief numbers must match between client and server.

## Decisions

### D1. Pack container
- `.tlpack` = uncompressed tar of: `manifest.json` (canonical JSON, RFC 8785), `manifest.sig` (Ed25519 over the canonical manifest bytes), and data files: `bars.bin` (columnar little-endian int64, zstd), `funding.bin`, `events.bin`, `options.bin`, `mission.json`, `reveal.json`, `i18n/*.json` (as built: `mission.json` and `i18n.json` stay reserved; mission texts live in the client catalogs).
- Hashes in the manifest are SHA-256 of each file's stored bytes; the signature therefore covers all data.
- Publisher public keys pinned in each client build with a rotation list (`signingKeyId`). Private imports are signed by a per-device key and flagged `private`.
- ~~Web verifies with WebCrypto Ed25519; native with libsodium/CryptoKit; server with `cryptography`.~~ As built: verification (and signing of private imports) runs in the engine, so every client and the server share one verifier through the C ABI. `cryptography` remains an independent check in the Python tests.

### D1a. Pack implementation choices (as built)
- **Ed25519: Monocypher 4.0.2**, vendored unmodified (`engine/third_party/monocypher`, its RFC 8032 SHA-512 variant in `optional/monocypher-ed25519.c`). Chosen over TweetNaCl (slower, unaudited, `randombytes` hook) and libsodium (too large to vendor, needs its own build): two C99 files, CC0/BSD, audited by Cure53 in 2020, deterministic signing, no allocation, builds unchanged for native and Emscripten. Tested with RFC 8032 vectors.
- **zstd: the official 1.5.7 decoder**, vendored as upstream's decoder-only single-file input (`zstddeclib-in.c`) plus the decoder sources it includes (no compressor, no legacy formats, no assembly). Chosen over the "educational" decoder (not meant for hostile input) and third-party ports: it is what OSS-Fuzz fuzzes continuously. Decompression goes into a buffer of exactly the declared size, so a bomb stops at `dstSize_tooSmall` instead of allocating.
- **Size:** the whole engine WASM module, with both libraries, is about 157 KB gzipped (budget 400 KB).
- **Canonicalization:** RFC 8785 over a strict JSON subset (integers within 2^53, no fractions or exponents, valid UTF-8, no duplicate keys). Under that subset JCS number formatting is plain digits, so no floating point is involved. Money and rates are decimal strings anyway.
- **Container:** ustar with flat names (`[a-z0-9._-]`, no prefix, regular files only), deterministic headers (zero mtime/uid/gid). `manifest.json` is stored in canonical form; `manifest.sig` holds the signature (the manifest's "signature" field of the spec lives in that file).
- **Data:** every `.bin` is a zstd frame of one or more columnar little-endian int64 tables, each with a 16-byte header (magic, rows, columns, 0): `bars.bin` (`TLB1`), `funding.bin` (`TLF1`), `events.bin` (news `TLE1`, then liquidity shocks `TLQ1`), `options.bin` (series `TLO1`, then the IV base `TLI1`, one row per bar with `ivModel` `file`). Row counts come from the manifest (`data.bars`, `events`, `liquidityShocks`, `optionSeries`) and are checked against limits before decompression; every column is range-checked before narrowing. Private imports store raw zstd blocks (still valid zstd) so clients need no encoder; `tools/packer build` compresses published packs (zstd level 19 through the `zstandard` package, content size in each frame). `mission.json` and `i18n.json` stay reserved.
- **Branch packs:** mode `branch` names `data.branchStep`; every later bar is flagged synthetic when the pack opens (spec "Branch labeling"). The packer builds one from a masked window and a built-in mission's market for a seed, rescaled to continue from the recorded close (`splice_branch`).
- **Limits:** pack 16 MiB, 8 files, manifest 64 KiB, 8 MiB per decompressed file, 20,000 bars; CSV 4 MiB and 20,000 rows. Parsers are fuzzed with libFuzzer (`engine/fuzz`).
- **Private CSV packs:** practice rules (10,000 capital, chosen leverage cap 1–20, warm-up of a quarter of the rows clamped to 20–60, perpetual only, default costs), funding every 8 hours of bars, `license.name = private-import`, never publishable (`private_pack`).

### D2. Scoring and debrief live in the engine repo
- Scoring (C++20, `engine/src/scoring.cpp` + `engine/include/tradelab/scoring.hpp`, part of the engine library, versioned by `kScoringVersion` = 9; v6 adds advisor points and advice outcomes, v7 plan matching and the written-off line, v8 the full-revaluation Greek attribution, side-aware IV findings and the game-over benchmark, v9 the signals-by-regime table and each trade's entry regime from the seeded reveal) computes score dimensions, findings, per-trade rows (R multiple, plan respected) and the next-lesson code from the run state and command log; the C ABI debrief adds P&L aggregates and buy-and-hold/flat comparisons.
- Report-only debrief facts (`engine/src/debrief.cpp`, scoring v4, fixed in v5) are recomputed from the event log and the scenario, never stored in the run state, so they change no digest: spread/slippage cost (each taker fill against its reference price, each option fill against the mid of its quote), option P&L by Greek (bar by bar, Greeks of the start quote of each interval, residual = the rest of the option P&L) and, per perpetual trade, the stop, the highest leverage the position carried (at entry, or a higher re-leverage while open), the liquidation price at that leverage and the highest leverage that would have kept the stop in front (found by re-running the engine's liquidation formula from the trade's leverage down to 1×). Theta or vega explaining at least half of an option loss is a finding that maps to the Options lesson. Scoring v5 also adds an open trade's unrealized P&L (valued like the account equity) to its net, withholds good-process credit when an option was written, and credits a disciplined win (`good_process_win`). Option quotes come from the same deterministic pricing kernel; everything else is integer arithmetic. Same golden approach as the engine. Clients render codes as text; the server (`services/api`) recomputes by replaying the action log through the Python binding.
- Mission rules/rubrics are data in the pack, not code. Until packs exist, the built-in missions live in `engine/src/missions.cpp` (generator spec, rules, option chain) with their texts in the client catalogs (`apps/web/src/lib/game/missions.ts`).

### D3. Generator
- `engine/src/generator.cpp` (`engine/include/tradelab/generator.hpp`): regime segments (scripted per mission, or drawn by a Markov chain since generator v3, D8.1 G1) + GARCH(1,1)-like variance (`v ← 0.85·v + 0.15·z²`) + Bernoulli-per-bar jumps; volume log-normal tied to |return| and segment volume multiplier; funding = clamp(baseline + k·EMA(return) + segment bias, ±0.3%) with no noise term; IV base = annualized segment vol·√variance·(1 + premium)·segment/event IV multipliers.
- Target: runs offline at authoring time to bake bars into packs (fast load, inspectable), and at play time only for seeded sandbox variants. Both paths use the same deterministic code. Today, with no packs, the built-in missions are generated at play time from the run seed.

### D3a. Generator v2: readable market structure
Goal: technical and fundamental reads earn a small, probabilistic edge (about 55–70% in their setting), never a certainty, while a structure-free random walk gives none. Each bar's log-return = segment drift × direction + transient news reaction + structure + σ·z + jumps + crowding squeeze, where structure is:
- **Fair value** (hidden log level): moves with segment drift (fundamental flow), with 75% of information jumps, and by `fair_impact × surprise × credibility` on the bar after a news release. Expected news (surprise 0) leaves it unchanged ("priced in"). Price is pulled toward it by `fair_pull` per bar.
- **Anchors**: `Level` (log price at segment start) with reversion and soft walls beyond `band` → support/resistance and false breakouts; `Trend` (EMA20 of log price) with reversion → pullbacks to the average.
- **Momentum** (AR(1) on the previous return), **volatility compression** (`vol_decay` per bar, also lowers IV → Bollinger/ATR squeeze), and **random-direction segments** whose sign is drawn per seed with P(up) = 1/(1 + e^(−tilt·(fair − price))), so breakouts lean on fundamentals but can fail.
- **Crowding**: when |funding| exceeds `squeeze_funding`, a squeeze jump against the paying side happens with `squeeze_prob`. Squeezes are positioning, not information: they do not move the fair value and tend to retrace.
- Calibration is enforced by engine tests over 200 seeds with explicit bands (random walk ≈ 50%; range RSI fades, trend pullback buys, news-tilted breakouts and post-surprise drift inside their bands; shorting overbought in an uptrend stays below 57%).
- Every change to the generator changes every seed's market, so it bumps the execution-policy version (v5) and the goldens; unfinished runs from older versions are flagged by the existing policy-mismatch path.

### D3b. Realism: news priced on arrival, per-seed schedule
`tools/market_study.py` (5 missions × 100 seeds, signals entered at the next open) showed indicators near a coin flip, as in real markets, but two teaching problems:
- Following a credible shock headline won 86–98% of the time, gaining 9–15% in 10 bars, because news only moved the hidden fair value, which price approached at 0.6% of the gap per bar, and because scripted crash segments carried several bars of deterministic drift.
- The m03 and m05 shocks fell on the same three bars in 96–99% of seeds, so replays taught memory, not reading.

Decisions:
- `news_gap` = 0.8: on the bar after a release, 80% of the fair-value shift opens the bar as a gap. The m03 and m05 crash segments lose their drift (high volatility and two-sided jumps remain), and the events carry the shock (`fair_impact` 0.05 and 0.06, about −8% gaps). m01's failure segment splits into a 3-bar slip before the denial and a driftless aftermath; m02's unwind drift halves.
- Schedule: a lead drawn per seed on `Stream::Schedule` lengthens the first segment and shortens the last. Events reacting after the first segment move with it. The ranges keep each lesson's constraints: m05's shock always comes before the first expiry (bar 108), and m04's breakout before expiry 1.
- Outcomes stay scripted (m01 still fails, m03 and m05 still crash) so each mission still teaches its lesson. Randomizing outcomes is deferred to a later transfer or difficulty mode.
- Result (100 seeds): following news from the next open is right 41–65% of the time, the shock bars spread out (no three bars hold more than 33% of seeds), and return autocorrelation falls to about 0 except m04's breakout (0.18). Lessons hold: m01 is back in the range 25 bars after the breakout started in 72% of seeds, and the denial gaps every time; m03 liquidates a 10× long in 88% of seeds; m05 margin-calls a 300-put writer in 40%.
- `engine/tests/test_mission_realism.cpp` asserts the bands (spec "Mission realism"). Policy v10.

### D4. Masking
- Factor `100 / firstPlayableOpen` computed from the first playable bar only. Strike grids generated after masking. Rounding declared per pack (half-even to tick).
- As built (`engine/src/masking.cpp`): the reference is the open of the bar at the first decision point (bar `warmupBars`), the earliest open the player sees, so no later data enters the factor. Each price is `round_half_even(p * 100 / ref / tick) * tick` in 128-bit integers (one rounding); volume is multiplied by `ref / 100` so notional volume and capacity are unchanged. The factor is not stored in the manifest (it would reveal the price level); `reveal.json` describes the transformation.

### D5. Packer CLI
- `tools/packer` (Python, using the nanobind engine): `validate`, `import-csv`, `mask`, `generate`, `build`, `sign`, `inspect`. Validation logic shared with the client import wizard through the engine's C ABI so web/desktop/iPad import produce identical errors.
- As built: `tools/packer/packer.py` uses the existing ctypes binding (not nanobind) and offers `key`, `import-csv`, `validate`, `generate`, `mask` (also branch sources), `build`, `sign` and `inspect`. A pack source is a directory with the manifest and the uncompressed data files; the engine writes sources (`tl_pack_source_mission`, `tl_pack_source_csv`), signs a manifest over stored files (`tl_pack_write`) and re-signs (`tl_pack_resign`); the packer only compresses (the engine stays decoder-only). `build --publish` also requires the generator's calibration report for generated bars. `m4m-pull` waits for 1b.x.

### D5a. Historical source: Market4Me
- Source of truth for historical data is Market4Me (`github.com/CyberdyneCorp/Market4Me`), via bulk exports only: `POST /api/v1/exports` (`series` = bars | funding | open_interest | market_state, `instrument` e.g. `BTC-USDT:PERP:binance-usdm`, `tf`, `from`, `to` ≤ 1,100 days, pinned `as_of`) → poll `GET /exports/{id}` → `GET /exports/{id}/file` (Parquet + manifest with checksums). No direct DB or NATS reads.
- Auth: CyberdyneAuth client-credentials as service client `cyb_tradelab_packer`, audience `market4me-api`, allowlisted on Market4Me with role `analyst`. Used only by `tools/packer` (authoring time), never by game clients or the game API.
- Available: BTC/ETH/SOL USD-M perp bars (1m→1w) from 2024-10-17; funding + mark + index; open interest; Deribit DVOL hourly (BTC/ETH; SOL = UNAVAILABLE → realized-vol IV). Liquidations are live-only (no history) → not used.
- Known gap: 2026-10-21 → 2026-11-02 live-ingest outage (backfill pending); the gap check rejects windows overlapping it.
- Point-in-time: `available_at` exported per record; bars have `revision` (corrections are new revisions). Packer pins `as_of` and uses `m4m dataset verify` / `reproduce` style checksums for provenance.
- Window selection for diverse regimes: 4h SMA50/200 position × realized-vol bucket computed from the bars (robust); Market4Me `market_state` (UP/DOWN/SIDEWAYS/REVERSAL/UNCERTAIN) as a secondary label once its staging history rebuild (pre-2026-10-04) is confirmed.
- Options chapters: prefer BTC/ETH windows with 1m sub-bars around expiries (exact TWAP settlement) and DVOL coverage.
- **Licensing:** Market4Me has used Binance/Deribit data internally only; commercial use and offline redistribution are NOT cleared. Packs ship derived data (rebased prices, masked symbols, relative time), but derived data from vendor feeds still needs legal review. Until cleared, published packs are synthetic; historical packs are dev/staging-only (`license_incomplete` blocks publish).

### D6. Leak prevention
- Clients receive data through the engine adapter, which exposes only bars ≤ current step. Reveal and future bars are never copied into UI state, logs, analytics or accessibility trees. A lint/test checks the web worker message schema has no field carrying future data.

### D7. Campaign runs and the curriculum (as built)
- **Capital through the engine.** Policy v12 adds two start options: `starting_balance` (replaces the mission's initial balance) and `leverage_cap` (lowers the mission's cap). They are stored in the run state, the state digest and checkpoints (format 3), so a campaign mission replays like any run; event digests of default runs are unchanged. The score's net P&L and its 2%/20% limits and the buy-and-hold benchmark use the run's own initial balance.
- **Campaign state on the device.** `meta.progress` version 2 holds the campaign run (id, capital for the next mission, mission index, status, one leg per settled run), ended campaign runs and the missions assessed in a campaign. A campaign mission is settled once per run id from the debrief's `finalEquity`; a run left behind is settled from the last equity stored in its run header. Server-verified campaign progress comes with sync (offline-sync "union of achievements").
- **Curriculum as data.** `apps/web/src/lib/game/curriculum.ts` lists the 13 stages with prerequisites, assessment missions and unlocks; the campaign order is the stages' missions in order (m01, m03, m02, m04, m05 at first; m06, m01, m03, m02, m07, m04, m05, m08 since batch A, D8.4). Of the four unlocks only the leverage cap needs an engine option with today's content: shorting is m01's own stage, and options are only listed in their own assessment missions. A unit test fails if a campaign mission would meet a lock the engine cannot enforce, which is when per-run short and option permissions must become start options too.
- **Assessment = finishing the mission in a campaign run.** Lesson questions (understanding dimension) are not scored yet, so finishing is the assessment for now; free play keeps the Completed mark but does not pass stages.

### D8. Phase-2 missions m06–m12 (batch A built: m06–m08, G1, G4; batch B built: m09–m12, G2, G3)
Phase 2 asks for 12 missions, at least 4 market regimes, 3 event families and options chapters (task 5.3). Five missions exist. This section designs the other seven. They are built in two batches (tasks 6.A and 6.B).

**What is missing today.**
- **Curriculum.** Foundations, Structure, Orders, Risk, Patterns & Indicators, Evaluation and Events have no assessment mission.
- **Lessons.** Nothing teaches these yet:
  - limit orders against market orders, spread and capacity;
  - where to put a stop and how far to trail it;
  - trend following against mean reversion when the regime changes;
  - expectation against surprise across several releases;
  - funding carry;
  - protective puts, covered calls, collars and spreads;
  - long volatility (straddles, implied against realized);
  - a final exam.
- **Generator.** Two of the spec's regimes, trend-down and low-liquidity, never appear as declared regimes. Every outcome is scripted, so the same story plays on every seed.

**Market regimes after phase 2.** Range, trend-up, trend-down, high-volatility, low-liquidity and crowded funding.

**Event families.** Every `NewsKind` belongs to one of three families:
1. **Scheduled releases:** `RateDecision`, and the new `MacroData` (a fictional economic print) and `NetworkUpgrade`. These can be announced ahead with a consensus.
2. **Incidents:** `ExchangeIncident`, `NetworkOutage` and `RegulationRumor`. They are unannounced and create gap risk.
3. **Flows and positioning:** `LiquidityInflow`, the new `LiquidityOutflow`, `InstitutionalAdoption`, `RumorDenied` and funding crowding.

New `NewsKind` values are appended (7 `LiquidityOutflow`, 8 `MacroData`, 9 `NetworkUpgrade`). Existing values keep their numbers.

#### D8.1 Generator capabilities
The engine changes are kept small. Each capability acts only when a mission declares it, so m01–m05 keep generating identical bars, news and digests. That is why `kExecutionPolicyVersion` does not change (13 when this was written). Each new mission adds its own golden (default options), and the existing goldens are never touched. If any existing golden changes, the implementation is wrong for this design. `kGeneratorVersion` becomes 3; it is not a digest input.

- **G1 Markov regime chain** (task 3.1, batch A). `GeneratorSpec.chain` is optional:
  - `states`: `Segment` templates (their `bars` field is ignored);
  - `transitions`: per-mille rows that sum to 1000, with a zero diagonal (dwell handles staying);
  - `min_dwell`, `max_dwell`, `initial` (per-mille weights over the states), `bars`, and `at`, the index in `segments` where the expanded chain is inserted.

  The path is drawn on a new `Stream::Regime` (6) with integer draws (`next_bounded`). The last visit is cut so the chain fills exactly `bars`. The chain is expanded into ordinary segments *before* `scheduled()` applies the per-seed lead, so the rest of the generator is unchanged. The existing funding rule (the bias of the last closed bar's segment) and the random-direction tilt then apply to chain segments too.

  `Segment` gains a `label` (`Range`, `Trend`, `HighVol`, `LowLiquidity`, `Crowded`, `Calm`, `Shock`; default `Unlabeled`). The label is used for the reveal and for tests. It never feeds generation.

  Tests:
  - over 2,000 seeds, each transition's frequency is within ±3 points of its row;
  - total length is fixed;
  - every dwell is in range, except the cut last visit;
  - m01–m05 bars are byte-identical with and without the feature compiled in (their goldens);
  - two chains that agree up to bar k give identical funding and IV up to bar k.
- **G2 Drawn event outcomes and intensity** (task 3.2, batch B). `ScriptedEvent` gains:
  - `actual_weights`: per-mille weights for actual = −2..+2; all zero means the declared `item.actual`;
  - `intensity_min` and `intensity_max` (default 1, 1), a factor on `fair_impact` and on the reaction `drift`.

  Both are drawn on a new `Stream::Outcome` (7) per event index. `sample_reaction` keeps its stream, so the existing reaction tests and goldens hold. The drawn actual is written into the `NewsItem`, so the headline shows it from its release, and an expected outcome (surprise 0) still leaves the fair value unchanged.

  Tests:
  - frequencies over 2,000 seeds within ±3 points;
  - intensity stays inside its range;
  - events that declare nothing keep their bars;
  - a drawn surprise of 0 does not move the fair value.
- **G3 Announced events** (task 3.2 "lead time", batch B). `NewsItem` gains `announce_lead` (bars, default 0). From the close of `release_step − announce_lead`, the state view lists the item under `upcoming` with kind, release step and expectation (the consensus) only. Tone, actual and credibility stay hidden until the release. Bars do not depend on the lead, because the announcement is visibility only.

  This generalizes the advisor change's calendar perk (add-advisor-intel-tutorials 3.1), which keeps listing unannounced `RateDecision` steps for perk holders. The advisor gets an `EventAhead` reason: an announced release within the next 3 bars votes 0 and moves the sum one step toward wait, like `LowVolSqueeze`. The advisor's m02 golden is unaffected, since m02 announces nothing.

  Tests:
  - a property test: changing an announced event's actual, tone or credibility changes nothing visible before `release_step`;
  - the `upcoming` list is exact from `release_step − announce_lead`;
  - bars are identical with and without the lead;
  - the advisor realism bands hold in every mission.
- **G4 Seeded reveal** (batch A). `mission_reveal(id, seed)` is a pure function that regenerates the mission and returns:
  - the regime spans (start, bars, label, and direction for random-direction segments as drawn);
  - each event's drawn actual, intensity and sampled reaction (none, along or reversed).

  It is exposed as C ABI `tl_mission_reveal`, through the WASM worker and the Python binding. Clients ask for it only after the run has ended, and a worker message-schema test enforces that (leak prevention, D6).

  The debrief adds a report-only **signals by regime** table. For RSI fade, Bollinger fade, MACD follow and SMA20 follow it gives hits and samples per revealed span of this run, and it tags each trade with the regime it was entered in. This is `kScoringVersion` 8. It is no score input and changes no digest.
- **Data only, no capability.** `MissionDef` gets an optional cost and contract override, so m06 can declare a thin book (wider spread, more slippage, less capacity). It also gets per-mission `allow_short` and `allow_short_options` flags, which already exist in `MissionRules` and `OptionSpec`.

**Deferred.** These are not needed by m06–m12:
- ~~event channels for spread, capacity and funding (the rest of task 3.2). m06 uses mission-level costs and segment volume instead;~~ As built (generator v5): jump, funding, spread and capacity channels exist on `ScriptedEvent` and act only when declared. m06 keeps its mission-level costs and segment volume, because its story (D8.2) thins the book while spread and slippage stay the same; no mission declares a channel, so no golden changed and no policy bump was needed;
- spread or covered-call margin offsets. Each short leg stays margined on its own, and the long legs count toward equity at bid, which is conservative and is what an isolated-margin venue does. m10 says this in a tutorial card;
- outcome randomization for m01–m05.

#### D8.2 The seven missions
Every mission is synthetic, starts with 10,000 (campaign: carried capital), has a warm-up of 60 bars and hourly bars. Titles are neutral, as for m01–m05. The segment values are starting points, tuned at build time until the learnability bands in D8.3 pass.

| | m06-thin-book | m07-trend-trailing | m08-regime-switch |
|---|---|---|---|
| Title PT / EN | Missão 06 · Livro fino / Mission 06 · Thin book | Missão 07 · Tamanho e saída / Mission 07 · Size and exit | Missão 08 · Sinais / Mission 08 · Signals |
| Batch | A | A | A |
| Stage (assesses) | Structure + Orders | Risk sizing | Patterns & Indicators |
| Campaign slot | 1st | 5th | 10th |
| Instrument | Perpetual, **long only**, 2× | Perpetual, long/short, 5× | Perpetual, long/short, 5× |
| Costs | half-spread 0.10%, slippage 0.20%, capacity 4% of bar volume, base volume 1,500 | default | default |
| Regimes | Range, then low-liquidity range, then random-direction drift, then range | Range, then trend (direction drawn, tilted by news), then high-vol chop at the extreme, then range | Markov chain (G1) of Range, Trend (direction per visit) and Chop, dwell 25–50 bars, 180 bars |
| Event family | Flows: `LiquidityOutflow` (surprise 0), later `LiquidityInflow` (+1, credibility 60) | Flows: `InstitutionalAdoption` (+1, credibility 75) before the trend | None (the lesson is the chart) |
| Capability | Cost override only | None (existing generator) | G1, G4 |

| | m09-event-straddle | m10-hedged-book | m11-news-calendar | m12-capstone |
|---|---|---|---|---|
| Title PT / EN | Missão 09 · Véspera / Mission 09 · The day before | Missão 10 · Proteção / Mission 10 · Cover | Missão 11 · Agenda / Mission 11 · Calendar | Missão 12 · Prova final / Mission 12 · Final exam |
| Batch | B | B | B | B |
| Stage (assesses) | Options II (with m04) | Options III (with m05) | Events | Evaluation |
| Campaign slot | 7th | 9th | 11th | 12th (last) |
| Instrument | Options **buy only** + perpetual 3× | Options buy and write + perpetual 5×, funding every 4 bars | Perpetual, long/short, 5× | Perpetual 10× + options buy and write |
| Regimes | Range, then compression with IV bid (iv_mult 1.45), then random-direction move, then range with IV crush (0.8) | Crowded-long uptrend (funding bias +0.06%), then shock aftermath, then range | Markov chain (G1) of Range, Trend and Chop, dwell 30–60 bars | Markov chain (G1) of Range, Trend, Chop, Thin (volume ×0.3) and Crowded-short (funding bias −0.08%), 200 bars |
| Event family | Scheduled: `RateDecision`, announced 20 bars ahead, consensus 0, actual drawn (−2..+2: 10/20/40/20/10%), intensity 0.6–1.4 | Incidents: `RegulationRumor`, unannounced, actual drawn (−2: 45%, −1: 35%, 0: 20%), intensity 0.7–1.3, credibility 85 | Scheduled: `MacroData`, `RateDecision`, `NetworkUpgrade` (announced 15–25 bars ahead, drawn actuals around the consensus); Incidents: one unannounced `ExchangeIncident` (actual 0 in half the seeds) | All three: an announced `RateDecision`, an unannounced `NetworkOutage` (no impact in 60% of seeds), a `LiquidityOutflow` |
| Option chain | Listed at the first decision, expiries +25 and +70 bars, strikes ±3 × 2.5 (the first expiry lands about 10 bars after the release) | Expiries +30 and +75, strikes ±3 × 5 (the shock lands before or after the first expiry depending on the seed's lead, −8..+20) | none | Expiries +40, +100 and +160, strikes ±3 × 5 |
| Capability | G2, G3, G4 | G2, G4 | G1–G4 | G1–G4 |

**m06 Thin book — limit orders, liquidity and structure.**
- **Story.** A quiet market where few quotes rest in the book. A market maker reduces its quotes: the book gets shallower (each bar takes fewer contracts; spread and slippage stay the same) and the range turns jumpier. Liquidity then returns with a modest positive surprise, and the market leaves the range, more often up than down.
- **Lesson.** Support and resistance are zones. In a thin book a market order pays spread and slippage (about 0.6% per round trip, a quarter of the range). A limit at the zone pays the maker fee but may not fill. An order larger than the bar's capacity fills in pieces, and a stop-market slips. Liquidity news changes depth (capacity), not value.
- **Objectives.**
  - Enter and exit with limit orders at the zones, and compare the cost line in the debrief.
  - Keep planned risk ≤ 1% per idea.
  - An unfilled limit is a missed trade, not a loss.
- **Reveal.**
  - The range zones and the thin phase.
  - That the outflow headline carried no surprise (fair value unchanged) and the inflow tilted the exit.
  - What the player's market fills paid against their limits.
  - How often orders were capacity-limited.
- **Tutorial cards** (5). This becomes the campaign's first mission, so it carries the introduction:
  - `chart`: read the chart, known against future;
  - `book` (new art): spread, slippage and capacity;
  - `ticket`: market, limit and take-profit, and "accepted is not filled";
  - `risk`: stop first, and stops slip in a thin book;
  - `advisor`: plan, advisor and points, the m01 card that the advisor spec requires on the first mission.

  m01 keeps its own cards for free play.

**m07 Size and exit — stops, trailing stops and sizing by risk.**
- **Story.** A range. A credible adoption headline lifts the fair value. A trend starts whose direction is drawn per seed and tilted by that headline (up in about 55–75% of seeds), with pullbacks of 1–3 ATR toward its EMA. It ends in a choppy, high-volatility top or bottom, and then a range.
- **Lesson.**
  - Size from the stop (quantity = risk budget / stop distance); leverage does not change the risk.
  - A stop goes beyond structure, not at a round number.
  - A tight trailing stop is cut by ordinary pullbacks. A wider trail (2–3 ATR) gives some back but stays in the move.
  - R multiples, and win rate against payoff.
- **Objectives.**
  - Risk ≤ 1% per idea, sized from the stop.
  - Once in profit, move the stop to break-even or trail it, but never tighter than the noise (ATR).
  - Finish without a drawdown above 10%.
- **Reveal.**
  - This seed's trend direction and how the headline tilted it.
  - The pullbacks that took out 1×ATR trails.
  - The R multiple of each trade, against what a 3×ATR trail would have kept.
- **Tutorial cards** (4): `risk` (size from the stop), `trail` (new: how a trailing stop ratchets, at the next bar), `trend` (pullbacks to the average), `plan` (R multiple; targets against trailing).

**m08 Signals — trend following against mean reversion across a regime change.**
- **Story.** The market switches between quiet ranges, directional trends and choppy high volatility, on a path drawn per seed (G1). No headline warns of a switch.
- **Lesson.**
  - Fading RSI or Bollinger extremes works in a range and loses in a trend.
  - Following MACD or SMA works in a trend and bleeds in a range.
  - ADX and the band width help to tell the two apart, late: indicators lag the switch.
  - Every indicator lesson needs a failure example (curriculum "Failure examples"). This mission is that example, measured on the player's own run.
- **Objectives.**
  - Before each entry, write which regime you think you are in, and why.
  - Use one tool per regime.
  - Planned risk ≤ 1%.
- **Reveal** (G4).
  - The regime band under the chart.
  - The signals-by-regime table for this run.
  - Each trade tagged with the regime it was entered in.
- **Tutorial cards** (4): `trend` (trend against range), `regime` (new: ADX and band width as a regime filter, and their lag), `volatility` (Bollinger squeeze and expansion), `chart` (no indicator alone is a signal).

**m09 The day before — long volatility around a scheduled release.**
- **Story.** A rate decision is announced 20 bars ahead with a consensus of "unchanged" (G3). Implied volatility is bid into the release while the range compresses. The decision, drawn per seed (G2), can match the consensus (40%), surprise mildly (40%) or surprise strongly (20%). The move after it has a drawn direction and size, and IV collapses after the release.
- **Lesson.**
  - A straddle or strangle pays when the realized move exceeds the move the premium implied.
  - Buying after the release pays for the crush and misses the gap.
  - Theta works against the buyer while waiting.
  - Writing is still locked (Options III), which keeps the lesson on the buyer's side.
- **Objectives.**
  - Before buying, compute the two breakevens.
  - Write the scenario in which the trade loses (no surprise, a crush).
  - Do not pay more than 2% of equity in premium.
- **Reveal** (G4).
  - The drawn decision and its intensity.
  - The implied move (straddle price at entry) against the realized move.
  - The vega and theta lines of the Greek attribution.
- **Tutorial cards** (5): `calendar` (new: announced events, consensus, `upcoming`), `straddle` (new: payoff with two breakevens), `volatility` (implied against realized), `decay` (theta and crush), `chain` (choosing strike and expiry around the release).

**m10 Cover — protective puts, covered calls, collars, spreads and carry.**
- **Story.** A steady uptrend with crowded longs paying funding every 4 bars. An unannounced regulation rumor arrives on a bar that moves with the seed; it is sometimes after the first expiry. Its severity is drawn (G2): in 20% of seeds it changes nothing.
- **Lesson.**
  - A protective put sets a floor at a known cost.
  - A collar finances the put by capping the upside.
  - A covered call earns premium but is margined on its own here (isolated venue; the perp's gain does not free option margin).
  - Vertical spreads define the risk.
  - Holding the perp long costs funding (carry); a long call costs theta instead. Compare the two.
  - Insurance has a tenor: a short-dated put can expire the bar before the shock.
- **Objectives.**
  - Before trading, write each structure's maximum loss.
  - Keep a buffer above option maintenance margin.
  - Compare the funding you pay with the theta you pay.
- **Reveal.**
  - This seed's rumor severity and timing against the expiries.
  - The hedged against the unhedged path.
  - Funding paid against the option's theta.
- **Tutorial cards** (5): `payoff` (protective put and collar), `margin` (how short legs are margined; long legs count at bid), `funding` (carry against theta), `chain` (choosing tenor), `risk` (the maximum loss of each structure).

**m11 Calendar — expectation against surprise.**
- **Story.** Three scheduled releases, each announced 15–25 bars ahead with a consensus, land in whatever regime the drawn path (G1) is in:
  - a macro print, consensus +1;
  - a rate decision, consensus 0;
  - a network upgrade, consensus +1, which can slip (actual 0).

  Actuals are drawn around the consensus (G2). Reactions include none and reversals (`p_align` 0.5 for the rate decision: "sell the news"). One unannounced exchange incident is real in half the seeds.
- **Lesson.**
  - The consensus is already in the price, and only the surprise moves the fair value.
  - Most of a surprise is in the opening gap.
  - A stop held through a release can fill far beyond its trigger.
  - Being flat or small into a release is a decision.
  - Good news can sell off.
- **Objectives.**
  - Before each release, decide flat, small or hedged, and record why.
  - Do not trade the headline after the gap without a new reason.
  - Planned risk ≤ 1%, counting the gap.
- **Reveal.**
  - Per release: consensus, actual, surprise × credibility, gap, reaction type.
  - Whether the player was positioned before or after.
- **Tutorial cards** (5): `calendar` (the upcoming list), `news` (expectation against surprise), `risk` (gaps jump stops), `plan` (deciding before the release), `advisor` (`EventAhead`: why the advisor says wait).

**m12 Final exam — capstone (Evaluation).**
- **Story.** Everything at once on a drawn path (G1), with an announced rate decision and an unannounced outage:
  - ranges, trends and chop;
  - a thin-book phase;
  - a crowded-short range where longs collect funding (the carry trade) at the risk of the path turning into a downtrend.
- **Lesson.**
  - Identify the regime first, then choose the tool.
  - Size for the worst case, including gaps.
  - One mission is one sample: Evaluation's "compare many attempts".
- **Objectives.**
  - Finish the mission.
  - Every entry has a plan and a stop.
  - No liquidation and no margin call.
  - Read the campaign report before concluding anything.
- **Debrief.**
  - The reveal (G4).
  - The new **campaign report**: across the campaign run's settled missions, trades, win rate, average R, expectancy in R, cost share of gross P&L and results by revealed regime. When there are fewer than 30 trades it is labeled "too few trades to conclude".
- **Tutorial cards** (5): `report` (new: expectancy and sample size), `regime` (identify first), `calendar` (the release), `funding` (getting paid to carry, and the squeeze risk), `risk` (one bad trade must not end the campaign).

#### D8.3 Realism and learnability checks
Every new mission joins `test_mission_realism.cpp` and `tools/market_study.py`. The pooled indicator bands (42–58%), the news-follow (< 72%), shock spread, fat tails and advisor bands then hold over 12 missions. The gap test counts credible size-2 surprises per mission instead of expecting exactly three.

A new `engine/tests/test_mission_learnability.cpp` plays scripted reference players through the engine (orders, stops, options) over 60 seeds and asserts each mission's bands. The bands state what a disciplined player can learn, and that nothing is guaranteed.

**Buy-and-hold must not be the obvious answer.** A 1× long held from the first eligible open is positive in fewer than 75% of seeds in every mission. Measured today (100 seeds): m01 36%, m02 51%, m03 62%, m04 71%, m05 18%. In m07, m08 and m12 a disciplined reference rule has a better mean return-to-max-drawdown ratio than buy-and-hold.

**No guaranteed outcome.** Every reference strategy below loses in at least 15% of seeds, except m10's payoff property, which is a floor, not a profit.

| Mission | What a disciplined player can learn (60 seeds) |
|---|---|
| m06 | A 2×-equity market order in the thin phase is capacity-cut in ≥ 50% of bars; a limit order at the 20-bar low fills completely within 5 bars in ≥ 50% of the times price trades through it. Range fades entered at market have a mean net ≤ 0 (costs eat the edge); the same fades with limit entries and take-profits have a mean cost per trade below a third of the market version's. A 1×ATR stop-market fills on average ≥ 0.3% beyond its trigger in the thin phase. Buy-and-hold positive in 45–75% |
| m07 | A breakout follower (close beyond the 20-bar range after the headline, 2×ATR stop, 3×ATR trail) wins in 50–80% of seeds and beats buy-and-hold on return/drawdown. A 1×ATR trail exits within 5 bars of entry in > 50% of seeds and keeps on average less than half of what the 3×ATR trail keeps. A fixed 2R target wins more often than the trail but earns less on average in trend seeds. Trend up in 55–75% |
| m08 | Inside Range spans, RSI fades win 55–72% and inside Trend spans < 50%; EMA(20) pullbacks in the trend's direction win 55–72% inside Trend spans and 40–60% inside Range spans. An ADX filter (fade below 20, follow above 25) beats each unfiltered rule by ≥ 3 points and stays < 65%. The first switch after the warm-up lands on no 3 bars in more than a third of seeds. Buy-and-hold positive in 35–65% |
| m09 | An ATM straddle of the first expiry bought 5 bars before the release and sold 3 bars after wins in 25–55% of seeds, with a mean P&L ≤ +5% of the premium; it wins > 70% of the time when the drawn surprise is ±2 and loses > 80% of the time when it is 0. ATM IV falls ≥ 25% (relative) from the release bar to 3 bars after. The same straddle bought 1 bar after the release loses in > 60%. Buy-and-hold positive in 35–65% |
| m10 | A 1× long plus an equal ATM put of the second expiry never loses more than (entry − strike + premium + costs) × quantity (100% of seeds: a payoff property). An unhedged 5× long's 2×ATR stop fills > 2% beyond its trigger in ≥ 40% of seeds with a severe rumor. With no shock (drawn 0) the hedged result is below the unhedged one in > 80% of seeds. A collar at quantity ≤ 50% of equity / price never margin-calls; a 300-put writer margin-calls in ≥ 30% of severe-rumor seeds. A 1× long held through the uptrend pays ≥ 0.8% of notional in funding on average. Buy-and-hold positive in < 60% |
| m11 | Following a scheduled headline from the next open is right in < 65% of cases 10 bars later. Releases with surprise 0 gap < 0.5% on average and releases with \|surprise\| ≥ 1 gap > 2%. A 5× position held through a release with a 1×ATR stop fills > 1% beyond its trigger in ≥ 30% of the releases with \|surprise\| ≥ 1. Positioning in the consensus's direction before the release is right in < 60%. Buy-and-hold positive in 35–65% |
| m12 | None of buy-and-hold, always-fade-RSI, always-follow-MACD, selling a straddle every expiry or carry-long in the crowded state is positive in more than 70% of seeds. The reference rule set (regime filter, 1% risk, flat into the announced release) beats buy-and-hold on return/drawdown and loses in ≥ 20% of seeds. Buy-and-hold positive in 35–65% |

Outcome rates measured at build time go into the README ("What can the player predict?") next to today's tables.

**As built (batch A).** Every m06 band holds. m07 and m08 hold their bands except two, which `test_mission_learnability.cpp` asserts in a weaker form until they are decided (tasks 6.A4, 6.A5):
- m07: with the same 2×ATR stop, a 2R (4×ATR) target wins about as often as the 3×ATR trail (63% each), not more often; it does earn about half as much in trend seeds.
- m08: the ADX filter beats fading everywhere (+8 points) but not the EMA pullback rule (50% each). ADX(14) lags a switch about as long as a 25–50-bar regime lasts, so above 25 it still keeps the pullbacks of the range after a trend. m08's return/drawdown comparison against buy-and-hold is not asserted yet.
- m08 uses a quieter range state with tight walls (reversion 0.005, band 1%) and a trend drift of 0.25% per bar: a stronger trend made fading lose so often in m07 and m08 that the pooled RSI band (≥ 42%) was at its floor.
- `tl_mission_reveal` reports intensity 100% for every event until G2; the reveal also labels m01–m05's segments, so every built-in mission gets the regime band.

**As built (batch B).** G2 and G3 act only where a mission declares them; every earlier golden is byte-identical (no policy bump; `kGeneratorVersion` 4, advisor v2 for `EventAhead`). Every m09–m12 band in the table above holds in `engine/tests/test_mission_learnability_b.cpp` (60 seeds), with these readings and tunings:
- A drawn headline's tone is the sign of its surprise against the consensus (an in-line print or a harmless incident reads neutral). Tone only steers the transient reaction when there is no surprise, and an in-line print keeps the direction it had, so this changed no band.
- m09: the straddle 5 bars before / 3 after wins 35% (mean −8% of the premium), 79% on a ±2 surprise, 18% on 0; ATM IV falls 40% from the release bar to 3 bars after. "The same straddle bought 1 bar after the release" keeps the same exit (3 bars after the release) and loses in 70%. Buy-and-hold 45%. The move after the release is mild (drift 0.08%, vol 0.55%, event vol ×1.3, IV ×0.85): a stronger drawn move made the late straddle win.
- m10: the severe-rumor gap needed `fair_impact` 0.075 and an event IV multiplier of 1.8 for the 300-put writer to be margin-called in ≥ 30% of severe seeds (36%; the stop band: 92% gapped > 2%). The crowded uptrend's funding bias is 0.09% per 4-bar interval (0.06% gave 0.59% of notional, under the 0.8% band; now 0.84%). The floor property holds in 60 of 60 seeds with the long held to the last bar, where the put of the last expiry settles, and costs = fees + funding paid. Collars are checked at 40% of equity / price and never margin-call. With no shock (9 of 60 seeds) the hedged book did worse in 8. Squeezes are off in m10 (its lesson is the rumor, not m02's squeeze). Buy-and-hold 32%.
- m11: 180-bar chain (dwell 30–60) plus a 20-bar closing range that absorbs a per-seed lead of −10..+10 bars, so the releases do not land on fixed bars. Following a scheduled headline is right 38%; |gap| 0.08% without surprise and 3.1% with one; a 5× long's 1×ATR stop jumps > 1% in 38% of the releases with a surprise it was held through; the consensus side before the release is right 45%. Buy-and-hold 53%.
- m12: 30-bar opening range, a 180-bar chain of five states (dwell 25–50; trend drift 0.3%), a 20-bar closing range, lead −10..+10; expiries +40, +100, +160 (230 bars). Fixed strategies: buy-and-hold 50%, always fade RSI 52%, always follow MACD 45%, sell a straddle every expiry 55%, carry long while shorts pay 57% (none above 70%). The reference rule set (ADX regime filter, 1% risk with a 2×ATR stop, flat from 3 bars before an announced release to the bar after) loses in 50% of seeds and beats buy-and-hold on return/drawdown only in the sense that it loses less per unit of drawdown (−0.01 against −0.04): the band holds, but no reference rule is profitable on average in the capstone.
- The `calendar` tutorial art already existed (the calendar perk's card); m09, m11 and m12 reuse it. New art: `straddle` and `report`.
- The campaign report reads a compact copy of each settled mission's engine debrief stored on its campaign leg (trades' net, R and entry regime; fees, spread and slippage, funding), so it adds engine numbers and recomputes no P&L. Average R is shown separately for winning and losing trades; expectancy is the mean R per trade with an R; the cost share is costs over gross P&L (net + costs) when the gross P&L is positive.

#### D8.4 Curriculum and campaign
- **Stage order.** Events moves before Evaluation, so the order becomes Patterns & Indicators, Events, Evaluation. Evaluation is about comparing many attempts across regimes and events, so it is the natural last stage, and its mission is the capstone. Without the swap, the capstone would have to come before the news lesson it builds on. `docs/product-spec.md` is updated to match.
- **Assessments.**

| Stage | Assessment missions | Unlocks |
|---|---|---|
| Foundations | none: Academy lesson (web-client 4.4); coming soon until then, and it does not block | — |
| Structure | m06 | — |
| Orders | m06 | — |
| Futures I | m01 | short |
| Futures II | m03 | leverage |
| Futures III | m02 | — |
| Risk sizing | m07 | — |
| Options I | m04 | optionsBuy |
| Options II | m04, m09 | — |
| Options III | m05, m10 | optionsWrite |
| Patterns & Indicators | m08 | — |
| Events | m11 | — |
| Evaluation | m12 | — |

- **Campaign order:** m06, m01, m03, m02, m07, m04, m09, m05, m10, m08, m11, m12.
- **Locks stay enforceable by the engine.** m06 comes before Futures I, so its own rules disallow shorting (`allow_short = false`), and its 2× cap already meets the locked leverage cap. m09 comes before Options III, so its option spec disallows writing. Every later mission meets no lock. The existing unit test (no campaign mission meets a lock the engine cannot enforce) therefore keeps passing without new start options.
- **Advisor intro.** The advisor and points intro, which the advisor spec requires in "the first mission", moves into m06's tutorial, because m06 now opens the campaign. m01 keeps its card for free play.
- **Batch A as built.** Before batch B, the campaign was m06, m01, m03, m02, m07, m04, m05, m08 (the stage order was still Patterns & Indicators, Evaluation, Events). Batch B (6.B7) made the swap and the twelve-mission order above; twelve of the 13 stages have content and only Foundations is coming soon.

## Risks / Trade-offs

- Offline packs contain the future (inspectable). Accepted: signature = integrity, not secrecy. Competitive modes require online progressive delivery (later).
- Generator realism is limited; calibration report makes the limit explicit (`docs/calibration/generator-v5.md`: fat tails and clustering are there, but volatility clustering decays within about ten bars, faster than in real markets; no licensed reference series yet).
- Score weights are unvalidated pedagogy → configurable, versioned, reviewed by a domain expert before MVP.
- Drawn outcomes (D8, G2) mean that a mission's lesson may not happen on a given seed. For example, m10's rumor changes nothing in 20% of seeds. This is intended, because a hedge that was not needed is part of the lesson. Each reveal says what this seed drew, so the player can tell bad luck from a bad decision.
- Twelve missions make the realism and learnability tests slower (60 seeds × 12 missions plus the reference players). If CI time becomes a problem, the learnability suite can run on fewer seeds in the default job and on 60 seeds in a nightly job. The bands must not be widened instead.
- `mission_reveal` is a pure function of mission id and seed, so a modified client could call it early. Offline packs already contain the future (first risk above), so this is accepted. The official clients call it only after the run has ended, and a worker-schema test enforces that.

## Open Questions

- Content team ownership for PT-BR lesson copy and review cadence.
- Whether calibration questions are mandatory in every campaign mission or only in later chapters.
