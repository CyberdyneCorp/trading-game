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
- `tests/test_missions.cpp`: scripted mission runs through the C ABI (state and event-stream digests): m01 and m05 since phase 1, and one golden per phase-2 mission with default options (m06–m12), plus the SHA-256 of `tl_mission_reveal("m08-regime-switch", 7)` and of `tl_mission_reveal("m11-news-calendar", 7)`.
- `tests/test_mission_learnability.cpp` (m01–m08 and the buy-and-hold bound), `tests/test_mission_learnability_b.cpp` (m09–m12; both share the reference players in `tests/learnability.hpp`) and `tests/test_mission_realism.cpp` are not goldens: they assert bands over 60 seeds of every mission (design D8.3), so a generator or mission change that makes a lesson unlearnable or predictable fails them.
- `tests/test_advisor.cpp`: a scripted m02 run with start options (advice allowance 3, fast wire 1, calendar) and two advisor requests.
- `bindings/python/tests/test_engine.py`: repeats the `m01-false-breakout` and the advisor scripted runs through the Python binding and checks the same digests; `bindings/python/tests/test_missions_batch_a.py` repeats the m06, m07 and m08 goldens and the reveal digest; `test_missions_batch_b.py` the m09–m12 goldens, the m11 reveal digest and the `upcoming` list.

CI runs them on every target (linux-x86_64, macos-arm64, wasm under node, Python). A digest failure means cross-platform results diverged. Do **not** update the digest to make CI green.

The only legitimate update is an intentional change to a kernel, the PRNG, the generator or execution rules. That change must:

1. bump the execution-policy version (old runs must keep replaying), and
2. be explained in the PR, regenerating the digest from a native build, confirming WASM matches it, and updating the Python mirror of any changed mission digest.

A new mission is not such a change: it adds its own golden and leaves every existing one byte-identical (design D8.1). Each generator capability acts only when a mission declares it, so the phase-2 missions added no policy version.

## Phase-2 generator capabilities (generator v5, scoring v9)

**G1 Markov regime chain.** `GeneratorSpec.chain` (`RegimeChain`: segment templates, per-mille transition rows with a zero diagonal, initial weights, `min_dwell`/`max_dwell`, `bars`, insertion index `at`) is expanded per seed into ordinary segments by `expanded(spec, seed)` before `scheduled()` applies the per-seed lead, so the rest of the generator is unchanged. The path is drawn on `Stream::Regime` (6) with integer draws (`chain_path`); the last visit is cut so the chain fills exactly `bars`. `Segment.label` (`RegimeLabel`: range, trend, high_vol, low_liquidity, crowded, calm, shock) names a segment for the reveal and tests and never feeds generation. Missions without a chain generate exactly as before (`kGeneratorVersion` 3 is not a digest input). `tests/test_generator.cpp` checks transition and initial frequencies over 2,000 seeds (±3 points), the fixed length, the dwell range, that a spec without a chain expands to itself, and that two chains agreeing up to bar k give identical bars, IV and funding up to k.

**G4 seeded reveal.** `mission_reveal(id, seed)` regenerates a built-in mission and returns its regime spans as played (start, bars, label, drawn direction of random-direction segments) and each event's actual, surprise, intensity (100% until G2) and sampled reaction. C ABI `tl_mission_reveal(mission, seed)` (run-less, static buffer; `{"error":"unknown_mission"}` otherwise), WASM export, Python `mission_reveal`. It describes the run's future, so the web worker answers it only once the run has ended (`apps/web/src/lib/engine/reveal.test.ts`). The debrief of a built-in mission adds the report-only **signals by regime** table (`"regimes"`: per revealed span of the decision window, hits and samples of RSI(14) fade, Bollinger(20, 2) fade, MACD histogram follow and SMA20 follow, read at a close and judged 5 bars after the next open) and each trade's entry regime (`"regime"`). That is scoring v9; it is no score input and changes no digest.

**G2 drawn event outcomes and intensity** (generator v4). `ScriptedEvent.actual_weights` (per mille for the actual −2..+2; all zero = the declared actual) and `intensity_min`/`intensity_max` (default 1, 1) are drawn per seed and event index on `Stream::Outcome` (7), separate from the reaction stream, by `draw_outcome` (the intensity in whole percent). `played(spec, seed)` = chain expanded, schedule applied, then each declaring event gets its drawn actual written into the headline (tone = the sign of the surprise against the consensus) and its intensity applied to `fair_impact` and the reaction `drift`. Events that declare neither generate exactly as before, so every earlier golden is byte-identical. `tests/test_events.cpp`: frequencies over 2,000 seeds within ±3 points, intensity range, undeclared events unchanged, a drawn surprise of 0 moves no fair value, a larger intensity gives a larger gap.

**G3 announced events.** `NewsItem.announce_lead` (bars, default 0). From the close of `release_step − announce_lead` until the item is visible, `tl_state` lists it in the top-level `"upcoming"` array with its `step`, `kind` and `expectation` (the consensus) only; tone, actual and credibility stay hidden. It is visibility only: bars never depend on it. A fast wire that shows the headline early hands it from `upcoming` to `news` (flagged `early`). The advisor adds `EventAhead` (`event_ahead`, advisor v2): an announced release within the next 3 bars votes 0 and pulls the sum one step toward wait. `NewsKind::MacroData` = 8 and `NetworkUpgrade` = 9 are appended. `tests/test_events.cpp` checks that changing an announced item's actual, tone or credibility changes no state view before its release (through the C ABI's state JSON), the exact first bar it is listed, identical bars with and without the lead, and the advisor reason's window; `tests/test_intel.cpp` checks perks against every mission, m11 included.

**Reveal events** add the consensus, credibility, `announceLead`, `gapBp` (open after the release against its close), `moveBp` (close 3 bars after against the close before) and `impliedBp` (the ATM straddle of the first expiry still live 3 bars after the release, quoted the bar before, as basis points of the price; −1 without options), and report the drawn intensity.

**Event channels** (generator v5). A `ScriptedEvent` may also declare a jump, funding, spread and capacity channel, active on its reaction bars (release + 1 .. release + duration). Defaults leave the market exactly as before, and no built-in mission declares one (m06's thin book stays a mission-level cost override with segment volume, as design D8.2 describes it: the market maker's quotes thin, spread and slippage stay the same), so every golden is unchanged.
- *Jump*: each active bar jumps with `jump_prob` by `jump_size × (0.5 + u)` (log) along the sampled reaction (along the surprise when the reaction is none), drawn on `Stream::EventJump` (8) per (seed, event, bar). An event jump is liquidity, not information: it does not move the fair value.
- *Funding*: `funding_shift` is added to the rate of each active bar, so the first shifted rate is the one published at the release close, never earlier.
- *Spread and capacity*: `spread_mult` and `capacity_mult` become a `LiquidityShock` {start, bars, spread_pct, capacity_pct} in `Scenario::liquidity`. Execution reads `costs_at(scenario, bar)` for the bar it fills on: half-spread and slippage × spread_pct / 100 (rounded up, capped at 50%), capacity fraction × capacity_pct / 100 (rounded down). They never move prices. `validate` refuses shocks outside the market, on bar 0, or with a spread below 10% or above 1,000% of the model (`InvalidLiquidity`).

`tests/test_event_channels.cpp` checks that undeclared channels change nothing, that spread and capacity leave bars and funding identical while the shock covers exactly the reaction bars, the funding shift's first and last published rate, that jumps follow the sampled reaction only inside the window (400 seeds), and fills at the shocked spread and depth.

**Mission data.** `MissionDef` may override the cost model and the contract (m06's thin book: half-spread 0.10%, slippage 0.20%, capacity 4% of bar volume, base volume 1,500) and declares its own short permission (m06 is long only, 2× cap). `NewsKind::LiquidityOutflow` = 7 is appended (existing kinds keep their numbers; `validate` refuses kinds above it).

## Start options and the advisor (policy v11, v12)

`start_run(scenario, seed, RunOptions{advice_allowance, news_lead, calendar, starting_balance, leverage_cap})` (C ABI `tl_run_create_ex`) stores the options in the run state, the digest and checkpoints; the defaults are a run with the mission's own rules, without advisor or perks. Policy v11 appended the first three fields to the state digest, so the frozen state digests changed through the version while every event digest stayed the same: a default run executes exactly as under v10.

Policy v12 adds the campaign options (checkpoint format 3): `starting_balance` (> 0) replaces the mission's initial balance, so a campaign mission starts from the capital the previous one ended with, and `leverage_cap` (> 0) lowers the mission's cap where the curriculum keeps leverage above 2× locked. `run_initial_balance(sc, st)` and `run_leverage_cap(sc, st)` (`include/tradelab/engine.hpp`) are the run's own rules: the wallet, `SetLeverage`, the bankruptcy check, the state's `initialBalance` and `contract.leverageCap`, the score's net P&L and its 2%/20% risk limits, and the buy-and-hold benchmark all use them. In the C ABI the balance is a decimal string (NULL or "" = the mission's; "0", negatives or garbage make `tl_run_create_ex` return NULL). The two fields are appended to the state digest, so the state goldens changed again with v12 and every event digest stayed the same. `tests/test_campaign.cpp` covers both options, the checkpoint round trip and the refused values.

`RequestAdvice` (`tl_request_advice`) stores `advise(scenario, state)` (`include/tradelab/advisor.hpp`) in `RunState.advice` and uses one unit of the allowance. It does not advance time or touch orders, positions or balances, and with no allowance left it is rejected with `NoAdviceLeft` before anything is recorded (the digest does not change). `advise()` reads only bars up to the step, published funding, patterns ending at the step and news released by then (or within `news_lead` bars). `test_advisor.cpp` checks this by changing every later bar, funding rate and headline. The debrief adds each read's outcome, the advisor's hit rate and `advisorPointsEarned` (scoring v6). Scoring v7 matches each plan to the entry order it was recorded for (same decision step, kind and side; an add adds its planned risk) and reports `writtenOff`, the option loss the simulated fund covered after the wallet was floored at zero.
Policy v13 adds `MoveOrder` (`tl_move_order(run, client_id, order_id, price)`, Python `Run.move_order`, replay action `{"k":"move","id","price"}`): a working reduce-only stop or take-profit moves to a new trigger in place, keeping its id, quantity, bracket parent and OCO group, with an `order_moved` order update. Only the policy number in the state digest changed the frozen state digests; every event digest stayed the same. `tests/test_brackets.cpp` covers it. Scoring v8 (report-only, no digest change) attributes option P&L by full revaluation (`engine/src/debrief.cpp`), picks the IV finding by side (`iv_rise` for written options), adds `wrote_options`, and measures buy-and-hold to the run's last bar. The state JSON names the bar interval (`stepSeconds`: 3600 for the missions, the pack's for an imported market).

Policy v14: a reduce-only exit larger than the position it closes (a bracket leg left at its size after a partial close) cancels its remainder (`position_closed`) on the same bar, right after its own fill update; before, it stayed `partially_filled` without a position until the next bar. Only the policy number in the state digest changed the frozen state digests; every event digest stayed the same. `tests/test_brackets.cpp` covers it.

Policy v15: a triggered stop is a market order. A reduce-only stop or trailing stop cut short by bar capacity executes its remainder at the next opens (with the market orders, from the open plus costs; it keeps its trigger as the requested price), where before it rested at its trigger again and could stay open while the market traded above it, then fill at the trigger. Any other stop remainder (an entry stop) is cancelled with `insufficient_capacity`, like a market order's. A stop with no capacity left at its trigger still rests (no fill yet). Only the policy number in the state digest changed the frozen state digests; every event digest stayed the same. `tests/test_brackets.cpp` covers it. Scoring v10 (report-only) names a trade's exit by the order that closed it: `stop`, `trailing` (new), `target` (take-profit or limit), `manual` (market), so a stop that exits in profit is no longer a target (`tests/test_debrief.cpp`).

### Intel perks in the state view

`news_lead` and `calendar` only change what `tl_state` serializes, never the market or the digests (no policy bump): a news item is listed once `release_step <= step + news_lead`, with `"early": true` until its release step, and with `calendar` the top-level `"calendar"` array holds the release step of each rate decision not visible yet (step only; empty without the perk). Bars are still serialized up to `step` only. `tests/test_intel.cpp` checks the early flag (lead 1 and 2), the calendar hand-over to an early headline, and that bars and event digests are identical with and without perks in every mission.

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

**Container.** A `.tlpack` is an uncompressed ustar archive with flat file names: `manifest.json` (stored in canonical form), `manifest.sig` (64-byte Ed25519 signature over the canonical manifest), `bars.bin`, optional `funding.bin`, `events.bin`, `options.bin` and `reveal.json`. `mission.json` and `i18n.json` are reserved and refused as `unsupported_feature` (mission texts stay in the client catalogs).

**Data files.** Each `.bin` is one zstd frame holding one or more columnar tables; a table is a 16-byte header (4-byte magic, rows, columns, 0; little-endian u32) followed by each column in turn as little-endian int64. A file must hold exactly its tables, with the row counts the manifest declares.

| File | Tables (columns) |
|---|---|
| `bars.bin` | `TLB1`: open, high, low, close (1e-8), volume (1e-8) |
| `funding.bin` | `TLF1`: rate (1e-10) |
| `events.bin` | `TLE1` (rows `data.events`): release_step, kind, tone (−1..1), credibility (0..100), expectation (−2..2), actual (−2..2), announce_lead; then `TLQ1` (rows `data.liquidityShocks`, may be 0): start, bars, spread_pct, capacity_pct |
| `options.bin` | `TLO1` (rows `data.optionSeries`): id, kind (0 call, 1 put), strike (1e-8), list_step, expiry_step; then `TLI1`: implied-vol base (1e-8, 0..1,000%), one row per bar with `ivModel.kind` = `file`, else 0 rows |

Each column is range-checked before narrowing (`malformed_data`); semantic checks (steps inside the market, kinds, ids 1..n, liquidity shocks) are `validate(Scenario)`'s (`invalid_scenario`). The manifest lists every file with its stored size, decompressed size and SHA-256, so the signature covers all data.

**Manifest v1 fields.** `schemaVersion`, `scenarioId`, `packVersion`, `mode` (historical, synthetic, branch), `private`, `engineCompatibility` {`min`, `max`}, `executionPolicyVersion`, `scoringVersion`, `timeframe`, `stepSeconds`, `displayPrecision`, `aliasPolicy` (`per-run`), `instruments.perpetual` {multiplier, tickSize, lotSize, minQty, maxLeverage}, optional `instruments.options` {multiplier, premiumTick, lotSize, feeRate, spread, skew, smile, termSlope, minutesPerStep, settlementBars, imBase, imFloor, mmBase, allowShort} (requires `options.bin`), `costModel` {makerFee, takerFee, halfSpread, slippage}, `liquidityModel` {capacityFraction}, `fundingModel` {intervalSteps, source: file or none}, `ivModel` {kind: realized, none or file}, `marginModel` {maintenanceMargin}, `missionRules` {initialBalance, leverageCap, warmupBars, allowShort}, `data` {bars, gaps, optional events, liquidityShocks, optionSeries (default 0), branchStep (required in branch mode, refused otherwise)}, `files`, `signingKeyId`, `provenance`, `license` {name, attribution, transformation, commercial, offlineRedistribution}, `distribution` {downloadable}. Decimals are strings; JSON numbers must be integers (no fractions or exponents, within 2^53). Perpetual-only packs without news write exactly the fields of the first version, so existing packs keep their bytes.

**Order of checks** (`open_pack`): size limit, container, manifest JSON and schema (`missing_field` / `invalid_field` name the dotted path), signature against the caller's trusted keys, engine compatibility, each listed file's size and hash (`hash_mismatch`, DATA02), row limits (`too_many_bars`, `too_many_events`, `too_many_series`), decompression into exactly the declared size (`decompression_limit` on bombs), table decoding, bar labels, `validate(Scenario)`. With publish intent it also checks `private_pack` and the license record (`license_incomplete`, LIC01).

**Limits** (`PackLimits`, `kCsv*`): pack 16 MiB, 8 files, manifest 64 KiB, 8 MiB per decompressed file, 20,000 bars, 1,000 news rows and 1,000 liquidity shocks, 2,000 option series; CSV 4 MiB, 20,000 rows, 512-byte lines, at least 50 rows.

**Synthetic and branch packs.** Bars of a `synthetic` pack are flagged synthetic (as a built-in mission's are). A `branch` pack (spec market-generator "Branch labeling") is a recorded window up to and including `data.branchStep`, followed by generated bars: every bar after the branch point is flagged synthetic, the ones before it are not. The state view carries the flag as the 6th element of each bar, and the web client shows a "synthetic branch" tag from the first generated bar on. `splice_branch` (`src/masking.cpp`) builds one: the alternative market's bars from branch_step + 1 are rescaled (half-even to the tick, volume by the inverse factor) to continue from the recorded close; funding, news released after the branch (announced no earlier than the bar after it) and liquidity shocks starting after it come from the alternative.

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

Vendored code and how to update it: `third_party/README.md`.

### Pack authoring

`tools/packer/packer.py` (usage in `tools/packer/README.md`) authors packs through the same C ABI, so it reports exactly what clients report. The engine side (`src/pack_build.cpp`, native only; not exported to WASM):

| C ABI | Engine | What |
|---|---|---|
| `tl_pack_source_mission(mission, seed)` | `mission_pack_source` | a built-in mission generated for a seed as a synthetic pack source (manifest + raw data files, as an archive in the blob) |
| `tl_pack_source_csv(csv, meta)` | `csv_pack_source` | a masked CSV window as a publishable historical source; `branchStep`, `branchMission`, `branchSeed` make it a branch source |
| `tl_pack_write(manifest, files_tar, size, seed, prefix)` | `assemble_pack` | signs and writes a pack from stored files: each `.bin` a zstd frame that declares its content size (the decompressed size is read from the frame header) |
| `tl_pack_resign(pack, size, seed, prefix)` | `resign_pack` | signs a built, non-private pack again (a publisher key); private packs are refused (`private_pack`) |
| `tl_signing_key(seed, prefix)` | | the `<prefix>-<16 hex>` id and public key of a seed |

The engine only decodes zstd; the packer compresses with the `zstandard` package (level 19, content size and checksum in every frame). A pack generated from a mission replays the mission run digest for digest (`tests/test_pack_tables.cpp`, `bindings/python/tests/test_packer.py`); every mission round-trips through `events.bin` and `options.bin`. The web e2e fixture `apps/web/e2e/fixtures/branch.tlpack` is a branch pack built by the packer and signed with the device key of the seed in `e2e/branch-pack.spec.ts`; rebuild it with `packer.py mask` (the 150-row CSV of `make_csv` in `bindings/python/tests/test_packer.py`, `--warmup 40 --branch-at 90 --branch-mission m08-regime-switch --branch-seed 4`) and `packer.py build --prefix device` with that seed. `e2e/fixtures/synthetic-m09.tlpack` is `packer.py generate m09-event-straddle --seed 7` built the same way (events and options tables in zstd frames).

### Calibration report

`tools/calibration_report.py` generates every built-in mission for N seeds and writes the generator's calibration report (return distribution, autocorrelation of returns and of absolute returns, drawdowns, against the stylized facts and an optional authorized reference series). The report for each generator version lives in `docs/calibration/generator-v<N>.md` (v5: 12 missions × 100 seeds); `packer.py build --publish` refuses synthetic or branch bars whose generator version has none (spec "Calibration report").
