# Tasks: add-scenarios-and-gameplay

## 1. Pack format
- [x] 1.1 Manifest JSON schema + canonicalization + Ed25519 signing/verification — schema v1 (`engine/src/pack_manifest.cpp`, field list in `engine/README.md`), RFC 8785 canonical form over a strict integer-only JSON subset (`json_in.cpp`), Ed25519 via vendored Monocypher 4.0.2 (RFC 8032 vectors tested; signatures cross-checked with Python `cryptography`). The publisher key ceremony and the pinned production key list are a separate, human step (clients pin an empty list today)
- [ ] 1.2 Columnar data files with zstd; size/count limits; decompression guard — done: `bars.bin` and `funding.bin` columnar int64 in zstd frames (vendored zstd 1.5.7 decoder), pack/file/manifest/bar/row limits, decompression into exactly the declared size (bomb tests); pending: `events.bin` and `options.bin` encodings (reserved, refused as `unsupported_feature`), real compression in the packer (private imports store raw zstd blocks)
- [x] 1.3 Validator in engine C ABI; tests DATA01, DATA02, LIC01 — `tl_pack_validate`, `tl_csv_validate`, `tl_run_create_pack`/`tl_run_restore_pack` (`engine/src/c_api_pack.cpp`); DATA01 in `test_csv_import.cpp`, DATA02 and LIC01 in `test_pack.cpp`, mirrored in `bindings/python/tests/test_packs.py` and the web E2E
- [x] 1.4 Masking with geometry-preservation property test — `engine/src/masking.cpp` (factor from the open of the bar at the first decision point, half-even to tick, volume scaled inversely); property test over 200 random windows plus a no-future-data test (`test_masking.cpp`); per-run alias from the seed

## 1b. Market4Me historical ingestion
- [ ] 1b.1 Register `cyb_tradelab_packer` (client-credentials, audience `market4me-api`) and get it allowlisted as `analyst` on Market4Me
- [ ] 1b.2 `packer m4m-pull`: bulk export (bars incl. 1m, funding/mark/index, DVOL) with pinned `as_of`; verify manifest checksums
- [ ] 1b.3 Availability-time gating, revision handling, gap check (`source_gap`) with test on the 2026-10-21→11-02 hole
- [ ] 1b.4 Regime/vol-bucket window selector; diversity report per campaign
- [ ] 1b.5 Legal review request for Binance/Deribit-derived packs (LIC gate)

## 2. Import
- [x] 2.1 CSV parser + validation rules + gap report — `engine/src/csv_import.cpp`: order, duplicates, positive prices, non-negative volume, OHLC consistency, timestamp unit (DATA01), interval coherence, gaps reported and never filled; seconds, milliseconds or UTC ISO dates; quote volume converted; funding carried forward
- [x] 2.2 Fuzz tests for import (libFuzzer on native) — `engine/fuzz/` targets for CSV import (and the pack built from it), pack validation and the JSON reader (canonical form is a fixed point); CI job `fuzz` runs each for 60 s with ASan + UBSan

## 3. Generator
- [ ] 3.1 Regime/vol/jump process, volume, funding, IV base — scripted segments, GARCH-like vol clustering, jumps, volume tied to |return|, funding rule and IV base done (`engine/src/generator.cpp`); Markov regime transitions pending (missions script their segments)
- [x] 3.1a Generator v2 market structure (fair value, range/trend anchors, momentum, compression, tilted breakouts, crowding squeezes) with calibration tests (`engine/tests/test_generator.cpp`); missions m01–m05 redesigned; execution policy v5
- [ ] 3.2 Event model with seeded reaction distributions — done: release step, credibility, expectation/actual (surprise), seeded reaction direction (none / along the surprise / reversed), transient drift, volatility, volume and IV channels, duration, fair-value impact (surprise × credibility); test of the reaction distribution over 1,000 seeds. Pending: lead time, jump/spread/capacity/funding channels, sampled intensity (magnitude is fixed per event)
- [ ] 3.3 Player-independence and branch-label tests — player independence done (`test_missions.cpp` "events never depend on the player"); branch-label test pending (no branch mode yet)
- [ ] 3.4 Calibration report notebook + first report
- [x] 3.5 Realism: news priced on arrival (opening gap), per-seed mission schedule, mission-level realism tests and `tools/market_study.py` (design D3b)

## 4. Gameplay rules and scoring
- [ ] 4.1 Mission schema (objectives, permissions, leverage cap, warnings) — engine `MissionRules` has initial balance, leverage cap, warm-up and short permission, plus option short-writing permission; objectives are client i18n text (`apps/web/src/lib/game/missions.ts`), the liquidation-distance warning is a fixed 3% in the client, the briefing does not yet list option buy/write permissions, and a stop beyond liquidation is warned but needs no explicit confirmation
- [ ] 4.2 Plan/journal/wait decision records in the command log — plans and calibration answers are engine commands; every advance is a logged client action, but there is no explicit engine `wait` record and the debrief only reports "waited" when the whole mission had no entries
- [x] 4.3 `engine/scoring`: dimensions, renormalization, Brier, golden tests EDU01/EDU02 (`engine/src/scoring.cpp`, `engine/tests/test_scoring.cpp`)
- [x] 4.4 Debrief aggregates + Greek attribution + next-lesson mapping — realized P&L, fees, funding, option realized, a separate spread/slippage line, max drawdown, peak leverage, turnover, closest liquidation distance, buy-and-hold and flat comparison, per-trade R table (perpetual and option trades, expiry included), option P&L by Greek (delta, gamma, theta, vega, residual; `engine/src/debrief.cpp`), per-trade stop vs liquidation with the leverage that would have kept the stop in front (also in the liquidation and stop-beyond-liquidation findings), theta/IV findings, next-lesson codes (Futures II, Risk, Futures III, Orders, Structure, Evaluation, Options II); scoring v4, report-only (no digest change); v5 playtest fixes: open trades net their unrealized P&L, safe leverage judged at the highest leverage held (re-leverage included), no good-process credit for written options, `good_process_win` finding; v7: plans matched to their entry order (a plan recorded on a fill bar for the next order no longer replaces that trade's plan; an add adds its planned risk), written-off shortfall line after a margin-call bankruptcy; tests in `engine/tests/test_debrief.cpp`, `apps/web/src/lib/game/debrief.test.ts` and the debrief e2e
- [ ] 4.5 Understanding dimension from structured answers and human-reviewed rubrics (scoring v7 still always reports it non-applicable)
- [ ] 4.6 Risk dimension inputs beyond scoring v1: adherence to planned risk, leverage vs stop distance, liquidation distance kept, drawdown vs a mission-declared budget — done in v3: planned risk judged per executed trade (rejected orders no longer count), option margin calls penalized like liquidations; pending: leverage vs stop distance, liquidation distance kept, mission-declared drawdown budget
- [ ] 4.7 Campaign runs: capital carried between missions, bankruptcy ends the campaign run (single-mission game-over screen → debrief is done)
- [ ] 4.8 Assisted practice: rewind / change seed as a new attempt flagged `assisted`, excluded from progress

## 5. Content
- [ ] 5.1 `tools/packer` CLI — minimal `tools/packer/packer.py` over the Python binding: `key`, `import-csv`, `validate` (incl. publish/LIC01); pending: `mask` of external windows, `generate`, `build` with real zstd compression, publisher `sign`, `inspect`, `m4m-pull` (1b.x)
- [x] 5.2 Phase-1 content: 1 synthetic perp mission (breakout fails, stop works) — `m01-false-breakout`, built into `engine/src/missions.cpp` (moves to a pack with 1.x)
- [ ] 5.3 Phase-2 content: 12 missions, ≥4 regimes, 3 event families, options chapters — 5 built-in missions so far (m01–m03 perpetuals, m04 options buying, m05 options writing)
- [ ] 5.4 Curriculum graph and unlock rules as data; tests for gating and non-destructive failure
