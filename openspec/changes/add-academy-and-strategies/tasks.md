# Tasks: add-academy-and-strategies

The tasks are split so parallel builders can own sections:
- **E (engine and bindings):** independent of the clients.
- **W (web):** owns the content files.
- **D (desktop):** follows W's content and E's calls.

Dependencies are written as "needs X". Each task ships its tests and updates the README, docs and this change in the same PR, and is ticked only when fully done.

Suggested PRs, in order:
1. E1–E6 (shared kernel, what-if, dry run, spread margin, fixtures, bindings);
2. W1–W4 (content move, runner, first lessons, glossary);
3. E7–E10 (missions m13–m17, bands, comparison, docs);
4. W5–W11;
5. D1–D9 in parallel with 3 and 4;
6. X (cross-checks and docs).

## E. Engine and bindings

- [x] E1 Shared pricing kernel: move `make_inputs`, `black76` and quantization behind one internal function used by `quote()`. OPT01 and every golden must stay unchanged (no digest diff; `tools/regen_golden.py` shows no change).
- [x] E2 What-if valuation (design D4):
  - `tl_option_whatif(json)` (no run) and `tl_run_option_whatif(run, json)` (series ids, run IV model with the current step's base, shifts for forward, bars and IV points floored at 5%);
  - outputs per leg and net (value, P&L, Greeks) with the representations of D4, and exact expiry analytics (breakevens, `maxProfit`/`maxLoss` with `null` for unbounded, the price floor at 0);
  - payoff curves at expiry and at the shift.

  Tests:
  - OPT05: zero shift equals `quote()` (value rounded to the tick = mid, Greeks bit-exact) for every series of every mission at 5 steps;
  - analytics against a fine-grid brute force for the seven templates and random leg sets;
  - OPT06: no look-ahead (bars and `iv_base` after s altered);
  - purity (state digest and event log unchanged);
  - at expiry the time-value Greeks are 0.

  `kAbiVersion` stays 1 (additions); update `c_api.h` docs.
- [x] E3 Combo dry run `tl_combo_preview(run, legs_json)`, needs E2:
  - applies the ordered legs on a copy of `RunState` with the command functions;
  - reports per leg accepted/reason, the quote crossed, premium, fee, margin reserved and available after, plus totals.

  Tests:
  - state and digest identical before and after;
  - matches a real placement leg by leg (reasons and balances) for every template in m05, m10 and a `spread_margin` scenario;
  - a perpetual leg included.
- [x] E4 Defined-risk margin (design D5): `OptionSpec.spread_margin`, pairing (requirement per pair, greedy by series id, lowest requirement first), the condor rule (larger side when the short strikes do not overlap), min with per-leg, working buys counted at acceptance, filled longs only at execution; `missionRules.spreadMargin` in the state view.

  Property tests:
  - a debit spread's short needs 0;
  - a credit spread ≤ width × units;
  - a condor = its larger side;
  - the unpaired remainder per leg;
  - order independence;
  - a cover cancelled at the open → the short is checked naked and cancelled `insufficient_margin` when it does not fit;
  - flag off: m05 and m10 goldens and `option_book` unchanged.
- [x] E5 `whatif` golden fixture kind:
  - `tests/golden/whatif-call.json`, `whatif-condor.json` and `whatif-covered-call.json`, listed in `index.json`;
  - replayed by `test_golden.cpp` (native and WASM) and `bindings/python/tests/test_golden.py`;
  - `tools/regen_golden.py` writes them;
  - the engine README's fixture table documents the kind.
- [x] E6 Bindings, needs E2–E4:
  - WASM exports and worker requests `whatif`, `runWhatif` and `comboPreview` (feature-detected: absent on an older pinned build → the client hides the lab and builder analysis);
  - Python `option_whatif`, `Run.option_whatif` and `Run.combo_preview`;
  - parity tests;
  - an optional `combo` field on option actions, ignored by the adapters, with a test that a tagged log replays to the same digest as the untagged one (Python and WASM).
- [ ] E7 Missions m13–m17 (design D8) in `missions.cpp`:
  - generator specs from existing capabilities only (`kGeneratorVersion` stays 5);
  - chains, `spread_margin` (m13, m14, m16, m17), m15 buy-only, objectives, liquidation warning and drawdown budget;
  - `tl_mission_reveal` labels;
  - one golden per mission (default options), with existing goldens byte-identical;
  - rubrics in `engine/rubrics/lessons.json` (two questions each, `reviewed: false`).
- [ ] E8 Learnability and realism, needs E7: `tests/test_mission_learnability_c.cpp` with the D8 bands (reference players in `tests/learnability.hpp`), the realism bands pooled over 17 missions, and `tools/market_study.py` rows for m13–m17. Record any tuning as "As built" in design D8; bands are never widened silently.
- [ ] E9 Strategy comparison, scoring v13 (design D8):
  - per-mission comparison declarations;
  - `debrief.strategyComparison` (name code, entry and exit bar, net P&L, max drawdown, margin calls) computed after the run with the engine's quotes and fees;
  - report-only (no score input, no digest);
  - refused while the run is active (worker schema test).

  Tests: comparison rows reproduce a scripted run of the same structure.
- [ ] E10 Engine README (what-if, dry run, `spread_margin`, fixture kind, scoring v13), `c_api.h` comments, policy and ABI history notes (unchanged, and why).

## W. Web (owns the shared content)

- [x] W1 Content source (design D1):
  - `src/lib/content/` with `academy/index.json`, `academy/<id>.json`, `glossary.json` and `coach.json`, plus a typed loader and validator (`content/academy.ts`);
  - Foundations moves to `academy/foundations-bar.json` with identical texts, and its existing unit and e2e tests pass unchanged;
  - lessons load lazily per file;
  - `budget:size` passes.

  Schema tests: both locales for every text, unique ids, valid widget and question shapes, no mission id in a lesson.
- [x] W2 Lesson runner (design D2), needs W1:
  - question types `choice`, `number` (tolerance, either decimal mark) and `set` (widget target), with per-choice feedback;
  - widgets `chart` (levels: entry, stop, target, liquidation, mark and last, strike), `ticket` (sandbox on `sizing.ts`/`stops.ts`), `table`; `payoff`, `whatif` and `builder` come in W5 (needs E6);
  - keyboard and screen-reader support like the existing runner;
  - unit tests for each widget's viewmodel and e2e for a `set` question.
- [x] W3 Lessons, part 1 (perpetual, orders, risk, evaluation), needs W2:
  - `structure-zones`, `orders-execution`, `futures-pnl`, `futures-margin`, `futures-funding`, `risk-sizing`, `risk-expectancy`, `patterns-lag` (with its failure example), `events-surprise`, `mind-plan`;
  - PT-BR and en, with `checks` for every stated number and Vitest verifying the arithmetic kinds.
- [x] W4 Glossary (design D7), needs W1:
  - `glossary.json` (≈ 70 terms, both locales, stage, related, examples with checks);
  - a `Term` component (hover, focus and tap; `aria-describedby`; "more");
  - the Glossary screen (search ignoring accents and case);
  - marked terms in the ticket, rail, option chain and debrief findings (the builder's outputs are marked with W7);
  - `[[id|text]]` links in lesson and tutorial texts.

  Tests: every linked or marked term exists; search; e2e tooltip on focus; axe.
- [ ] W5 What-if lab (design D4, options-whatif spec), needs E6:
  - worker requests and the `WhatIfLab` component (price, time and IV sliders; expiry and today curves; per-leg and net Greeks; the "Black-76, zero rate" note);
  - Academy mode (editable legs) and terminal mode (from positions or builder legs, the "today's IV level" note);
  - lesson widgets `payoff`, `whatif` and `builder` (open: they plug into W2's runner; `WhatIfLab` already takes `unlocked` sliders and Academy legs);
  - unit tests with a fake worker;
  - e2e: zero shift equals the chain, sliders change engine numbers.
- [ ] W6 Lessons, part 2 (options and strategies), needs W5. As built so far: the five lessons exist with the `payoff` widget and expiry-arithmetic checks (Vitest); left: the `whatif` and `builder` widgets and the engine-checked numbers below: `options-payoff`, `options-greeks`, `options-iv`, `options-writing`, `strategies-shapes`, with checks verified by `bindings/python/tests/test_academy_numbers.py` (needs E6).
- [x] W7 Strategy builder (design D3, strategy-builder spec), needs E3, E4 and E6:
  - templates from the chain, the short toggle, free legs on one expiry, "use my long";
  - writing-locked templates disabled with the reason;
  - analysis from `runWhatif` + `comboPreview`;
  - submission order (long options, perpetual, short options) with `combo` tags;
  - cancel on refusal at placement;
  - incomplete-combo detection after the advance, with a notice, "close open legs" (confirmed) and "keep";
  - journal and debrief grouping;
  - its outputs marked as glossary terms (W4's `Term`).

  Unit tests (ordering, cancel on refusal, incomplete detection) with a fake worker. e2e: a condor previewed, submitted and filled; a refused leg cancels the others; a forced no-bid leg gives the incomplete notice. Update `optionsMath.ts`'s comment: its mirrors stay for single-leg display only.
- [x] W8 Curriculum, Academy screen and review (design D9), needs W3:
  - the `strategies` stage in `curriculum.ts` (lesson-only until W10 adds m13–m17 to it and to the campaign order);
  - `stagePassed` by missions for stages with missions;
  - the map shows lessons x/y;
  - the Academy screen (by stage, status, retake, glossary link);
  - the debrief's recommended lesson opens the Academy;
  - progress v3 (`lessonReviews`, `coachDone`, `glossarySeen`) with `migrateProgress`;
  - review scheduling (1/3/7/21 days, reset, retire) and the dismissable menu prompt.

  Unit tests: unlocks never depend on lessons, migration, scheduling with a fake clock. e2e: review prompt with a mocked clock.
- [x] W9 Guided first trade (design D6), needs W1:
  - `CoachMarks` (non-modal callout, target outline, `aria-describedby`, live region, focus moves, reduced motion);
  - steps from `coach.json`, started on the first campaign terminal, the debrief step on that run's debrief;
  - skip and `Esc`; replay from the help overlay and the briefing; `progress.coachDone`;
  - Space and Enter on the callout never advance.

  e2e: shown once, skip, replay, keyboard only, axe.
- [ ] W10 Missions m13–m17 on the web, needs E7 and E9:
  - m13–m17 as the Strategies stage's missions in `curriculum.ts` (campaign order m06, m01, m03, m02, m07, m04, m09, m05, m10, m13–m17, m08, m11, m12);
  - `missions.ts` texts (PT-BR, en), tutorial cards in `tutorials.ts` with the new art (`spread`, `covered`, `insurance`, `condor`, `roll`) and margin-rule cards (mission-tutorial spec);
  - `lessons.ts` question texts (checked against the rubrics);
  - the briefing's margin rule;
  - the debrief's strategy comparison;
  - unit tests (cards: 3–5, no spoiler words);
  - e2e: each mission played to the debrief (`e2e/missions-batch-c.spec.ts`).
- [ ] W11 Web README: content layout, Academy, glossary, lab, builder, tour, progress v3, e2e list.

## D. Desktop

- [ ] D1 Generator, needs W1: `gen_i18n.mjs` reads `content/academy/*.json`, `glossary.json` and `coach.json` into `strings.csv` (`academyLesson.*`, `glossary.*`, `coach.*`) and `content.json`, replacing the TypeScript evaluation of `academy.ts`; `check_i18n.mjs` drift; script tests updated; the Foundations lesson renders as before (headless and windowed captures unchanged).
- [ ] D2 Engine adapter, needs E2–E4 and E6: `tl_option_whatif`, `tl_run_option_whatif` and `tl_combo_preview` on the `EngineHost` worker; the `combo` field in `storage::Action` and replay; the `whatif` fixtures in `tradelab_desktop_golden_tests`.
- [ ] D3 Lesson runner widgets and question types, needs D1 and D2:
  - viewmodels for `chart` levels, `ticket` sandbox, `table`, `payoff`, `whatif` and `builder`, plus the `number` and `set` questions;
  - painters in the view layer;
  - unit tests with the web's vectors;
  - headless lesson by keyboard.
- [ ] D4 What-if lab screen (Academy and terminal), needs D2; unit and headless tests; zero shift equals the chain.
- [ ] D5 Strategy builder panel in the option ticket, needs D2: templates, analysis, submission order with `combo`, cancel on refusal, incomplete notice, journal and debrief grouping; viewmodel tests with a fake engine; a headless flow.
- [ ] D6 Glossary screen and tooltips, needs D1: `Rwidget_set_tooltip` on hover, the help line and accessible description on focus; screen-reader names; headless test.
- [ ] D7 Coach marks overlay, needs D1: anchored to the named widgets; skip, `Esc`, replay from help and briefing; `prefs.json` `coachDone`; keys never advance from the callout; headless keyboard test.
- [ ] D8 Academy screen, curriculum Strategies stage and lessons x/y, the review prompt (`prefs.json` `lessonReviews`), and the recommended lesson from the debrief; tests with a fake clock.
- [ ] D9 Missions m13–m17 in the menu, campaign, briefing margin rule, tutorials (new art) and debrief comparison:
  - the playtest extended to 17 missions (strategies played with the builder);
  - windowed reference captures for every new screen on macOS and on Linux (lavapipe in docker; say so if docker is unavailable);
  - complexity ≤ 15;
  - desktop README.

## X. Cross-checks and docs

- [ ] X1 Same numbers: the playtest report for one m16 seed, played with the same legs on web and desktop, shows identical builder analysis, fills and debrief comparison.
- [ ] X2 Root README ("What can the player predict?" for m13–m17, the features table, the curriculum), `docs/product-spec.md` (Strategies stage, Academy, glossary, tour), and the "As built" notes in this design.
- [ ] X3 `openspec validate --all --strict`; cognitive complexity within targets (frontend ≤ 12, C++ ≤ 15); every earlier golden digest unchanged.
