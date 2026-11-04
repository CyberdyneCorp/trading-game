# Design: add-scenarios-and-gameplay

## Context

Scenarios must travel to browsers, desktops and iPads, work offline, be verifiable, and never leak the future through UI or accessibility. Scoring and debrief numbers must match between client and server.

## Decisions

### D1. Pack container
- `.tlpack` = uncompressed tar of: `manifest.json` (canonical JSON, RFC 8785), `manifest.sig` (Ed25519 over the canonical manifest bytes), and data files: `bars.bin` (columnar little-endian int64, zstd), `funding.bin`, `events.bin`, `options.bin`, `mission.json`, `reveal.json`, `i18n/*.json`.
- Hashes in the manifest are SHA-256 of each file's stored bytes; the signature therefore covers all data.
- Publisher public keys pinned in each client build with a rotation list (`signingKeyId`). Private imports are signed by a per-device key and flagged `private`.
- Web verifies with WebCrypto Ed25519; native with libsodium/CryptoKit; server with `cryptography`.

### D2. Scoring and debrief live in the engine repo
- Scoring (C++20, `engine/src/scoring.cpp` + `engine/include/tradelab/scoring.hpp`, part of the engine library, versioned by `kScoringVersion` = 2) computes score dimensions, findings, per-trade rows (R multiple, plan respected) and the next-lesson code from the run state and command log; the C ABI debrief adds P&L aggregates and buy-and-hold/flat comparisons. Greek attribution is not built yet. Same golden approach as the engine. Clients render codes as text; the server (`services/api`) recomputes by replaying the action log through the Python binding.
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

### D4. Masking
- Factor `100 / firstPlayableOpen` computed from the first playable bar only. Strike grids generated after masking. Rounding declared per pack (half-even to tick).

### D5. Packer CLI
- `tools/packer` (Python, using the nanobind engine): `validate`, `import-csv`, `mask`, `generate`, `build`, `sign`, `inspect`. Validation logic shared with the client import wizard through the engine's C ABI so web/desktop/iPad import produce identical errors.

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
