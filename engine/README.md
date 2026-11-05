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

## Interface definition (IDL)

`engine/idl/tradelab.idl` declares the enumerations shared by state, commands, events and the debrief (numeric value and JSON name) and every JSON shape the C ABI returns. `python3 engine/idl/gen.py` writes three files that are never edited by hand:

- `include/tradelab/idl_types.hpp`: the C++ `enum class`es and `json_name()`, which the C ABI writers use;
- `tests/idl_schema.hpp`: the shapes as JSON, checked by `tests/test_idl_conformance.cpp` against real output (every mission played with every command kind, a CSV import, a pack run, the error paths): exact keys, value types, declared enum names, decimal strings;
- `apps/web/src/lib/engine/types.ts`: the web client's types.

Adding a field or an enum value: edit the IDL (append enum values, never renumber them: they are part of the recorded-run format), run the generator, then make the C ABI emit it; the conformance test fails until both agree. `gen.py --check` runs in CI (engine lint job and web job) and fails on a stale generated file. The engine's internal structs (`RunState`, `Command`, `Scenario`) stay hand-written (design D10).

## Indicator reference values

`tests/test_indicator_reference.cpp` checks every indicator against TA-Lib's 252-bar daily regression series with TA-Lib 0.8.3's outputs for the game's parameters, vendored in `tests/data/talib_reference.inc` with TA-Lib's BSD-3-Clause license (`tests/data/TA-LIB-LICENSE.txt`). SMA, EMA, RSI, ATR, VWAP and Bollinger share TA-Lib's definitions: the warm-up equals TA-Lib's lookback and values agree within 1e-6. MACD and +DI/−DI/ADX seed their smoothing differently from TA-Lib (each EMA at its own period; Wilder sums seeded with the full period), so they agree within 1e-6 (MACD) and 1e-3 points (DI/ADX) from bar 150 on, with the same warm-up. Regenerate the table with `uv run --python 3.12 --with TA-Lib==0.8.3 --with numpy python tools/indicator_reference.py`.

## Phase-2 generator capabilities (generator v4, scoring v9)

**G1 Markov regime chain.** `GeneratorSpec.chain` (`RegimeChain`: segment templates, per-mille transition rows with a zero diagonal, initial weights, `min_dwell`/`max_dwell`, `bars`, insertion index `at`) is expanded per seed into ordinary segments by `expanded(spec, seed)` before `scheduled()` applies the per-seed lead, so the rest of the generator is unchanged. The path is drawn on `Stream::Regime` (6) with integer draws (`chain_path`); the last visit is cut so the chain fills exactly `bars`. `Segment.label` (`RegimeLabel`: range, trend, high_vol, low_liquidity, crowded, calm, shock) names a segment for the reveal and tests and never feeds generation. Missions without a chain generate exactly as before (`kGeneratorVersion` 3 is not a digest input). `tests/test_generator.cpp` checks transition and initial frequencies over 2,000 seeds (±3 points), the fixed length, the dwell range, that a spec without a chain expands to itself, and that two chains agreeing up to bar k give identical bars, IV and funding up to k.

**G4 seeded reveal.** `mission_reveal(id, seed)` regenerates a built-in mission and returns its regime spans as played (start, bars, label, drawn direction of random-direction segments) and each event's actual, surprise, intensity (100% until G2) and sampled reaction. C ABI `tl_mission_reveal(mission, seed)` (run-less, static buffer; `{"error":"unknown_mission"}` otherwise), WASM export, Python `mission_reveal`. It describes the run's future, so the web worker answers it only once the run has ended (`apps/web/src/lib/engine/reveal.test.ts`). The debrief of a built-in mission adds the report-only **signals by regime** table (`"regimes"`: per revealed span of the decision window, hits and samples of RSI(14) fade, Bollinger(20, 2) fade, MACD histogram follow and SMA20 follow, read at a close and judged 5 bars after the next open) and each trade's entry regime (`"regime"`). That is scoring v9; it is no score input and changes no digest.

**G2 drawn event outcomes and intensity** (generator v4). `ScriptedEvent.actual_weights` (per mille for the actual −2..+2; all zero = the declared actual) and `intensity_min`/`intensity_max` (default 1, 1) are drawn per seed and event index on `Stream::Outcome` (7), separate from the reaction stream, by `draw_outcome` (the intensity in whole percent). `played(spec, seed)` = chain expanded, schedule applied, then each declaring event gets its drawn actual written into the headline (tone = the sign of the surprise against the consensus) and its intensity applied to `fair_impact` and the reaction `drift`. Events that declare neither generate exactly as before, so every earlier golden is byte-identical. `tests/test_events.cpp`: frequencies over 2,000 seeds within ±3 points, intensity range, undeclared events unchanged, a drawn surprise of 0 moves no fair value, a larger intensity gives a larger gap.

**G3 announced events.** `NewsItem.announce_lead` (bars, default 0). From the close of `release_step − announce_lead` until the item is visible, `tl_state` lists it in the top-level `"upcoming"` array with its `step`, `kind` and `expectation` (the consensus) only; tone, actual and credibility stay hidden. It is visibility only: bars never depend on it. A fast wire that shows the headline early hands it from `upcoming` to `news` (flagged `early`). The advisor adds `EventAhead` (`event_ahead`, advisor v2): an announced release within the next 3 bars votes 0 and pulls the sum one step toward wait. `NewsKind::MacroData` = 8 and `NetworkUpgrade` = 9 are appended. `tests/test_events.cpp` checks that changing an announced item's actual, tone or credibility changes no state view before its release (through the C ABI's state JSON), the exact first bar it is listed, identical bars with and without the lead, and the advisor reason's window; `tests/test_intel.cpp` checks perks against every mission, m11 included.

**Reveal events** add the consensus, credibility, `announceLead`, `gapBp` (open after the release against its close), `moveBp` (close 3 bars after against the close before) and `impliedBp` (the ATM straddle of the first expiry still live 3 bars after the release, quoted the bar before, as basis points of the price; −1 without options), and report the drawn intensity.

**Mission data.** `MissionDef` may override the cost model and the contract (m06's thin book: half-spread 0.10%, slippage 0.20%, capacity 4% of bar volume, base volume 1,500) and declares its own short permission (m06 is long only, 2× cap). `NewsKind::LiquidityOutflow` = 7 is appended (existing kinds keep their numbers; `validate` refuses kinds above it).

## Start options and the advisor (policy v11, v12)

`start_run(scenario, seed, RunOptions{advice_allowance, news_lead, calendar, starting_balance, leverage_cap})` (C ABI `tl_run_create_ex`) stores the options in the run state, the digest and checkpoints; the defaults are a run with the mission's own rules, without advisor or perks. Policy v11 appended the first three fields to the state digest, so the frozen state digests changed through the version while every event digest stayed the same: a default run executes exactly as under v10.

Policy v12 adds the campaign options (checkpoint format 3): `starting_balance` (> 0) replaces the mission's initial balance, so a campaign mission starts from the capital the previous one ended with, and `leverage_cap` (> 0) lowers the mission's cap where the curriculum keeps leverage above 2× locked. `run_initial_balance(sc, st)` and `run_leverage_cap(sc, st)` (`include/tradelab/engine.hpp`) are the run's own rules: the wallet, `SetLeverage`, the bankruptcy check, the state's `initialBalance` and `contract.leverageCap`, the score's net P&L and its 2%/20% risk limits, and the buy-and-hold benchmark all use them. In the C ABI the balance is a decimal string (NULL or "" = the mission's; "0", negatives or garbage make `tl_run_create_ex` return NULL). The two fields are appended to the state digest, so the state goldens changed again with v12 and every event digest stayed the same. `tests/test_campaign.cpp` covers both options, the checkpoint round trip and the refused values.

`RequestAdvice` (`tl_request_advice`) stores `advise(scenario, state)` (`include/tradelab/advisor.hpp`) in `RunState.advice` and uses one unit of the allowance. It does not advance time or touch orders, positions or balances, and with no allowance left it is rejected with `NoAdviceLeft` before anything is recorded (the digest does not change). `advise()` reads only bars up to the step, published funding, patterns ending at the step and news released by then (or within `news_lead` bars). `test_advisor.cpp` checks this by changing every later bar, funding rate and headline. The debrief adds each read's outcome, the advisor's hit rate and `advisorPointsEarned` (scoring v6). Scoring v7 matches each plan to the entry order it was recorded for (same decision step, kind and side; an add adds its planned risk) and reports `writtenOff`, the option loss the simulated fund covered after the wallet was floored at zero.
Policy v13 adds `MoveOrder` (`tl_move_order(run, client_id, order_id, price)`, Python `Run.move_order`, replay action `{"k":"move","id","price"}`): a working reduce-only stop or take-profit moves to a new trigger in place, keeping its id, quantity, bracket parent and OCO group, with an `order_moved` order update. Only the policy number in the state digest changed the frozen state digests; every event digest stayed the same. `tests/test_brackets.cpp` covers it. Scoring v8 (report-only, no digest change) attributes option P&L by full revaluation (`engine/src/debrief.cpp`), picks the IV finding by side (`iv_rise` for written options), adds `wrote_options`, and measures buy-and-hold to the run's last bar. The state JSON names the bar interval (`stepSeconds`: 3600 for the missions, the pack's for an imported market).

Policy v14: a reduce-only exit larger than the position it closes (a bracket leg left at its size after a partial close) cancels its remainder (`position_closed`) on the same bar, right after its own fill update; before, it stayed `partially_filled` without a position until the next bar. Only the policy number in the state digest changed the frozen state digests; every event digest stayed the same. `tests/test_brackets.cpp` covers it.

Policy v15: a triggered stop is a market order. A reduce-only stop or trailing stop cut short by bar capacity executes its remainder at the next opens (with the market orders, from the open plus costs; it keeps its trigger as the requested price), where before it rested at its trigger again and could stay open while the market traded above it, then fill at the trigger. Any other stop remainder (an entry stop) is cancelled with `insufficient_capacity`, like a market order's. A stop with no capacity left at its trigger still rests (no fill yet). Only the policy number in the state digest changed the frozen state digests; every event digest stayed the same. `tests/test_brackets.cpp` covers it. Scoring v10 (report-only) names a trade's exit by the order that closed it: `stop`, `trailing` (new), `target` (take-profit or limit), `manual` (market), so a stop that exits in profit is no longer a target (`tests/test_debrief.cpp`).

Policy v16 (design D6j, `tests/test_policy_v16.cpp`):

- **Capacity priority.** Phases keep the bar's order (open, adverse exits, favorable exits, entries); within a phase, orders competing for the bar's capacity are served by price, then sequence, instead of placement order: the level the market reaches first (at the open along the gap from the previous close, intrabar nearest to the open), then the command sequence (bracket legs carry their entry's), then the id. Market orders at the open are served by sequence.
- **Percentage trailing stops.** A trailing stop takes a fixed distance or a rate of the best close (`Command.trail_rate`, exactly one of the two, rate in (0, 1), else `invalid_tick`); the distance is `ceil(close × rate)` and the trigger is rounded outward to the tick at placement and at every close. C ABI `tl_place_trailing_rate(run, client_id, side, qty, reduce_only, "0.02")`, Python `Run.place_trailing_rate`, replay `{"k":"order","type":"trailing_stop",…,"trailRate":"0.02"}`; the state lists `trailRate` per order. `Order.trail_rate` is appended to the order serialization (checkpoint format 4).
- **Maintenance-margin tiers.** `ContractSpec.margin_tiers` (`notional_floor`, `maintenance_margin`, `max_leverage`) above the base rate and leverage, keyed by the position's notional at its average entry: the liquidation price (and so the debrief's safe leverage) uses the tier's rate, and an order whose resulting position sits in a tier capped below the run leverage is rejected with `leverage_above_tier` (checked again at fill; `SetLeverage` too). Built-in missions: 0.5% (50×) below 50,000, 1% (20×) from 50,000, 2.5% (10×) from 250,000, 5% (5×) from 1,000,000. The state's `contract.marginTiers` lists them, base first. Packs keep one rate.

The policy number and the appended `trail_rate` changed every frozen state digest; every event digest stayed the same.

Policy v17 (design D6k, `tests/test_policy_v17.cpp`):

- **Mark price.** `Scenario.mark` (one bar per bar; empty = `markFromLastPrice`): liquidation at the open, intrabar (mark low/high) and at the close, funding (mark close), the account `mark` (unrealized, equity, notional) and the debrief's open-trade mark read it through `mark_bar(sc, i)`; fills, stops, limits and capacity keep the traded bar; option pricing keeps the traded close. The state's `contract.markFromLastPrice` is true for every built-in mission and pack.
- **Settlement window.** `OptionSpec.settlement_minutes` (default 30) replaces `settlement_bars`. `settlement_price` takes the TWAP of `Scenario.sub_bars` closes for the expiry bar when they tile the window (not approximate), else the mean of the closes of the bars covering it (approximate unless they tile it). `in_settlement_window(sc, series, bar)`: an option order that would execute at an open at or after the window start is rejected (or, resting, cancelled) with `series_settling` (reason 34). Quotes carry `settling`; the options JSON `settlementMinutes`.
- **Continuity bars.** `Bar.continuity` (flat at the previous close, zero volume, never bar 0): zero capacity, so only the close phase runs and working orders wait for the next trading bar. `MissionRules.gap_policy`: `Skip` runs indicators and patterns over the trading bars only (`src/gaps.{hpp,cpp}`, no value at a continuity bar; VWAP keeps real-bar sessions), `CarryForward` feeds the flat bar. The state's bar tuple gains a seventh element (`continuity`) and the contract `gapPolicy`.

Built-in missions have no mark series, sub-bars or continuity bars and the default window starts after the expiry bar's open, so only the policy number changed the frozen state digests; every event digest stayed the same.

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

**Container.** A `.tlpack` is an uncompressed ustar archive with flat file names: `manifest.json` (stored in canonical form), `manifest.sig` (64-byte Ed25519 signature over the canonical manifest), `bars.bin`, optional `funding.bin`, optional `reveal.json`. `events.bin`, `options.bin`, `mission.json` and `i18n.json` are reserved and refused as `unsupported_feature` until packs carry news and option series.

**Data files.** `bars.bin` is a zstd frame of a columnar table: a 16-byte header (`TLB1`, rows, 5 columns, 0), then little-endian int64 columns open, high, low, close (1e-8) and volume (1e-8); with `data.gapPolicy` declared, a sixth column holds the continuity flag (0 or 1), otherwise exactly five columns are required. `funding.bin` is the same with magic `TLF1` and one rate column (1e-10). The manifest lists every file with its stored size, decompressed size and SHA-256, so the signature covers all data.

**Manifest v1 fields.** `schemaVersion`, `scenarioId`, `packVersion`, `mode` (historical, synthetic, branch), `private`, `engineCompatibility` {`min`, `max`}, `executionPolicyVersion`, `scoringVersion`, `timeframe`, `stepSeconds`, `displayPrecision`, `aliasPolicy` (`per-run`), `instruments.perpetual` {multiplier, tickSize, lotSize, minQty, maxLeverage}, `costModel` {makerFee, takerFee, halfSpread, slippage}, `liquidityModel` {capacityFraction}, `fundingModel` {intervalSteps, source: file or none}, `ivModel` {kind: realized or none}, `marginModel` {maintenanceMargin}, `missionRules` {initialBalance, leverageCap, warmupBars, allowShort}, `data` {bars, gaps, optional gapPolicy: skip or carryForward}, `files`, `signingKeyId`, `provenance`, `license` {name, attribution, transformation, commercial, offlineRedistribution}, `distribution` {downloadable}. Decimals are strings; JSON numbers must be integers (no fractions or exponents, within 2^53).

**Order of checks** (`open_pack`): size limit, container, manifest JSON and schema (`missing_field` / `invalid_field` name the dotted path), signature against the caller's trusted keys, engine compatibility, each listed file's size and hash (`hash_mismatch`, DATA02), decompression into exactly the declared size (`decompression_limit` on bombs), column decoding, `validate(Scenario)`. With publish intent it also checks `private_pack` and the license record (`license_incomplete`, LIC01).

**Limits** (`PackLimits`, `kCsv*`): pack 16 MiB, 8 files, manifest 64 KiB, 8 MiB per decompressed file, 20,000 bars; CSV 4 MiB, 20,000 rows, 512-byte lines, at least 50 rows.

**CSV import.** A header row with `timestamp` (or time, date, datetime, open_time, ts), open, high, low, close, volume and an optional funding column, in any order; comma separated, dot decimals, at most 8 decimals (10 for funding), no exponents. Timestamps are integers in the declared unit (seconds or milliseconds; the wrong unit is `timestamp_unit`, DATA01, and is never rescaled) or UTC ISO dates. Rows must increase by a multiple of the declared interval; larger steps are reported as gaps. By default the rows around a gap are joined; with the metadata `"gapFill": "skip"` or `"carryForward"` (policy v17) each missing interval becomes a continuity bar (previous close, zero volume, zero funding), at most 20,000 bars in all (`too_many_bars`). Empty funding cells keep the last rate.

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
