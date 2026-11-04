# Tasks: add-advisor-intel-tutorials

Delivered in three PRs, in this order: tutorials (client only), advisor and points, intel perks.

## 1. Mission tutorials (PR 1, web)
- [ ] 1.1 `missions.ts`: 3–5 tutorial cards per mission in PT-BR and EN (no outcome reveal); m01 introduces the advisor and points
- [ ] 1.2 `TutorialCard` component with inline SVG illustrations (`chart`, `ticket`, `news`, `funding`, `options`, `advisor`, `risk`)
- [ ] 1.3 IndexedDB `meta.progress` record (`tutorialsSeen`, `advisorPoints` = 3, `settledRuns`) with unit tests
- [ ] 1.4 Session flow: tutorial before the first briefing, skip, replay from the briefing; e2e tests
- [ ] 1.5 README: player flow; spec tasks updated

## 2. Advisor and points (PR 2, engine + bindings + web)
- [ ] 2.1 Engine `RunOptions` on `start_run` (defaults keep m01/m05 goldens); policy v11
- [ ] 2.2 `advisor.hpp/.cpp`: `advise()` per design D2, visible data only; property test for no look-ahead
- [ ] 2.3 `RequestAdvice` command, `NoAdviceLeft`, `RunState.advice`; allowance tests; new m02 golden with advice
- [ ] 2.4 `advisor_points_earned` in the score report; formula and cap tests
- [ ] 2.5 Advisor realism test in `test_mission_realism.cpp` (45–65% per mission, wait ≥ 20%, confidence within 8 points); `tools/market_study.py` prints the advisor row
- [ ] 2.6 C API, WASM worker protocol and Python binding: start options, advice command, advice in state and debrief; binding parity tests
- [ ] 2.7 Web: advisor button with points, advice panel (localized reasons, "apply stop/size"), debrief advice table; points settlement once per run; unit and e2e tests
- [ ] 2.8 README ("What can the player predict?" gets the advisor row; how points work); engine docs

## 3. Intel perks (PR 3, engine + web)
- [ ] 3.1 Engine: `news_lead` visibility with `early` flag, calendar list in the state view; tests that bars are identical with and without perks
- [ ] 3.2 Web: perk unlocks from distinct finished missions, briefing toggles (locked perks show their requirement), perk tutorial/briefing card, "with intel" label in the debrief and run record; unit and e2e tests
- [ ] 3.3 Backend re-simulation accepts stored start options (Python binding); test
- [ ] 3.4 README and market-study note on how much Fast wire changes news trades
