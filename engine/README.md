# TradeLab engine

Deterministic C++20 simulation core shared by every client (web via WASM, desktop, iOS, server via Python). Normative behavior: `openspec/changes/add-deterministic-engine/`.

## Build and test

```bash
cmake --preset native-debug && cmake --build --preset native-debug && ctest --preset native-debug

# WASM (tests run under node); requires Emscripten (brew install emscripten, or emsdk with $EMSDK)
emcmake cmake -S . -B build/wasm-local -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-local && (cd build/wasm-local && ctest --output-on-failure)

# Lint: banned APIs + clang-tidy (cognitive complexity <= 15)
python3 ../tools/check_engine_bans.py src include
clang-tidy -p build/native-debug --quiet src/*.cpp   # macOS + Homebrew LLVM: add --extra-arg=-isysroot$(xcrun --show-sdk-path)
```

## Determinism rules

Results must be bit-identical on native x86-64/arm64, WASM, Python and Swift. Enforced by `tools/check_engine_bans.py` and by frozen digests in the tests.

- All state uses `Fixed<Tag, Decimals>` int64 types (`Price`, `Qty`, `Money` at 1e-8, `Rate` at 1e-10, `Vol` at 1e-8). Every multiply/divide names its `Rounding`. Overflow returns `ErrorCode::NumericOverflow`; it never wraps.
- No `std::exp/log/sin/cos/erf/pow` or other libm transcendentals: use `tradelab::det::*`, which uses only IEEE-754 correctly rounded operations. Quantize to fixed-point before touching state.
- No `<random>`: use `Pcg32` with an explicit `Stream`.
- No I/O, clocks, threads, locale or environment in `engine/`.
- Build flags: `-ffp-contract=off -fno-fast-math`. Never add `-ffast-math`, `-Ofast` or `-mfma` to engine targets.

## Golden digests

There is no separate fixtures directory: the goldens are SHA-256 digests frozen inside the tests.

- `tests/test_det_math.cpp`, `tests/test_prng.cpp`: kernel outputs and normal variates.
- `tests/test_options.cpp` (OPT01): a grid of option quotes and Greeks.
- `tests/test_missions.cpp`: scripted mission runs through the C ABI (state and event-stream digests).
- `tests/test_advisor.cpp`: a scripted m02 run with start options (advice allowance 3, fast wire 1, calendar) and two advisor requests.
- `bindings/python/tests/test_engine.py`: repeats the `m01-false-breakout` and the advisor scripted runs through the Python binding and checks the same digests.

CI runs them on every target (linux-x86_64, macos-arm64, wasm under node, Python). A digest failure means cross-platform results diverged. Do **not** update the digest to make CI green.

The only legitimate update is an intentional change to a kernel, the PRNG, the generator or execution rules. That change must:

1. bump the execution-policy version (old runs must keep replaying), and
2. be explained in the PR, regenerating the digest from a native build, confirming WASM matches it, and updating the Python mirror of any changed mission digest.

## Start options and the advisor (policy v11)

`start_run(scenario, seed, RunOptions{advice_allowance, news_lead, calendar})` (C ABI `tl_run_create_ex`) stores the options in the run state, the digest and checkpoints; the defaults are a run without advisor or perks. Policy v11 appended these fields to the state digest, so the frozen state digests changed through the version while every event digest stayed the same: a default run executes exactly as under v10.

`RequestAdvice` (`tl_request_advice`) stores `advise(scenario, state)` (`include/tradelab/advisor.hpp`) in `RunState.advice` and uses one unit of the allowance. It does not advance time or touch orders, positions or balances, and with no allowance left it is rejected with `NoAdviceLeft` before anything is recorded (the digest does not change). `advise()` reads only bars up to the step, published funding, patterns ending at the step and news released by then (or within `news_lead` bars). `test_advisor.cpp` checks this by changing every later bar, funding rate and headline. The debrief adds each read's outcome, the advisor's hit rate and `advisorPointsEarned` (scoring v4).
