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

`RequestAdvice` (`tl_request_advice`) stores `advise(scenario, state)` (`include/tradelab/advisor.hpp`) in `RunState.advice` and uses one unit of the allowance. It does not advance time or touch orders, positions or balances, and with no allowance left it is rejected with `NoAdviceLeft` before anything is recorded (the digest does not change). `advise()` reads only bars up to the step, published funding, patterns ending at the step and news released by then (or within `news_lead` bars). `test_advisor.cpp` checks this by changing every later bar, funding rate and headline. The debrief adds each read's outcome, the advisor's hit rate and `advisorPointsEarned` (scoring v6).
## Scenario packs and CSV import

Normative behavior: `openspec/changes/add-scenarios-and-gameplay/specs/scenario-packs/`. Everything here treats input as hostile; each check fails with a stable reason code (listed at the top of `src/pack.cpp` and `src/csv_import.cpp`) that clients turn into text.

| Piece | Where |
|---|---|
| Strict JSON reader + RFC 8785 canonical form | `src/json_in.{hpp,cpp}` |
| Ed25519 (Monocypher, vendored) | `include/tradelab/ed25519.hpp`, `src/ed25519.cpp` |
| zstd decoding with a size guard, raw-block frame writer | `src/zstd_frame.cpp` (vendored decoder) |
| ustar reader/writer | `src/tar.cpp` |
| Manifest schema v1 | `src/pack_manifest.{hpp,cpp}` |
| Validation and loading (`open_pack`) | `src/pack.cpp` |
| CSV import, masking, private pack builder | `src/csv_import.cpp`, `src/masking.cpp`, `src/pack_build.cpp` |
| C ABI | `tl_csv_validate`, `tl_pack_build_csv` (+ `tl_blob_data/size`), `tl_device_key`, `tl_pack_validate`, `tl_run_create_pack`, `tl_run_restore_pack` in `src/c_api_pack.cpp` |

**Container.** A `.tlpack` is an uncompressed ustar archive with flat file names: `manifest.json` (stored in canonical form), `manifest.sig` (64-byte Ed25519 signature over the canonical manifest), `bars.bin`, optional `funding.bin`, optional `reveal.json`. `events.bin`, `options.bin`, `mission.json` and `i18n.json` are reserved and refused as `unsupported_feature` until packs carry news and option series.

**Data files.** `bars.bin` is a zstd frame of a columnar table: a 16-byte header (`TLB1`, rows, 5 columns, 0), then little-endian int64 columns open, high, low, close (1e-8) and volume (1e-8). `funding.bin` is the same with magic `TLF1` and one rate column (1e-10). The manifest lists every file with its stored size, decompressed size and SHA-256, so the signature covers all data.

**Manifest v1 fields.** `schemaVersion`, `scenarioId`, `packVersion`, `mode` (historical, synthetic, branch), `private`, `engineCompatibility` {`min`, `max`}, `executionPolicyVersion`, `scoringVersion`, `timeframe`, `stepSeconds`, `displayPrecision`, `aliasPolicy` (`per-run`), `instruments.perpetual` {multiplier, tickSize, lotSize, minQty, maxLeverage}, `costModel` {makerFee, takerFee, halfSpread, slippage}, `liquidityModel` {capacityFraction}, `fundingModel` {intervalSteps, source: file or none}, `ivModel` {kind: realized or none}, `marginModel` {maintenanceMargin}, `missionRules` {initialBalance, leverageCap, warmupBars, allowShort}, `data` {bars, gaps}, `files`, `signingKeyId`, `provenance`, `license` {name, attribution, transformation, commercial, offlineRedistribution}, `distribution` {downloadable}. Decimals are strings; JSON numbers must be integers (no fractions or exponents, within 2^53).

**Order of checks** (`open_pack`): size limit, container, manifest JSON and schema (`missing_field` / `invalid_field` name the dotted path), signature against the caller's trusted keys, engine compatibility, each listed file's size and hash (`hash_mismatch`, DATA02), decompression into exactly the declared size (`decompression_limit` on bombs), column decoding, `validate(Scenario)`. With publish intent it also checks `private_pack` and the license record (`license_incomplete`, LIC01).

**Limits** (`PackLimits`, `kCsv*`): pack 16 MiB, 8 files, manifest 64 KiB, 8 MiB per decompressed file, 20,000 bars; CSV 4 MiB, 20,000 rows, 512-byte lines, at least 50 rows.

**CSV import.** A header row with `timestamp` (or time, date, datetime, open_time, ts), open, high, low, close, volume and an optional funding column, in any order; comma separated, dot decimals, at most 8 decimals (10 for funding), no exponents. Timestamps are integers in the declared unit (seconds or milliseconds; the wrong unit is `timestamp_unit`, DATA01, and is never rescaled) or UTC ISO dates. Rows must increase by a multiple of the declared interval; larger steps are reported as gaps and never filled. Empty funding cells keep the last rate.

**Private packs.** `build_csv_pack` masks prices so the open of the bar at the first decision point (`warmupBars`, a quarter of the rows clamped to 20–60) is 100, rounded half-even to the 0.01 tick, and scales volume by the inverse factor. It writes the data as raw-block zstd frames (valid zstd, with no encoder needed in any client), marks the pack `private` and signs it with a per-device key derived from a 32-byte seed. The reveal (asset, pair, venue, period) is only in `reveal.json` and reaches the client through `tl_debrief`. Pack runs leave the mission digests untouched; a scripted pack run is frozen as a golden in `tests/test_pack.cpp` and mirrored in `bindings/python/tests/test_packs.py`.

**Fuzzing** (libFuzzer, native clang; the CI job `fuzz` runs each target for 60 s with ASan and UBSan):

```bash
cmake -S . -B build/fuzz -G Ninja -DTRADELAB_BUILD_FUZZERS=ON -DTRADELAB_BUILD_TESTS=OFF \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18   # macOS: Homebrew llvm@18
cmake --build build/fuzz --target fuzz_csv fuzz_pack fuzz_json
mkdir -p /tmp/corpus && cp fuzz/corpus/csv/* /tmp/corpus/ && build/fuzz/fuzz_csv -max_total_time=60 /tmp/corpus
```

If the toolchain's ASan runtime does not start (seen with Homebrew LLVM 18 on recent macOS), configure with `-DTRADELAB_FUZZ_SANITIZERS=undefined`.

Vendored code and how to update it: `third_party/README.md`. `tools/packer/packer.py` wraps the same C ABI for authoring (`key`, `import-csv`, `validate`).
