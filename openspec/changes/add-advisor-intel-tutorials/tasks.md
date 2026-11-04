# Tasks: add-advisor-intel-tutorials

Delivered in three PRs, in this order: tutorials (client only), advisor and points, intel perks.

## 1. Mission tutorials (PR 1, web)
- [x] 1.1 Tutorial cards per mission in PT-BR and EN (no outcome reveal), in `tutorials.ts` to keep `missions.ts` readable; unit tests (3–5 cards, both locales, ≤ 3 sentences, no spoiler words). The advisor/points card for m01 ships with the advisor (2.7) so no card describes a control that does not exist yet
- [x] 1.2 `Tutorial` screen and `TutorialArt` inline SVG illustrations (chart, ticket, risk, news, plan, funding, trend, leverage, volatility, margin, payoff, chain, decay); keyboard: arrows/Enter, Esc skips
- [x] 1.3 IndexedDB `meta.progress` record with `tutorialsSeen` and unit tests (`advisorPoints` and `settledRuns` were added with 2.7)
- [x] 1.4 Session flow: tutorial before the first briefing, skip, replay from the briefing, remembered across reloads; e2e tests
- [x] 1.5 README: player flow and features table

## 2. Advisor and points (PR 2, engine + bindings + web)
- [x] 2.1 Engine `RunOptions` on `start_run` (defaults keep m01/m05 event digests; their state digests change only through the version and the appended default fields); policy v11; checkpoint format 2
- [x] 2.2 `advisor.hpp/.cpp`: `advise()` per design D2, visible data only; property test for no look-ahead (later bars, unpublished funding and news beyond the lead altered, 5 missions × 4 seeds, leads 0 and 2)
- [x] 2.3 `RequestAdvice` command, `NoAdviceLeft` (rejected before anything is recorded: digest unchanged), `RunState.advice`; allowance tests; new m02 golden with advice (`test_advisor.cpp`)
- [x] 2.4 `advisor_points_earned` in the score report (scoring v6 after merging with the debrief work, which took v4 and v5; with each read's outcome and the hit rate); formula and cap tests
- [x] 2.5 Advisor realism test in `test_mission_realism.cpp` (45–65% per mission, wait ≥ 20%, confidence within 8 points); `tools/market_study.py` prints the advisor rows
- [x] 2.6 C API (`tl_run_create_ex`, `tl_request_advice`), WASM worker protocol and Python binding (`RunOptions`, `Run.request_advice`, `replay(..., options)`, action `{"k":"advice"}`): start options, advice command, advice in state and debrief; binding parity tests (Python repeats the m02 advisor golden). The API service still replays with default options (3.3)
- [x] 2.7 Web: m01 tutorial card introducing the advisor and points (replaces the plan card, which it now includes, to stay at 5 cards); advisor button with points, advice panel (localized reasons, "apply stop/size"), debrief advice table; points settlement once per run; unit and e2e tests
- [x] 2.8 README ("What can the player predict?" gets the advisor table; how points work); engine docs

## 3. Intel perks (PR 3, engine + web)
- [ ] 3.1 Engine: `news_lead` visibility with `early` flag, calendar list in the state view; tests that bars are identical with and without perks
- [ ] 3.2 Web: perk unlocks from distinct finished missions, briefing toggles (locked perks show their requirement), perk tutorial/briefing card, "with intel" label in the debrief and run record; unit and e2e tests
- [ ] 3.3 Backend re-simulation accepts stored start options (Python binding); test
- [ ] 3.4 README and market-study note on how much Fast wire changes news trades
