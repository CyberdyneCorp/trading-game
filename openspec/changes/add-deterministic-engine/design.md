# Design: add-deterministic-engine

## Context

Four clients (web, desktop, iPad, Android later) and one server must agree bit-for-bit on every fill, margin figure, funding payment, liquidation and option premium. The desktop client is C++20, so a C++20 core removes a language from the stack (v0.1 proposed Rust). Option pricing needs transcendental functions, which are not bit-identical across `libm` implementations (glibc, Apple libm, musl/Emscripten), so determinism must be designed, not assumed.

## Goals / Non-Goals

**Goals:** one engine, identical results on native/WASM/Python/Swift; explainable accounting; cheap to test with golden vectors; small, embeddable, no dependencies at runtime.

**Non-Goals:** realism beyond what teaching needs (no order book, no market impact); performance beyond ~1 ms per step on mid-range phones.

## Decisions

### D1. C++20, header-light static library, C ABI facade
- `engine/include/tradelab/*.hpp` (C++ API) plus `engine/include/tradelab/c_api.h` (stable C ABI: opaque handles, JSON/flatbuffer-free binary command and event encoding).
- C ABI is what WASM, JNI and Python bind to; Swift uses C++ interop directly against the C++ API but goldens run through both.
- No exceptions across the ABI: `Result<T, EngineError>` internally, error codes externally. Built with `-fno-exceptions -fno-rtti` for WASM size.
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
- PCG32 (or xoshiro256**) with explicit stream ids per purpose (`market`, `events`, `slippage`). Normal variates via engine Box-Muller using `det::log`/`det::sqrt`/`det::cos` kernels. Never `std::*_distribution`.

### D5. Data model and step loop
- `Scenario` (immutable, validated), `RunState` (positions, orders, balances, rng, step), `Command`, `Event`.
- `Engine::advance(RunState&, span<Command>) -> EventBatch` implements the fixed step sequence from the spec. Each phase is a separate function (`apply_commands`, `release_events`, `open_and_gaps`, `liquidations_and_triggers`, `fills`, `funding`, `expiries`, `mark_and_margin`, `close_bar`) to keep per-function complexity ≤ 15.
- Event-sequence numbers are global per run.

### D6. Liquidation & margin formulas (execution-policy v1)
- Isolated long liquidation price: `L = (q·m·E − M) / (q·m·(1 − mmr))`; short: `L = (q·m·E + M) / (q·m·(1 + mmr))` where `q` quantity, `m` multiplier, `E` entry, `M` isolated margin (after funding), `mmr` tier rate. Rounded toward the side that liquidates earlier (conservative).
- Liquidation forfeits full isolated margin (simplified vs. real exchanges that return residual; confirmed by product 2026-11-02). Bankruptcy check after each step: no positions/orders and available < IM + fees of min quantity at mission cap → `account_bankrupt`, run ends (game over).
- Option settlement: Deribit-style TWAP over a declared window (default 30 min); mean of closes / expiry close fallback flagged `settlementApproximated`. Scenarios built for options chapters should include 1-minute sub-bars around expiries so settlement is exact.
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

### D7. Digest and checkpoints
- Canonical little-endian binary serialization of `RunState`; SHA-256 (vendored, constant-time not required). Checkpoint = serialized state + engine version + policy version.

### D8. Golden vectors
- `fixtures/golden/<name>/{scenario.bin,commands.bin,expected.events.sha256,expected.state.sha256}` plus a human-readable `.md` describing intent.
- Generated by a native tool, reviewed by a human, then frozen. CI replays them on: linux-x86_64 (clang), macos-arm64 (AppleClang), wasm (node), python (nanobind). Swift added in `add-native-clients`.
- Property tests (RapidCheck): reconciliation invariant, non-negative balances, reduce-only never flips, no-lookahead (mutate future, compare prefix).

### D9. Bindings in this change
- WASM: Emscripten, `-sMODULARIZE -sEXPORT_ES6`, used from a Web Worker; TypeScript types generated from a small IDL (`engine/idl/*.yaml`) to avoid hand-maintained duplicates.
- Python: nanobind wheel `tradelab_engine`, consumed by `services/api` for server re-simulation.

## Risks / Trade-offs

- **Kernel accuracy vs. determinism** → kernels target ~1e-12 relative error; premiums are quantized to tick so any residual difference would show up as a golden failure, not a silent drift.
- **Simplified liquidation (full margin forfeit)** → may overstate losses vs. some venues; disclosed in UI and debrief.
- **OHLC conservatism penalizes favorable exits** → flagged `ambiguousIntrabar`; sub-bar data replaces it when licensed.
- **WASM bundle size** → target < 400 KB gzipped; measured in CI.

## Open Questions

- Maintenance-margin tiers and default fees: need domain review (product-spec §20.5).
