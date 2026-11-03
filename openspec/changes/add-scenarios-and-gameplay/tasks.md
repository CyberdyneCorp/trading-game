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
- [ ] 3.1 Regime/vol/jump process, volume, funding, IV base
- [x] 3.2 Event model with seeded reaction distributions
- [ ] 3.3 Player-independence and branch-label tests
- [ ] 3.4 Calibration report notebook + first report

## 4. Gameplay rules and scoring
- [ ] 4.1 Mission schema (objectives, permissions, leverage cap, warnings)
- [ ] 4.2 Plan/journal/wait decision records in the command log
- [ ] 4.3 `engine/scoring`: dimensions, renormalization, Brier, golden tests EDU01/EDU02
- [ ] 4.4 Debrief aggregates + Greek attribution + next-lesson mapping

## 5. Content
- [ ] 5.1 `tools/packer` CLI
- [ ] 5.2 Phase-1 content: 1 synthetic perp mission (breakout fails, stop works)
- [ ] 5.3 Phase-2 content: 12 missions, ≥4 regimes, 3 event families, options chapters
- [ ] 5.4 Curriculum graph and unlock rules as data; tests for gating and non-destructive failure
