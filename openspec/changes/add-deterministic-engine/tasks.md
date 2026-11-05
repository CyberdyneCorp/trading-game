# Tasks: add-deterministic-engine

## 1. Skeleton and toolchain
- [x] 1.1 CMake project `engine/` (C++20, presets for native/wasm), clang-tidy + cognitive-complexity gate (≤ 15)
- [x] 1.2 Compiler flags: `-ffp-contract=off`, no fast-math, `-fno-exceptions -fno-rtti` for wasm; CI check that forbids `std::exp/log/erf/*_distribution` in `engine/src`
- [x] 1.3 CI matrix (`.github/workflows/engine.yml`): linux-x86_64, macos-arm64 (debug + release), wasm (node), python (ctypes binding tests against `libtradelab`), lint, openspec — green on GitHub

## 2. Numerics and determinism
- [x] 2.1 `Fixed<Tag, Decimals>` types with `__int128` mul/div and explicit rounding modes; overflow detection
- [x] 2.2 Deterministic kernels `det::exp/log/sqrt/cos/norm_cdf` + accuracy tests against high-precision reference tables
- [x] 2.3 PCG32 PRNG with stream ids; Box-Muller normals
- [x] 2.4 Canonical serialization primitives (LE writer/reader) + SHA-256 digest

## 3. Simulation core
- [ ] 3.1 Scenario model + validation; RunState; Command/Event types; IDL → C++/TS types — done: `scenario.hpp` + `validate()`, `state.hpp`, `engine.hpp`, `types.hpp`; pending: the IDL (`engine/idl/`) and generated TS types (the web client's `apps/web/src/lib/engine/types.ts` is hand-written)
- [x] 3.2 Step loop with phases in spec order (open → option open → intrabar → close); global event sequence numbering
- [x] 3.3 Command idempotency (client command id) and sequence/step validation
- [x] 3.4 No-lookahead test: mutate future bars, compare digests at `t` — SIM01 (`tests/test_engine.cpp`), plus indicator, pattern and option-quote (OPT04) variants
- [x] 3.5 RunState checkpoint save/restore with version check; resume-equals-uninterrupted test

## 4. Order execution
- [x] 4.1 Market, limit, stop-market, take-profit, trailing (close-based)
- [x] 4.2 Lifecycle states and events; rejection reasons enum (`types.hpp`; `triggered` is defined but not emitted, see spec "Order lifecycle")
- [x] 4.3 Costs: fees, spread, slippage, tick/lot; capacity with priority — maker/taker fees, half-spread + slippage, tick/lot validation, shared per-bar capacity from bar volume with `capacityFromBarVolume` on limited fills; within each phase capacity is served by price (the level reached first), then sequence (policy v16, design D6j, `tests/test_policy_v16.cpp`)
- [x] 4.4 Bracket/OCO, reduce-only, orphan cancellation (policy v14: an exit larger than the position it closes cancels its remainder on the same bar); `MoveOrder` (policy v13) moves a working stop/take-profit in place, keeping its OCO link (`tests/test_brackets.cpp`, Python replay action `move`)
- [x] 4.5 Conservative intrabar policy with `ambiguousIntrabar` flags
- [x] 4.6 Goldens SIM03–SIM07
- [x] 4.7 Percentage trailing distance (policy v16, design D6j): `trail_rate` command/order field, `tl_place_trailing_rate`, Python `Run.place_trailing_rate` and replay `trailRate`, web protect-position percent option (`TrailField.svelte`)

## 5. Perpetual futures
- [x] 5.1 One-way positions, average entry, flips
- [x] 5.2 Isolated margin, leverage caps, add/remove margin
- [x] 5.3 Liquidation price + liquidation (adverse extreme first, gap handling, insurance fund)
- [x] 5.4 Funding from series or synthetic rule (the generator emits the funding series)
- [x] 5.5 Property tests: reconciliation (SIM08), non-negative balances; goldens FUT01–FUT04
- [x] 5.6 Maintenance-margin tiers by notional (policy v16, design D6j): `ContractSpec.margin_tiers` keyed by notional at entry for the liquidation price, `leverage_above_tier` on placement, fill and `SetLeverage`, debrief safe leverage and the web ticket's liquidation estimate and tier warning use the tier; default tiers on built-in missions
- [ ] 5.7 Mark-price bars for unrealized P&L, margin and liquidation, and the scenario flag `markFromLastPrice` when absent — pending: everything uses last-price closes and no scenario flag is exposed

## 6. Options
- [x] 6.1 Series listing, Black-76 pricing, spread, quantization
- [x] 6.2 IV model (base from the scenario `iv_base` series or realized vol, term, skew, smile; event shocks via the generator's `iv_base`) — past data only
- [x] 6.3 Greeks
- [x] 6.4 Buy/write, option margin, margin call and buy-back
- [x] 6.5 Expiry settlement (TWAP of the last `settlement_bars` closes, flagged approximate for one bar); goldens OPT01–OPT04
- [ ] 6.6 Settlement window in market time: TWAP from sub-bars and rejection with `series_settling` from the start of the window — pending: scenarios have no sub-bars and the reason does not exist

## 7. Indicators
- [x] 7.1 SMA, EMA, RSI, ATR, volume with warm-up (values absent until warm-up)
- [ ] 7.2 Reference-value tests against a published dataset — done: hand-computed small-series values for every indicator (`tests/test_indicators.cpp`); pending: a published reference dataset
- [x] 7.3 VWAP (24-bar sessions), MACD(12,26,9), ADX(14) with +DI/−DI; indicator version 2; hand-computed tests and no-lookahead checks
- [x] 7.4 Bollinger(20, 2) with exact integer square root; indicator version 3
- [x] 7.5 Candle pattern detection (29 patterns, context-aware), pattern version 1, calibration test
- [ ] 7.6 Continuity bars: scenario gap policy (skip or carry forward) for indicators and zero executable capacity — pending: `Bar` has no continuity marker and no gap policy exists

## 8. Bindings
- [x] 8.1 C ABI facade (`engine/include/tradelab/c_api.h`, JSON out, decimal strings in)
- [ ] 8.2 WASM module + generated TS types; size budget check (< 400 KB gz) — done: ES module `tradelab.mjs` (single file, ~66 KB gz) built by `apps/web/scripts/build-engine.sh` and tested under node in CI; pending: generated TS types (see 3.1) and a CI size-budget check
- [x] 8.3 Python binding `tradelab_engine` (ctypes over the C ABI, `bindings/python/`), loads `libtradelab` (`tradelab_capi`) from `TRADELAB_LIB` or the build tree; the wheel does not bundle the shared library (the API image builds it)
- [x] 8.4 All goldens green on every target (frozen digests in the engine tests, mirrored by the Python test); golden-update procedure in `engine/README.md`
