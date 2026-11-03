# Proposal: add-scenarios-and-gameplay

## Why

The engine simulates; the game teaches. We need a portable, signed scenario format (historical masked or synthetic), a generator for regimes and events, and the client-agnostic rules of a mission: briefing, plan, decisions, debrief, scoring and curriculum gating of leverage and options. Defining these once keeps web, desktop and iPad behavior identical.

## What Changes

- Scenario pack format (manifest + data), signing, validation, masking of historical data, licensing metadata.
- Private CSV import with validation (OHLCV + optional funding).
- Synthetic market generator: regimes, stochastic volatility, jumps, funding and IV dynamics, scripted events.
- Mission model: objectives, instrument permissions, leverage cap, short-options flag, decision windows.
- Gameplay loop rules: plan before advance, journal, wait as a first-class action, assisted-practice rewinds.
- Debrief contents and P&L attribution (costs, funding, liquidation, option Greeks).
- Scoring (risk, plan coherence, understanding, calibration) with renormalization.
- Curriculum progression that unlocks shorting, higher leverage caps and options writing through demonstrated understanding only.

## Capabilities

### New Capabilities
- `scenario-packs`: manifest, data, signing, masking, import, licensing.
- `market-generator`: synthetic paths, regimes, events, funding and IV dynamics.
- `gameplay-loop`: missions, plan/journal, decision flow, debrief.
- `mission-scoring`: scoring dimensions, calibration questions, anti-gaming rules.
- `curriculum`: lessons, progression and unlock rules.

### Modified Capabilities
None.

## Non-goals

- Lesson copy and art (content work tracked separately).
- Online comparable challenges and leaderboards.
- Editorial admin UI (only the pack format and validator CLI here).
- LLM features at play time.

## Impact

- New: `content/` (pack sources), `tools/packer` (C++ or Python CLI that builds/signs/validates packs using the engine), `fixtures/scenarios/`.
- Depends on `add-deterministic-engine`. Consumed by all clients and the API.
