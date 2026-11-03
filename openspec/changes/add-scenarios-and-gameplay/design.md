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
- `engine/scoring/` (C++20, separate target, versioned by `scoringVersion`) computes score dimensions, debrief aggregates and Greek attribution from the event stream. Same golden approach as the engine. Clients display; the server recomputes.
- Mission rules/rubrics are data in the pack, not code.

### D3. Generator
- `engine/generator/`: Markov regime switching + GARCH(1,1)-like variance + compound Poisson jumps; volume as log-normal tied to |return| and regime; funding = clamp(k·EMA(drift) + baseline + noise); IV base = sqrt(variance state)·(1 + premium) + event shocks.
- Runs offline at authoring time to bake bars into packs (fast load, inspectable), and at play time only for seeded sandbox variants. Both paths use the same deterministic code.

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
