# Design: add-deterministic-engine

## Context

Four clients (web, desktop, iPad, Android later) and one server must agree bit-for-bit on every fill, margin figure, funding payment, liquidation and option premium. The desktop client is C++20, so a C++20 core removes a language from the stack (v0.1 proposed Rust). Option pricing needs transcendental functions, which are not bit-identical across `libm` implementations (glibc, Apple libm, musl/Emscripten), so determinism must be designed, not assumed.

## Goals / Non-Goals

**Goals:** one engine, identical results on native/WASM/Python/Swift; explainable accounting; cheap to test with golden vectors; small, embeddable, no dependencies at runtime.

**Non-Goals:** realism beyond what teaching needs (no order book, no market impact); performance beyond ~1 ms per step on mid-range phones.

## Decisions

### D1. C++20, header-light static library, C ABI facade
- `engine/include/tradelab/*.hpp` (C++ API) plus `engine/include/tradelab/c_api.h` (stable C ABI: opaque `tl_run` handle; commands take scalars and decimal strings, results and state are returned as UTF-8 JSON with monetary values as decimal strings). A run is created from a built-in mission id and a seed (`tl_run_create`); the scenario is generated inside the engine. JSON was chosen over a binary encoding to keep the bindings trivial; state never round-trips through it (only checkpoints, which are hex-encoded canonical binary).
- C ABI is what WASM, JNI and Python bind to; Swift uses C++ interop directly against the C++ API but goldens run through both.
- No exceptions across the ABI: `Result<T>` with a stable `ErrorCode` internally, reason/error strings externally. Built with `-fno-exceptions -fno-rtti` for WASM.
- Alternative considered: Rust (v0.1). Rejected: desktop would need a second systems language and FFI for its main dependency.

### D2. Numerics
- `Fixed<Tag, Decimals>` int64 types with engine-wide scales (execution-policy v1): `Price` 1e-8, `Qty` 1e-8, `Money` 1e-8 credits, `Rate` 1e-10 (funding/fees/margin rates), `Vol` 1e-8. Fixed scales keep types compile-time checked; scenarios express granularity through tick/lot sizes that must be multiples of these scales. Headroom: int64 at 1e-8 holds up to ~9.2e10 units; intermediate products use `__int128`.
- Decimal strings (`"123.45"`) are the only text form: strict parser (no exponent, no excess fractional digits — never silent rounding).
- Multiplication/division use `__int128` intermediates with explicit rounding mode per call site (`floor`, `ceil`, `half_even`, `against_player`). Rounding modes are part of the execution-policy version.
- Overflow checks on every arithmetic op in debug and on boundary ops (notional, P&L) in release.

### D3. Floating point only inside pricing kernels
- Black-76, Greeks and IV use `double` with: `-ffp-contract=off`, no `-ffast-math`, no `long double`, no `std::exp/log/erf`. Engine kernels `det::exp`, `det::log`, `det::norm_cdf` implemented with argument reduction + fixed polynomial coefficients using only `+ − × ÷ sqrt` (IEEE-754 correctly rounded on all targets, including WASM).
- Results quantized to `Money`/`Vol` fixed-point immediately; state never stores doubles.
- x87 is excluded (x86-64 SSE2 only). Verified by the golden matrix, not by reasoning.

### D4. PRNG
- PCG32 (XSH-RR) with explicit stream ids per purpose (`Market` 1, `Events` 2, `Slippage` 3, `Sandbox` 4; never renumbered). Normal variates via engine Box-Muller using `det::log`/`det::sqrt`/`det::cos` kernels. Never `std::*_distribution` (enforced by `tools/check_engine_bans.py`).

### D5. Data model and step loop
- `Scenario` (immutable, validated), `RunState` (positions, orders, balances, rng, step, full event log), `Command`, `Event`.
- `start_run(sc, seed)`, `apply_command(sc, st, cmd) -> CommandResult` (at the current decision point, before advancing) and `advance(sc, st)` (one bar) implement the fixed step sequence from the spec. `advance` runs `open_phase` (gap liquidation, gap-triggered stops/limits, market orders), `option_open_phase`, `intrabar_phase` (adverse exits, favorable exits, entries, post-entry adverse sweep) and `close_phase` (funding, close liquidation, option expiry and margin call, trailing ratchet, metrics, finish/bankruptcy), each split into small functions to keep per-function complexity ≤ 15.
- News items are scenario data published at their release step; their market reaction is baked into the generated bars, so there is no engine-side event release phase.
- Event-sequence numbers are global per run; events are appended to `RunState::events`.

### D6. Liquidation & margin formulas (execution-policy v1)
- Isolated long liquidation price: `L = (q·m·E − M) / (q·m·(1 − mmr))`; short: `L = (q·m·E + M) / (q·m·(1 + mmr))` where `q` quantity, `m` multiplier, `E` entry, `M` isolated margin (after funding), `mmr` maintenance rate. Rounded toward the side that liquidates earlier (conservative). The contract currently has a single `maintenance_margin` rate; notional tiers are pending (tasks 5.6).
- Liquidation forfeits full isolated margin (simplified vs. real exchanges that return residual; confirmed by product 2026-11-02). Bankruptcy check after each step: no positions/orders and available < IM + fees of min quantity at mission cap → `account_bankrupt`, run ends (game over).
- Option settlement target: Deribit-style TWAP over a declared window (default 30 min); mean of closes / expiry close fallback flagged approximate. Scenarios built for options chapters should include 1-minute sub-bars around expiries so settlement is exact (sub-bars not implemented yet, task 6.6; see D6c for what v3 does).
- Short-option margin (default, scenario-overridable): IM = `max(0.15·F − OTM, 0.10·F)·m·q + ask·m·q`; MM = `0.075·F·m·q + mark·m·q`.

### D6b. Brackets, OCO and trailing (execution-policy v2)
- An entry may carry `attach_stop`/`attach_tp`. Each fill that grows exposure creates or grows reduce-only children (stop-market, take-profit) for exactly the increased quantity, sharing an OCO group = parent id. A fill on one child reduces its siblings by the same quantity; flat position cancels all reduce-only orders.
- Children can be created mid-bar, so `RunState::orders` is a `std::deque` (stable references) and matching iterates by index.
- An entry filled intrabar gets its children immediately; the post-entry adverse sweep may hit the child stop, but a same-bar take-profit is never credited.
- Trailing stop-market: trigger initialized from the decision-step close ± distance, ratcheted at each close (never loosened), effective from the next bar. Percentage trails are a later extension.
- Policy v2 changed only the state serialization (new order fields); v1 event streams are unchanged.

### D6c. Options (execution-policy v3)
- Pricing: Black-76, zero rate, forward = perp close (display) or next open (execution). T from remaining steps × `minutes_per_step`. IV = base × term(T) + skew·ln(K/F) + smile·ln(K/F)², floor 5%. Base = scenario `iv_base[t]` (published at the close of t; the generator derives it from its volatility state + premium + per-segment event multiplier) or, when absent, annualized realized vol of the last 48 closes ≤ t (flag `ivFromRealized`).
- Quotes: mid quantized half-even to the premium tick; half-spread = max(spread × mid, 1 tick); bid floored (may be 0 → no bid), ask ceiled (≥ 1 tick). Greeks quantized (delta/gamma Rate, theta per step and vega per vol point in Price).
- Execution: option orders (market/limit) execute only at the next bar's open against a quote with F = open and IV published at the previous close — never intrabar, because the premium path inside a bar is unobservable. No capacity limit for options in v3 (documented approximation).
- Fees: `fee_rate` × underlying notional, capped at 12.5% of premium notional (venue-style).
- Margin: buyers pay premium (no margin). Writers: IM = max(im_base·F − OTM, im_floor·F) + ask; MM = mm_base·F + mid, per unit. Account option equity = wallet − isolated perp margin + longs at bid − shorts at ask. Below total MM after a step → `margin_call` event, buy back the short with the largest MM at the ask until satisfied; any shortfall is absorbed by the simulated insurance fund (wallet never negative).
- Settlement at the expiry close: TWAP of the last `settlement_bars` closes; with 1 bar it is the close and flagged approximate (sub-bar data not yet in scenarios).
- Reconciliation: wallet = initial + perp realized − fees + funding + option_cash (premiums and settlements).
- Policy v3 adds the instrument (series) field to order/event serialization.

### D6d. Plans, calibration answers and scoring (execution-policy v4)
- `RecordPlan` stores the structured plan (side, invalidation, target, confidence, planned risk, skipped, option, has-lose-scenario) at the decision step; free text never reaches the engine (spec: no subjective grading).
- `AnswerQuestion` is accepted only when a question is due: every 15 steps from the first decision step, while the resolving bar (t+5) exists, once per step. Threshold = close at t; skipped answers are kept but not scored.
- `engine/scoring` (version 1) derives entries from a position walk over perp fills, protection from reduce-only stops placed in [t-1, t+2] (bracket children included), and computes risk/plan/calibration with renormalization, findings, next lesson and flat-to-flat trades with R = net / planned risk. Integer arithmetic only, so client and server agree exactly.

### D6e. Generator v2 (execution-policy v5)
- Missions' bars come from the seeded market generator (`generator.hpp`, stream `Market`), which also emits the funding series and the `iv_base` series. Generator v2 changed the generated bars, so the policy version was bumped to 5; execution rules are unchanged from v4.

### D7. Digest and checkpoints
- Canonical little-endian binary serialization of `RunState`; SHA-256 (vendored, constant-time not required). State digest covers the policy version, seed, step, status, leverage, balances, position, orders, option positions, plans, answers, metrics and RNG state; the events digest covers the full event log.
- Checkpoint = header (magic, checkpoint format 1, execution-policy version, scenario id) + serialized state. Loading fails with `IncompatiblePolicy`, `ScenarioMismatch`, `TruncatedInput` or `NonCanonicalEncoding`. The engine semver is not stored (reported by `tl_version`).

### D8. Golden vectors
- Goldens are SHA-256 digests frozen inside the doctest suite rather than a fixtures directory: kernel outputs (`test_det_math`), normal variates (`test_prng`), an option quote grid (OPT01, `test_options`) and scripted mission runs through the C ABI (`test_missions`, state + events digests). `bindings/python/tests` repeats a scripted run and checks the same digests. Update procedure: `engine/README.md`.
- CI runs them on: linux-x86_64 (clang 18), macos-arm64 (AppleClang), debug and release, wasm (node), python (ctypes). Swift added in `add-native-clients`.
- Property-style tests are seeded randomized loops (engine `Pcg32`, stream `Sandbox`) in doctest, not RapidCheck: reconciliation and non-negative balances (SIM08, with and without options), checkpoint resume. No-lookahead is checked by mutating future bars and comparing digests (SIM01, indicators, patterns, OPT04).

### D9. Bindings in this change
- WASM: Emscripten target `tradelab_wasm` (`-sMODULARIZE -sEXPORT_ES6 -sSINGLE_FILE`, exports the `tl_*` functions), built by `apps/web/scripts/build-engine.sh` into `apps/web/src/lib/engine/wasm/tradelab.mjs` and used from a Web Worker. TypeScript types are currently hand-written (`apps/web/src/lib/engine/types.ts`); generating them from a small IDL (`engine/idl/*.yaml`) is pending (tasks 3.1, 8.2).
- Python: pure-Python package `tradelab_engine` (`bindings/python/`) using ctypes over the C ABI in the shared library `libtradelab` (CMake target `tradelab_capi`), located via `TRADELAB_LIB`, the package directory or the engine build tree. Consumed by `services/api` (its image builds the library) for server re-simulation. ctypes was chosen over nanobind: the C ABI already exists for WASM, so no compiled extension or per-platform wheel is needed.

## Risks / Trade-offs

- **Kernel accuracy vs. determinism** → kernels target ~1e-12 relative error; premiums are quantized to tick so any residual difference would show up as a golden failure, not a silent drift.
- **Simplified liquidation (full margin forfeit)** → may overstate losses vs. some venues; disclosed in UI and debrief.
- **OHLC conservatism penalizes favorable exits** → flagged `ambiguousIntrabar`; sub-bar data replaces it when licensed.
- **WASM bundle size** → target < 400 KB gzipped (single-file module is ~66 KB gzipped today); not yet measured in CI (task 8.2).

## Open Questions

- Maintenance-margin tiers and default fees: need domain review (product-spec §20.5).
