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

Two kinds of goldens freeze the engine's output:

- **Unit vectors**, inline in the tests that own them: `tests/test_det_math.cpp` and `tests/test_prng.cpp` (kernel outputs and normal variates), `tests/test_options.cpp` (OPT01: a grid of option quotes and Greeks), `tests/test_sha256.cpp` and `tests/test_ed25519.cpp` (standard vectors).
- **Scripted runs and reveals**, as shared, language-neutral fixtures in `tests/golden/` (below), replayed by `tests/test_golden.cpp` (native and WASM), `bindings/python/tests/test_golden.py`, and the desktop and iOS clients (add-native-clients tasks 1.6 and 2.1).

`tests/test_mission_learnability.cpp` (m01–m08 and the buy-and-hold bound), `tests/test_mission_learnability_b.cpp` (m09–m12; both share the reference players in `tests/learnability.hpp`) and `tests/test_mission_realism.cpp` are not goldens: they assert bands over 60 seeds of every mission (design D8.3), so a generator or mission change that makes a lesson unlearnable or predictable fails them.

CI runs them on every target (linux-x86_64, macos-arm64, wasm under node, Python). A digest failure means cross-platform results diverged. Do **not** update the digest to make CI green.

The only legitimate update is an intentional change to a kernel, the PRNG, the generator or execution rules. That change must:

1. bump the execution-policy version (old runs must keep replaying), and
2. be explained in the PR, regenerating the digests from a native build (`python3 tools/regen_golden.py --write` for the fixtures) and confirming WASM matches them.

A new mission is not such a change: it adds its own golden and leaves every existing one byte-identical (design D8.1). Each generator capability acts only when a mission declares it, so the phase-2 missions added no policy version.

History of the scripted goldens: m01 and m05 since phase 1; policy v2 (order fields) and v3 (instrument field) changed both digests; m01 gained news events (content); v4 added plans and calibration answers to the state digest; v7 changed m05 (jump before the first expiry); v9 (funding from the last closed bar) changed the state digests through the version; v10 (news priced on arrival) regenerated every mission market; v11 appended start options and advice to the state digest (event digests unchanged, so a default run executes exactly as before) and added the m02 advisor golden; phase 2 added one golden per new mission (m06–m12), the CSV pack golden and the m08/m11 reveal digests.

### Shared fixtures (`tests/golden/`)

`index.json` lists every fixture by name (`{"format": 1, "fixtures": [...]}`); CMake fails at configure time when it does not list exactly the `*.json` files in the folder, and `test_golden.py` checks the same. Each fixture is one JSON file, `<name>.json`:

| Field | Meaning |
|---|---|
| `format` | Fixture format version, currently `1`. A reader refuses other values. |
| `kind` | `"run"` (a scripted run) or `"reveal"` (a seeded mission reveal). |
| `name`, `description` | The file name without `.json`; what the script exercises. |
| `mission` | Built-in mission id (`tl_missions`). A `run` has either `mission` or `csvPack`. |
| `csvPack` | A private market: `csv` (the file's lines, joined with `\n` plus a final `\n`), `meta` (the import metadata object of `tl_pack_build_csv`) and `deviceSeed` (64 hex). Build the pack with `tl_pack_build_csv(csv, meta, deviceSeed)`, trust only `tl_device_key(deviceSeed)` and start it with `tl_run_create_pack`. |
| `seed` | Run seed (`uint32`). |
| `options` | Optional start options in the client's run-record shape (`adviceAllowance`, `newsLead`, `calendar`, `startingBalance`, `leverageCap`; absent = `tl_run_create_ex` defaults: 0, 0, off, `NULL`, 0). |
| `actions` | The action log in the web client's format (documented in `bindings/python/tradelab_engine/replay.py`). Client ids are 1, 2, … for every action except `adv`; `{"k":"adv","n":N}` is `tl_advance(run, N)`. JSON `null` is a `NULL` argument. |
| `expected` | `run`: `digest` and `eventsDigest` (the state's last two fields after the last action, read with `tl_state`), `status` (`finished`, `bankrupt` or `active`) and `rejected` (`[{"action": index in actions, "reason": code}]`, every rejected command; the others were accepted). `reveal`: `sha256`, the SHA-256 of the exact bytes `tl_mission_reveal(mission, seed)` returns. |
| `versions` | `tl_version()` of the engine that produced `expected`. A replay asserts `executionPolicy` equals the engine's; `engine` is informational. |

A client replays a `run` fixture through the same C ABI the engine tests use (`tests/golden.cpp` is the reference implementation) and compares `expected` field by field. The files are embedded in the test binaries at configure time (`generated/golden_fixtures.inc`), so the WASM tests need no file system.

Regeneration: `python3 tools/regen_golden.py` replays every fixture through the Python binding (`libtradelab` from `engine/build/native-release`, or `TRADELAB_LIB`) and exits 1 when an `expected` or `versions` block, or the file layout, differs (CI runs it in the Python job); `--write` rewrites them. It never touches the inputs (market, seed, options, actions, description). To add a fixture, write those inputs to `tests/golden/<name>.json` (with `"format": 1` and `"kind"`), add the name to `index.json`, run `--write`, and check that every earlier fixture is unchanged.

## Interface definition (IDL)

`engine/idl/tradelab.idl` declares the enumerations shared by state, commands, events and the debrief (numeric value and JSON name) and every JSON shape the C ABI returns. `python3 engine/idl/gen.py` writes three files that are never edited by hand:

- `include/tradelab/idl_types.hpp`: the C++ `enum class`es and `json_name()`, which the C ABI writers use;
- `tests/idl_schema.hpp`: the shapes as JSON, checked by `tests/test_idl_conformance.cpp` against real output (every mission played with every command kind, a CSV import, a pack run, the packer calls, the error paths): exact keys, value types, declared enum names, decimal strings;
- `apps/web/src/lib/engine/types.ts`: the web client's types.

Adding a field or an enum value: edit the IDL (append enum values, never renumber them: they are part of the recorded-run format), run the generator, then make the C ABI emit it; the conformance test fails until both agree. `gen.py --check` runs in CI (engine lint job and web job) and fails on a stale generated file. The engine's internal structs (`RunState`, `Command`, `Scenario`) stay hand-written (design D10).

## Indicator reference values

`tests/test_indicator_reference.cpp` checks every indicator against TA-Lib's 252-bar daily regression series with TA-Lib 0.8.3's outputs for the game's parameters, vendored in `tests/data/talib_reference.inc` with TA-Lib's BSD-3-Clause license (`tests/data/TA-LIB-LICENSE.txt`). SMA, EMA, RSI, ATR, VWAP and Bollinger share TA-Lib's definitions: the warm-up equals TA-Lib's lookback and values agree within 1e-6. MACD and +DI/−DI/ADX seed their smoothing differently from TA-Lib (each EMA at its own period; Wilder sums seeded with the full period), so they agree within 1e-6 (MACD) and 1e-3 points (DI/ADX) from bar 150 on, with the same warm-up. Regenerate the table with `uv run --python 3.12 --with TA-Lib==0.8.3 --with numpy python tools/indicator_reference.py`.

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
### Mission rules, decisions and lesson questions (scoring v11)

`MissionRules` carries the mission schema (task 4.1): `objectives` (`ObjectiveCode`, declared in the IDL like `DecisionKind`, + `value_bp`; packs get the common three), `liq_warning_bp` (3% unless declared; m02 10%), `drawdown_budget_bp` (20%; m07 10%) and `confirm_stop_beyond_liq`. None of it feeds execution. `tl_state` and `tl_debrief` write `"missionRules"` with the run's own balance and cap and the instrument permissions (long; short = `allow_short`; options buying when a chain is listed; options writing = `options.allow_short`).

`advance()` appends a `DecisionRecord` for the step it leaves (task 4.2): `act` when an order was placed, cancelled or moved, or leverage or margin changed at that step; `hold` when nothing was sent while a position was open; `wait` otherwise. It is a function of the state at each step, so it is not a state-digest input (like the command log); checkpoints carry it (format 5; a checkpoint of an earlier format is refused like any other format mismatch). The debrief lists `"decisions"`.

`AnswerLesson` (`tl_answer_lesson(run, cid, question, choice, value, skipped)`, Python `Run.answer_lesson`, replay action `{"k":"lesson","q","choice","value","skipped"}`) records one answer per question of the mission's rubric (task 4.5); it changes nothing else and is rejected with `unknown_question` (reason 35), `already_answered` (36) or `invalid_answer` (37); reasons 33 and 34 belong to policies v16 and v17. Lesson answers are appended to the state digest only when present, so every existing golden is unchanged. Rubrics are data: `engine/rubrics/lessons.json` (per mission: `reviewed`, `reviewer`, questions with `kind` choice or numeric, `choices`/`answer` or decimal-string `answer`/`tolerance`, `points`), embedded at configure time and parsed by `src/lessons.cpp`; `tl_lesson(mission)` lists the questions without the key. Understanding = points earned / points of every question, only for a reviewed rubric (a reviewed rubric must name its reviewer) with at least one answer or skip. Every shipped rubric is `reviewed: false` until a person signs it off. Free-text answers are not supported (`kind: text` is refused): human review of free text stays a later step.

Scoring v11 risk inputs (task 4.6): −15 when the max drawdown exceeds the mission's budget (was a fixed 20%), −10 when a perpetual trade's liquidation price (at the highest leverage it carried) is closer to its entry than twice its stop distance (finding `leverage_vs_stop`), −10 when the closest liquidation distance at a close fell below the mission's warning distance (`liq_buffer`); `drawdown_budget` names a breached budget. Tests: `tests/test_decisions.cpp`, `tests/test_lessons.cpp`, `bindings/python/tests/test_scoring_v11.py`.

Scoring v12 (report-only, no digest change): a perpetual trade closed by its stop keeps its plan (`planRespected`) when the stop's trigger sat at or inside the plan's invalidation (1% tolerance), even if a gap filled it further away; before, the fill price alone could mark it violated. `tests/test_scoring.cpp`.

### Liquidity shocks in the state view

The state JSON names the liquidity shocks the player can see, without changing the market or any digest: `contract.liquidityShocks` is true when the scenario has any, and `"liquidity"` holds the cost model (`halfSpread`, `slippage`, `capacityFraction`, from `costs_at`) of the next bar — the one an order placed now executes on — while a shock covers it and is visible: already in force at the current bar, or starting on the next bar right after a headline released at this one (the generator starts each shock the bar after its headline). Otherwise it is `null`, so a shock is never shown before the player could have seen it. The option view also carries `feeRate` (the option fee is that fraction of the underlying notional, capped at 12.5% of the premium), so clients check premium + fee against the available balance like `required_for`. `tests/test_event_channels.cpp`.

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
| `bars.bin` | `TLB1`: open, high, low, close (1e-8), volume (1e-8); with `data.gapPolicy` declared, a sixth column holds the continuity flag (0 or 1), otherwise exactly five columns |
| `funding.bin` | `TLF1`: rate (1e-10) |
| `events.bin` | `TLE1` (rows `data.events`): release_step, kind, tone (−1..1), credibility (0..100), expectation (−2..2), actual (−2..2), announce_lead; then `TLQ1` (rows `data.liquidityShocks`, may be 0): start, bars, spread_pct, capacity_pct |
| `options.bin` | `TLO1` (rows `data.optionSeries`): id, kind (0 call, 1 put), strike (1e-8), list_step, expiry_step; then `TLI1`: implied-vol base (1e-8, 0..1,000%), one row per bar with `ivModel.kind` = `file`, else 0 rows |

Each column is range-checked before narrowing (`malformed_data`); semantic checks (steps inside the market, kinds, ids 1..n, liquidity shocks) are `validate(Scenario)`'s (`invalid_scenario`). The manifest lists every file with its stored size, decompressed size and SHA-256, so the signature covers all data.

**Manifest v1 fields.** `schemaVersion`, `scenarioId`, `packVersion`, `mode` (historical, synthetic, branch), `private`, `engineCompatibility` {`min`, `max`}, `executionPolicyVersion`, `scoringVersion`, `timeframe`, `stepSeconds`, `displayPrecision`, `aliasPolicy` (`per-run`), `instruments.perpetual` {multiplier, tickSize, lotSize, minQty, maxLeverage}, optional `instruments.options` {multiplier, premiumTick, lotSize, feeRate, spread, skew, smile, termSlope, minutesPerStep, settlementMinutes (policy v17), imBase, imFloor, mmBase, allowShort} (requires `options.bin`), `costModel` {makerFee, takerFee, halfSpread, slippage}, `liquidityModel` {capacityFraction}, `fundingModel` {intervalSteps, source: file or none}, `ivModel` {kind: realized, none or file}, `marginModel` {maintenanceMargin}, `missionRules` {initialBalance, leverageCap, warmupBars, allowShort}, `data` {bars, gaps, optional events, liquidityShocks, optionSeries (default 0), branchStep (required in branch mode, refused otherwise), gapPolicy (skip or carryForward, policy v17: written when the bars include continuity bars)}, `files`, `signingKeyId`, `provenance`, `license` {name, attribution, transformation, commercial, offlineRedistribution}, `distribution` {downloadable}. Decimals are strings; JSON numbers must be integers (no fractions or exponents, within 2^53). Perpetual-only packs without news write exactly the fields of the first version, so existing packs keep their bytes.

**Order of checks** (`open_pack`): size limit, container, manifest JSON and schema (`missing_field` / `invalid_field` name the dotted path), signature against the caller's trusted keys, engine compatibility, each listed file's size and hash (`hash_mismatch`, DATA02), row limits (`too_many_bars`, `too_many_events`, `too_many_series`), decompression into exactly the declared size (`decompression_limit` on bombs), table decoding, bar labels, `validate(Scenario)`. With publish intent it also checks `private_pack` and the license record (`license_incomplete`, LIC01).

**Limits** (`PackLimits`, `kCsv*`): pack 16 MiB, 8 files, manifest 64 KiB, 8 MiB per decompressed file, 20,000 bars, 1,000 news rows and 1,000 liquidity shocks, 2,000 option series; CSV 4 MiB, 20,000 rows, 512-byte lines, at least 50 rows.

**Synthetic and branch packs.** Bars of a `synthetic` pack are flagged synthetic (as a built-in mission's are). A `branch` pack (spec market-generator "Branch labeling") is a recorded window up to and including `data.branchStep`, followed by generated bars: every bar after the branch point is flagged synthetic, the ones before it are not. The state view carries the flag as the 6th element of each bar, and the web client shows a "synthetic branch" tag from the first generated bar on. `splice_branch` (`src/masking.cpp`) builds one: the alternative market's bars from branch_step + 1 are rescaled (half-even to the tick, volume by the inverse factor) to continue from the recorded close; funding, news released after the branch (announced no earlier than the bar after it) and liquidity shocks starting after it come from the alternative.

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
