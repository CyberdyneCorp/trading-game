# Design: add-scenarios-and-gameplay

## Context

Scenarios must travel to browsers, desktops and iPads, work offline, be verifiable, and never leak the future through UI or accessibility. Scoring and debrief numbers must match between client and server.

## Decisions

### D1. Pack container
- `.tlpack` = uncompressed tar of: `manifest.json` (canonical JSON, RFC 8785), `manifest.sig` (Ed25519 over the canonical manifest bytes), and data files: `bars.bin` (columnar little-endian int64, zstd), `funding.bin`, `events.bin`, `options.bin`, `mission.json`, `reveal.json`, `i18n/*.json`.
- Hashes in the manifest are SHA-256 of each file's stored bytes; the signature therefore covers all data.
- Publisher public keys pinned in each client build with a rotation list (`signingKeyId`). Private imports are signed by a per-device key and flagged `private`.
- ~~Web verifies with WebCrypto Ed25519; native with libsodium/CryptoKit; server with `cryptography`.~~ As built: verification (and signing of private imports) runs in the engine, so every client and the server share one verifier through the C ABI. `cryptography` remains an independent check in the Python tests.

### D1a. Pack implementation choices (as built)
- **Ed25519: Monocypher 4.0.2**, vendored unmodified (`engine/third_party/monocypher`, its RFC 8032 SHA-512 variant in `optional/monocypher-ed25519.c`). Chosen over TweetNaCl (slower, unaudited, `randombytes` hook) and libsodium (too large to vendor, needs its own build): two C99 files, CC0/BSD, audited by Cure53 in 2020, deterministic signing, no allocation, builds unchanged for native and Emscripten. Tested with RFC 8032 vectors.
- **zstd: the official 1.5.7 decoder**, vendored as upstream's decoder-only single-file input (`zstddeclib-in.c`) plus the decoder sources it includes (no compressor, no legacy formats, no assembly). Chosen over the "educational" decoder (not meant for hostile input) and third-party ports: it is what OSS-Fuzz fuzzes continuously. Decompression goes into a buffer of exactly the declared size, so a bomb stops at `dstSize_tooSmall` instead of allocating.
- **Size:** the whole engine WASM module, with both libraries, is about 157 KB gzipped (budget 400 KB).
- **Canonicalization:** RFC 8785 over a strict JSON subset (integers within 2^53, no fractions or exponents, valid UTF-8, no duplicate keys). Under that subset JCS number formatting is plain digits, so no floating point is involved. Money and rates are decimal strings anyway.
- **Container:** ustar with flat names (`[a-z0-9._-]`, no prefix, regular files only), deterministic headers (zero mtime/uid/gid). `manifest.json` is stored in canonical form; `manifest.sig` holds the signature (the manifest's "signature" field of the spec lives in that file).
- **Data:** `bars.bin` and `funding.bin` are columnar little-endian int64 tables with a 16-byte header, compressed as zstd frames. Private imports store them as raw zstd blocks (still valid zstd) so clients need no encoder; the packer will compress published packs. `events.bin`, `options.bin`, `mission.json` and `i18n.json` are reserved until packs carry news and option chains.
- **Limits:** pack 16 MiB, 8 files, manifest 64 KiB, 8 MiB per decompressed file, 20,000 bars; CSV 4 MiB and 20,000 rows. Parsers are fuzzed with libFuzzer (`engine/fuzz`).
- **Private CSV packs:** practice rules (10,000 capital, chosen leverage cap 1–20, warm-up of a quarter of the rows clamped to 20–60, perpetual only, default costs), funding every 8 hours of bars, `license.name = private-import`, never publishable (`private_pack`).

### D2. Scoring and debrief live in the engine repo
- Scoring (C++20, `engine/src/scoring.cpp` + `engine/include/tradelab/scoring.hpp`, part of the engine library, versioned by `kScoringVersion` = 6; v6 adds advisor points and advice outcomes) computes score dimensions, findings, per-trade rows (R multiple, plan respected) and the next-lesson code from the run state and command log; the C ABI debrief adds P&L aggregates and buy-and-hold/flat comparisons.
- Report-only debrief facts (`engine/src/debrief.cpp`, scoring v4, fixed in v5) are recomputed from the event log and the scenario, never stored in the run state, so they change no digest: spread/slippage cost (each taker fill against its reference price, each option fill against the mid of its quote), option P&L by Greek (bar by bar, Greeks of the start quote of each interval, residual = the rest of the option P&L) and, per perpetual trade, the stop, the highest leverage the position carried (at entry, or a higher re-leverage while open), the liquidation price at that leverage and the highest leverage that would have kept the stop in front (found by re-running the engine's liquidation formula from the trade's leverage down to 1×). Theta or vega explaining at least half of an option loss is a finding that maps to the Options lesson. Scoring v5 also adds an open trade's unrealized P&L (valued like the account equity) to its net, withholds good-process credit when an option was written, and credits a disciplined win (`good_process_win`). Option quotes come from the same deterministic pricing kernel; everything else is integer arithmetic. Same golden approach as the engine. Clients render codes as text; the server (`services/api`) recomputes by replaying the action log through the Python binding.
- Mission rules/rubrics are data in the pack, not code. Until packs exist, the five built-in missions live in `engine/src/missions.cpp` (generator spec, rules, option chain) with their texts in the client catalogs (`apps/web/src/lib/game/missions.ts`).

### D3. Generator
- `engine/src/generator.cpp` (`engine/include/tradelab/generator.hpp`): regime segments (scripted per mission today; Markov switching pending) + GARCH(1,1)-like variance (`v ← 0.85·v + 0.15·z²`) + Bernoulli-per-bar jumps; volume log-normal tied to |return| and segment volume multiplier; funding = clamp(baseline + k·EMA(return) + segment bias, ±0.3%) with no noise term; IV base = annualized segment vol·√variance·(1 + premium)·segment/event IV multipliers.
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
- As built: `tools/packer/packer.py` uses the existing ctypes binding (not nanobind) and offers `key`, `import-csv` and `validate`; the other commands come with publisher keys and Market4Me (1b.x).

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

## Risks / Trade-offs

- Offline packs contain the future (inspectable). Accepted: signature = integrity, not secrecy. Competitive modes require online progressive delivery (later).
- Generator realism is limited; calibration report makes the limit explicit.
- Score weights are unvalidated pedagogy → configurable, versioned, reviewed by a domain expert before MVP.

## Open Questions

- Content team ownership for PT-BR lesson copy and review cadence.
- Whether calibration questions are mandatory in every campaign mission or only in later chapters.
