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
- Indicators: SMA, EMA, RSI, ATR, session VWAP, Bollinger bands, MACD, ADX with +DI/−DI and volume — with explicit warm-up — plus context-aware candlestick pattern detection.
- State digest, checkpoints and golden digests (frozen in the tests) that run on every target in CI.
- Stable C ABI (JSON out, decimal strings in) with bindings: WASM (Emscripten ES module) and Python (ctypes over the C ABI) in this change; Swift and JNI arrive with their clients.

## Capabilities

### New Capabilities
- `simulation-core`: clock, step sequence, determinism, numerics, RNG, digest, checkpoints, command log.
- `order-execution`: order types, lifecycle states, intrabar policy, costs, capacity, OCO and reduce-only rules.
- `perpetual-futures`: contract model, margin, leverage, liquidation, funding, P&L.
- `options`: option series, pricing, IV model, Greeks, settlement, short-option margin.
- `indicators`: indicator and candle-pattern definitions, warm-up rules.

### Modified Capabilities
None (greenfield).

## Non-goals

- Spot trading (removed from the product).
- Cross margin, hedge mode, portfolio margin, American options, multi-asset portfolios.
- Tick-level or order-book simulation; market impact of the player on prices.
- Stop-limit orders (post-MVP).
- Any network, persistence or UI code inside the engine.

## Impact

- New: `engine/` (library, C ABI, WASM target `tradelab_wasm`, shared library `tradelab_capi`), `bindings/python/`, `apps/web/scripts/build-engine.sh`, `tools/check_engine_bans.py`.
- Toolchain: CMake ≥ 3.28, Ninja, Clang 18 (Linux CI) / AppleClang (macOS), Emscripten pinned in CI (6.0.13), Python ≥ 3.12 (ctypes, no compiled extension), doctest (fetched, tests only).
- CI (`.github/workflows/engine.yml`): linux-x86_64 and macos-arm64 (debug + release), wasm (tests under node), python, lint (banned APIs + clang-tidy) and openspec, all checking the same golden digests.
- Engine version is semver, reported with a separate execution-policy version (currently 5) by `tl_version`; checkpoints record the policy version. Scenarios are currently built inside the engine from mission ids and a seed (no external scenario manifests yet).
- Consumers: `add-web-client`, `add-cyberdyne-identity-and-backend` (server replay), `add-native-clients`.
