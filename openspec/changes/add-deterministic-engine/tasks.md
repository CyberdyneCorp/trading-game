# Tasks: add-deterministic-engine

## 1. Skeleton and toolchain
- [x] 1.1 CMake project `engine/` (C++20, presets for native/wasm), clang-tidy + cognitive-complexity gate (≤ 15)
- [x] 1.2 Compiler flags: `-ffp-contract=off`, no fast-math, `-fno-exceptions -fno-rtti` for wasm; CI check that forbids `std::exp/log/erf/*_distribution` in `engine/src`
- [ ] 1.3 CI matrix: linux-x86_64, macos-arm64, wasm (node), python wheel — workflow written (native+wasm+lint+openspec); pending first GitHub run and python wheel (8.3)

## 2. Numerics and determinism
- [x] 2.1 `Fixed<Scale>` types with `__int128` mul/div and explicit rounding modes; overflow detection
- [x] 2.2 Deterministic kernels `det::exp/log/sqrt/cos/norm_cdf` + accuracy tests against high-precision reference tables
- [x] 2.3 PCG32 PRNG with stream ids; Box-Muller normals
- [x] 2.4 Canonical serialization primitives (LE writer/reader) + SHA-256 digest

## 3. Simulation core
- [ ] 3.1 Scenario model + validation; RunState; Command/Event types; IDL → C++/TS types
- [ ] 3.2 Step loop with phases in spec order; event sequence numbering
- [x] 3.3 Command idempotency (client command id) and sequence/step validation
- [ ] 3.4 Property test: no-lookahead (mutate future, compare prefix) — SIM01
- [x] 3.5 RunState checkpoint save/restore with version check; resume-equals-uninterrupted test

## 4. Order execution
- [x] 4.1 Market, limit, stop-market, take-profit, trailing (close-based)
- [ ] 4.2 Lifecycle states and events; rejection reasons enum
- [ ] 4.3 Costs: fees, spread, slippage, tick/lot; capacity with priority
- [x] 4.4 Bracket/OCO, reduce-only, orphan cancellation
- [ ] 4.5 Conservative intrabar policy with `ambiguousIntrabar` flags
- [x] 4.6 Goldens SIM03–SIM07

## 5. Perpetual futures
- [x] 5.1 One-way positions, average entry, flips
- [x] 5.2 Isolated margin, leverage caps, add/remove margin
- [x] 5.3 Liquidation price + liquidation (adverse extreme first, gap handling, insurance fund)
- [x] 5.4 Funding from series or synthetic rule
- [x] 5.5 Property tests: reconciliation (SIM08), non-negative balances; goldens FUT01–FUT04

## 6. Options
- [x] 6.1 Series listing, Black-76 pricing, spread, quantization
- [x] 6.2 IV model (realized-vol base, term, skew, smile, event shocks) — past data only
- [x] 6.3 Greeks
- [x] 6.4 Buy/write, option margin, margin call and buy-back
- [x] 6.5 Expiry settlement; goldens OPT01–OPT04

## 7. Indicators
- [x] 7.1 SMA, EMA, RSI, ATR, volume with warm-up states and gap policy
- [x] 7.2 Reference-value tests against a published dataset

## 8. Bindings
- [ ] 8.1 C ABI facade
- [ ] 8.2 WASM module + generated TS types; size budget check (< 400 KB gz)
- [ ] 8.3 nanobind Python wheel
- [ ] 8.4 All goldens green on every target; document the golden-update procedure in `engine/README.md`
