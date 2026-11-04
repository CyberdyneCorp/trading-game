# Tasks: add-scenarios-and-gameplay

## 1. Pack format
- [ ] 1.1 Manifest JSON schema + canonicalization + Ed25519 signing/verification
- [ ] 1.2 Columnar data files with zstd; size/count limits; decompression guard
- [ ] 1.3 Validator in engine C ABI; tests DATA01, DATA02, LIC01
- [ ] 1.4 Masking with geometry-preservation property test

## 1b. Market4Me historical ingestion
- [ ] 1b.1 Register `cyb_tradelab_packer` (client-credentials, audience `market4me-api`) and get it allowlisted as `analyst` on Market4Me
- [ ] 1b.2 `packer m4m-pull`: bulk export (bars incl. 1m, funding/mark/index, DVOL) with pinned `as_of`; verify manifest checksums
- [ ] 1b.3 Availability-time gating, revision handling, gap check (`source_gap`) with test on the 2026-10-21→11-02 hole
- [ ] 1b.4 Regime/vol-bucket window selector; diversity report per campaign
- [ ] 1b.5 Legal review request for Binance/Deribit-derived packs (LIC gate)

## 2. Import
- [ ] 2.1 CSV parser + validation rules + gap report
- [ ] 2.2 Fuzz tests for import (libFuzzer on native)

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
- [x] 4.4 Debrief aggregates + Greek attribution + next-lesson mapping — realized P&L, fees, funding, option realized, a separate spread/slippage line, max drawdown, peak leverage, turnover, closest liquidation distance, buy-and-hold and flat comparison, per-trade R table (perpetual and option trades, expiry included), option P&L by Greek (delta, gamma, theta, vega, residual; `engine/src/debrief.cpp`), per-trade stop vs liquidation with the leverage that would have kept the stop in front (also in the liquidation and stop-beyond-liquidation findings), theta/IV findings, next-lesson codes (Futures II, Risk, Futures III, Orders, Structure, Evaluation, Options II); scoring v4, report-only (no digest change); v5 playtest fixes: open trades net their unrealized P&L, safe leverage judged at the highest leverage held (re-leverage included), no good-process credit for written options, `good_process_win` finding; tests in `engine/tests/test_debrief.cpp`, `apps/web/src/lib/game/debrief.test.ts` and the debrief e2e
- [ ] 4.5 Understanding dimension from structured answers and human-reviewed rubrics (scoring v5 still always reports it non-applicable)
- [ ] 4.6 Risk dimension inputs beyond scoring v1: adherence to planned risk, leverage vs stop distance, liquidation distance kept, drawdown vs a mission-declared budget — done in v3: planned risk judged per executed trade (rejected orders no longer count), option margin calls penalized like liquidations; pending: leverage vs stop distance, liquidation distance kept, mission-declared drawdown budget
- [ ] 4.7 Campaign runs: capital carried between missions, bankruptcy ends the campaign run (single-mission game-over screen → debrief is done)
- [ ] 4.8 Assisted practice: rewind / change seed as a new attempt flagged `assisted`, excluded from progress

## 5. Content
- [ ] 5.1 `tools/packer` CLI
- [x] 5.2 Phase-1 content: 1 synthetic perp mission (breakout fails, stop works) — `m01-false-breakout`, built into `engine/src/missions.cpp` (moves to a pack with 1.x)
- [ ] 5.3 Phase-2 content: 12 missions, ≥4 regimes, 3 event families, options chapters — 5 built-in missions so far (m01–m03 perpetuals, m04 options buying, m05 options writing)
- [ ] 5.4 Curriculum graph and unlock rules as data; tests for gating and non-destructive failure
