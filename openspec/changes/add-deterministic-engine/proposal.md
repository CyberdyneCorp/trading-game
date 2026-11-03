# Proposal: add-deterministic-engine

## Why

Every client (web, desktop, iPad, later Android) and the server must produce exactly the same fills, margin, funding, liquidations and option prices for the same scenario and commands. Without one deterministic core, results drift between online and offline play, server validation is impossible, and every bug gets fixed three times. Teaching leverage and options also requires precise, explainable accounting before any UI is built.

## What Changes

- New C++20 library `engine/` — pure, deterministic, no I/O, wall clock, threads or locale.
- Bar-close simulation clock with strict no-lookahead semantics and replayable command logs.
- Fixed-point int64 numerics, own PRNG and own math kernels (`exp`, `log`, `sqrt`-based normal CDF) so results are bit-identical on native x86-64/arm64, WASM and the Python binding.
- Order execution: market, limit, stop-market, take-profit limit, trailing stop-market, bracket/OCO, reduce-only; costs, slippage, shared capacity; conservative OHLC intrabar policy.
- Linear perpetual futures: one-way positions, isolated margin, leverage caps, maintenance margin tiers, liquidation, funding.
- European cash-settled options on the perp price: Black-76 pricing, scenario IV model, Greeks, buying and writing, account-level margin for short options, expiry settlement.
- MVP indicators: SMA, EMA, RSI, ATR, volume — with explicit warm-up.
- State digest, checkpoints and golden-vector harness that runs on every binding in CI.
- Bindings: WASM (Emscripten) and Python (nanobind) in this change; Swift and JNI arrive with their clients.

## Capabilities

### New Capabilities
- `simulation-core`: clock, step sequence, determinism, numerics, RNG, digest, checkpoints, command log.
- `order-execution`: order types, lifecycle states, intrabar policy, costs, capacity, OCO and reduce-only rules.
- `perpetual-futures`: contract model, margin, leverage, liquidation, funding, P&L.
- `options`: option series, pricing, IV model, Greeks, settlement, short-option margin.
- `indicators`: MVP indicator definitions and warm-up rules.

### Modified Capabilities
None (greenfield).

## Non-goals

- Spot trading (removed from the product).
- Cross margin, hedge mode, portfolio margin, American options, multi-asset portfolios.
- Tick-level or order-book simulation; market impact of the player on prices.
- Stop-limit orders (post-MVP).
- Any network, persistence or UI code inside the engine.

## Impact

- New: `engine/`, `bindings/wasm/`, `bindings/python/`, `fixtures/golden/`.
- Toolchain: CMake ≥ 3.28, Clang ≥ 17 / AppleClang 15, Emscripten 3.1.x pinned, Python 3.12 + nanobind.
- CI: matrix build (linux-x86_64, macos-arm64, wasm, python) comparing golden digests.
- Engine version is semver; scenario manifests declare compatible engine and execution-policy versions.
- Consumers: `add-web-client`, `add-cyberdyne-identity-and-backend` (server replay), `add-native-clients`.
