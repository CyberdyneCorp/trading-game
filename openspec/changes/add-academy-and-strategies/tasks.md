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
- [x] E7 Missions m13–m17 (design D8) in `missions.cpp`:
  - generator specs from existing capabilities only (`kGeneratorVersion` stays 5);
  - chains, `spread_margin` (m13, m14, m16, m17), m15 buy-only, objectives, liquidation warning and drawdown budget;
  - `tl_mission_reveal` labels;
  - one golden per mission (default options), with existing goldens byte-identical;
  - rubrics in `engine/rubrics/lessons.json` (two questions each, `reviewed: false`).

  Done: the missions declare `spread_margin` (`MissionDef`), copied into `OptionSpec` once E4 adds the field; liquidation warning and drawdown budget keep the defaults (3%, 20%); the reveal reports spans and events like every mission. As built: design D8.
- [x] E8 Learnability and realism, needs E7: `tests/test_mission_learnability_c.cpp` with the D8 bands (reference players in `tests/learnability.hpp`), the realism bands pooled over 17 missions, and `tools/market_study.py` rows for m13–m17. Record any tuning as "As built" in design D8; bands are never widened silently.

  As built: design D8 (m13 one-strike spread; m16 quantities; m17 kernel and swing bands). m16's condor is sized per seed at 20% of equity at its maximum loss and is never margin-called under `spread_margin`.
- [x] E9 Strategy comparison, scoring v13 (design D8):
  - per-mission comparison declarations;
  - `debrief.strategyComparison` (name code, entry and exit bar, net P&L, max drawdown, margin calls) computed after the run with the engine's quotes and fees;
  - report-only (no score input, no digest);
  - refused while the run is active (worker schema test).

  Tests: comparison rows reproduce a scripted run of the same structure.

  As built: design D8 "As built (strategy comparison, E9)"; `tests/test_strategy_comparison.cpp`; the web's `strategiesEngine.test.ts` checks the WASM debrief refuses an active run and `REVEAL_FIELDS` lists the block.
- [x] E10 Engine README (what-if, dry run, `spread_margin`, fixture kind, scoring v13), `c_api.h` comments, policy and ABI history notes (unchanged, and why).
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
- [x] W5 What-if lab (design D4, options-whatif spec), needs E6:
  - worker requests and the `WhatIfLab` component (price, time and IV sliders; expiry and today curves; per-leg and net Greeks; the "Black-76, zero rate" note);
  - Academy mode (editable legs) and terminal mode (from positions or builder legs, the "today's IV level" note);
  - lesson widgets `payoff`, `whatif` and `builder`;
  - unit tests with a fake worker;
  - e2e: zero shift equals the chain, sliders change engine numbers.

  As built: the lesson widgets `whatif` (`LessonWhatIf`) and `builder` (`LessonBuilder`), design "Option lessons with the engine (W5, W6)"; e2e in `academy.spec.ts`.
- [x] W6 Lessons, part 2 (options and strategies), needs W5: `options-payoff`, `options-greeks`, `options-iv`, `options-writing`, `strategies-shapes` with the `payoff`, `whatif` and `builder` widgets; option values and Greeks stated only as engine `whatif` checks, verified by `bindings/python/tests/test_academy_numbers.py` and `content/academyEngine.test.ts` (which also check the expiry arithmetic of lessons and glossary examples against the engine).
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
  - the `strategies` stage in `curriculum.ts` (its lesson, with m13–m17 as its missions since W10);
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
- [x] W10 Missions m13–m17 on the web, needs E7 and E9:
  - m13–m17 as the Strategies stage's missions in `curriculum.ts` (campaign order m06, m01, m03, m02, m07, m04, m09, m05, m10, m13–m17, m08, m11, m12);
  - `missions.ts` texts (PT-BR, en), tutorial cards in `tutorials.ts` with the new art (`spread`, `covered`, `insurance`, `condor`, `roll`) and margin-rule cards (mission-tutorial spec);
  - `lessons.ts` question texts (checked against the rubrics);
  - the briefing's margin rule;
  - the debrief's strategy comparison;
  - unit tests (cards: 3–5, no spoiler words);
  - e2e: each mission played to the debrief (`e2e/missions-batch-c.spec.ts`).

  As built: texts, cards with the new art and the margin-rule cards, lesson texts, objectives, the Strategies stage and campaign order (with W8's curriculum), the briefing's margin rule (`missionRules.spreadMargin`, shown where writing is allowed), the debrief's strategy comparison (`StrategyComparison`, `comparisonLines`), unit tests and the e2e (legs placed one by one through the option ticket, and m13, m14 and m16 again through the builder, with the margin rule, the comparison, the sizing aid and a covered call's risk to the long's stop).
- [x] W11 Web README: content layout, Academy, glossary, lab, builder, tour, progress v3, e2e list.

## D. Desktop

- [x] D1 Generator, needs W1: `gen_i18n.mjs` reads `content/academy/*.json`, `glossary.json` and `coach.json` into `strings.csv` (`academyLesson.*`, `glossary.*`, `coach.*`) and `content.json`, replacing the TypeScript evaluation of `academy.ts`; `check_i18n.mjs` drift; script tests updated; the Foundations lesson renders as before (headless and windowed captures unchanged).

  As built: content.json also carries the glossary's structure (id, stage, related), the coach steps (id, target, completes), each lesson step's linked glossary ids (`terms`) and `glossary.ts` `FINDING_TERMS` (`findingTerms`); script tests in `tests/scripts/screen_sources.test.mjs`.
- [x] D2 Engine adapter, needs E2–E4 and E6: `tl_option_whatif`, `tl_run_option_whatif` and `tl_combo_preview` on the `EngineHost` worker; the `combo` field in `storage::Action` and replay; the `whatif` fixtures in `tradelab_desktop_golden_tests`.
- [x] D3 Lesson runner widgets and question types, needs D1 and D2:
  - viewmodels for `chart` levels, `ticket` sandbox, `table`, `payoff`, `whatif` and `builder`, plus the `number` and `set` questions;
  - painters in the view layer;
  - unit tests with the web's vectors;
  - headless lesson by keyboard.

  Progress: `chart` levels (painted and listed as text), the `ticket` sandbox (port of `ticketSandbox.ts`), `table`, `payoff` (port of `payoff.ts`, painted from `payoff_geometry`), `number` (either decimal mark, units; port of `readTypedNumber`) and `set` questions, review mode, with web parity fixtures (`payoff`, `ticketSandbox`, `lessonRun`), viewmodel tests and every lesson played headless from the keyboard and windowed with the mouse. Then the `whatif` widget (the shared lab panel on the step's legs, only its unlocked sliders) and the `builder` widget (templates on the synthetic chain, priced at the model, analysed), with unit tests of the lesson texts' engine numbers (`test_lesson_lab.cpp`), a headless keyboard flow and windowed captures in PT-BR and en at 1280x800 and 1024x768.
- [x] D4 What-if lab screen (Academy and terminal), needs D2; unit and headless tests; zero shift equals the chain.
- [x] D5 Strategy builder panel in the option ticket, needs D2: templates, analysis, submission order with `combo`, cancel on refusal, incomplete notice, journal and debrief grouping; viewmodel tests with a fake engine; a headless flow.
- [x] D6 Glossary screen and tooltips, needs D1: `Rwidget_set_tooltip` on hover, the help line and accessible description on focus; screen-reader names; headless test.

  As built: marked terms in the lesson sandbox and payoff analytics, the terminal's ticket, rail and option chain, and the debrief findings (`findingTerms`); each step's linked terms are buttons that open the glossary at the term; the focused term's definition shows on the screen's help line; search folds Latin-1 accents and case (web parity fixture `glossary`).
- [x] D7 Coach marks overlay, needs D1: anchored to the named widgets; skip, `Esc`, replay from help and briefing; `prefs.json` `coachDone`; keys never advance from the callout; headless keyboard test.

  As built: `coachDone` is stored with the rest of the progress (SQLite progress v3) instead of `prefs.json`; the callout is placed next to its docked target every frame (`academy::callout_box`), waits while a terminal dialog (calibration, plan) is open, describes its target for screen readers and is announced; the debrief step is inline over that run's findings. Windowed test with captures.
- [x] D8 Academy screen, curriculum Strategies stage and lessons x/y, the review prompt (`prefs.json` `lessonReviews`), and the recommended lesson from the debrief; tests with a fake clock.

  As built: `lessonReviews` lives in SQLite progress v3 (not `prefs.json`); the review prompt's dismissal is a display setting (`reviewDismissed`, the web's localStorage key); the curriculum stages (Strategies included) come from content.json, and the map shows each stage's lessons x/y.
- [ ] D9 Missions m13–m17 in the menu, campaign, briefing margin rule, tutorials (new art) and debrief comparison:
  - the playtest extended to 17 missions (strategies played with the builder);
  - windowed reference captures for every new screen on macOS and on Linux (lavapipe in docker; say so if docker is unavailable);
  - complexity ≤ 15;
  - desktop README.

  Progress: m13–m17 are in the menu, the campaign order and the curriculum's Strategies stage (generated from the web; fixtures regenerated), with their mission texts, tutorial cards (text; the desktop draws no card art yet), lesson questions and goldens (`tradelab_desktop_golden_tests` replays them and the `whatif` fixtures); each is played to the debrief with a builder structure in `tests/headless/test_headless_strategies.cpp` and `tests/windowed/test_windowed_strategies.cpp` (captures `strategy-*.png`), and the debrief groups a combo's trades under one row. The playtest suite plays the Strategies stage too (`tests/windowed/test_playtest_strategies.cpp`: the seven templates, the terminal and Academy labs, m13–m17 each with a structure, a campaign through the Strategies stage; PT-BR and en, 1280x800 and 1024x768). New reference images (`builder-condor-m13`, `academy-lab`, and `menu` with the Academy, glossary and lab buttons) were made on macOS only: docker did not respond on the development machine, so the Linux lavapipe run is open. Then the briefing's margin rule, the debrief's strategy comparison (port of `comparisonLines`, parity fixture), the tutorial art of m13–m17 (`viewmodel/game/tutorial_art`), the builder's sizing aid and a held long's stop, the playtest of m16 sized at 1%, of an m14 covered call on a stopped long and of the lesson widgets, and macOS reference images `tutorial-condor` and `lesson-builder-condor`. Open: the Linux lavapipe run of the reference images (docker did not respond on the development machine again).

## X. Cross-checks and docs

### Playtest of the merged web build (as built)

Played as a new player (guided first trade, every lesson right and wrong, glossary from the menu, terminal and builder, the lab's sliders, every template, m13–m17 with a sensible and a reckless approach, a campaign through Strategies; PT-BR and en; 1024 and 1440 px). Fixed, each with a regression test:
- scoring v13: `wrote_options` only for an uncovered short (a spread, condor or covered call was called naked writing and lost the good-process credit);
- the campaign debrief named a stage passed by its first mission (Strategies after m13, Options II after m04); now only stages whose missions are all assessed, web and desktop;
- covered call and protective put on a held long default to "use my long" with as many contracts as its units (they bought another perpetual unit and wrote one call against a 48-unit long);
- the combo plan named "× 1" whatever the contracts and called a short strangle's margin its planned risk; the debrief showed per-leg R from the split plan (±50R on a condor) and now shows one R per combo;
- glossary links rendered "strike ;" and "( writing )" (whitespace in `Term`), and bare `[[id]]` links showed the headword mid-sentence ("Uma Call", "o Spread (compra/venda)");
- theta in the what-if table now names its unit (a day in the Academy lab, a bar in a run), vega its point;
- builder outputs are marked glossary terms (W7 listed them, none were); "1 termo", "1 leg", localized leg quantities, the condor art label.

### Playtest of the merged desktop build (as built)

Played on macOS (MoltenVK) by injected input with the frames read: the guided first trade, every Academy lesson, the glossary, the Academy and terminal labs, every builder template, m13–m17 to the debrief, a campaign through the Strategies stage; PT-BR and en; 1280x800 and 1024x768. The same inputs on the web (same seed, mission, template, sliders) gave the same numbers: the builder's dry run, totals and analysis for all seven templates on m13 seed 3, both labs before and after their sliders, and m13–m17 (seed 3) to the debrief (result, fees, Greek attribution, the combo row and its legs, score). Linux was not run (docker did not respond). Fixed, each with a regression test:
- the menu's row of eight screen buttons ran past a 1024-wide window in English (the lab button cut); it wraps now, as the web's menu does (the playtest's clipping check at 1024x768);
- the builder's per-leg lines left out what the web's table shows in the same panel: each leg's IV, the position's value and P&L, the IV-floor note; and its payoff chart had neither the lab's legend (price range, line styles) nor an accessible description;
- the content generator wrote the bare id for a `[[id]]` glossary link without display text (the web shows the term's name in each locale); no such link exists in the content yet.

### Playtest of the finished desktop build (as built)

Played by injected input with the frames read, PT-BR and en, 1280x800 and 1024x768: the lesson what-if lab (options-greeks, options-iv) and builder (strategies-shapes) by mouse and keyboard, m13–m17 from their tutorial (new art) and briefing (margin rule) to the debrief comparison, the m16 condor sized at 1%, a covered call on an m14 long with its stop (planned loss 0,10 cr: breakeven 101,92 − stop 101,91 over 10 units, instead of the long's value). Fixed, each with a regression test:
- what-if quantities of 10 or more were sent to the engine as `1e+01` (`js_number` took the shortest `%g`); now plain notation as `Number#toString`;
- the lab table and the builder's leg lines rounded each leg's contracts (85,4 read +85);
- the debrief comparison's structure names were cut in their table cell at 1024 px: the cell keeps the short name and the rule is listed under the table.

### Playtest of the finished web build (as built)

The margin rule, the strategy comparison (m14 played, m15 without options), the builder's sizing aid (m16 iron condor at 1%), a covered call on a stopped long (m14) and the lesson what-if and builder widgets, played in PT-BR and en at 1024 and 1440 px with the numbers checked by hand (credit 1,37 × 88,4 = 121,11; maximum loss (2,5 − 1,37) × 88,4 = 99,89; spread margin 2,5 × 88,4 = 221; the covered call's loss to the stop (breakeven 104,14 − stop 103,10) × 44,7 = 46,49). Fixed, each with a regression test:
- a covered call or protective put on a held long planned the long's whole value as its risk ("one entry risked ~4,650 cr (> 2%)") although the long had its own stop: the what-if now takes the long's stop (engine `stop` on a perpetual leg, web `heldLong`);
- the builder defaulted to 1 contract with no way to size: a sizing aid from the engine's maximum loss;
- applying that size wrote no quantity into the legs (the derived size followed the new count), and its hint showed the old maximum loss while the engine re-analysed;
- leg quantities showed `88.4` in PT-BR; the comparison's "best here" tag touched the name; the lab's locked-slider label lost its space; lesson texts said "below" for widgets drawn above them.

- [x] X1 Same numbers: the playtest report for one m16 seed, played with the same legs on web and desktop, shows identical builder analysis, fills and debrief comparison.

  As built: m16 seed 3, iron condor sized by the sizing aid at 1% of equity (85,4 contracts; web from this worktree's build, desktop `tradelab_desktop_playtest_tests -tc="*X1*"`), calibration questions skipped on both. Same on both: the dry run at 1 contract (+1,0 P 97,50 @ 0,53; +1,0 C 105,00 @ 0,50; −1,0 P 100,00 @ 1,23; −1,0 C 102,50 @ 1,13; credit 1,33; fee 0,12; spread margin 2,50), breakevens 98,67 · 103,83, maximum profit 1,33 and loss 1,17, "Usar 85,4 contratos"; at 85,4 credit 113,58, fees 10,36, margin 213,50, maximum loss 99,92, the per-leg values, IVs and Greeks; the fills at bar 61 (0,53 / 0,50 / 1,23 / 1,13); the debrief (+103,22 cr, fees 10,36, spread and slippage 10,25, drawdown 30,86, process 58/100, the leg results −47,85 / −45,29 / +102,45 / +93,91); and the comparison (basis bar 60, 85,4 contracts; short strangle 60 → 90 +47,16 cr, drawdown 4,27, 0 calls; iron condor 60 → 90 +39,47 cr, 10,06, 0; the player's +103,22 best). Desktop defects found and fixed: quantities in exponent form in what-if requests, rounded leg labels.
- [x] X2 Root README ("What can the player predict?" for m13–m17, the features table, the curriculum), `docs/product-spec.md` (Strategies stage, Academy, glossary, tour), and the "As built" notes in this design.
- [x] X3 `openspec validate --all --strict`; cognitive complexity within targets (frontend ≤ 12, C++ ≤ 15); every earlier golden digest unchanged.
