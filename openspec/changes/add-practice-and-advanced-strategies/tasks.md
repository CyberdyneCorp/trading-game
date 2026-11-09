# Tasks: add-practice-and-advanced-strategies

The work comes in three waves (design D1). Within a wave, parallel builders can own these sections:
- **E (engine, bindings and API):** independent of the clients.
- **W (web):** owns the content files.
- **D (desktop):** follows W's content and E's calls.
- **X (playtest and cross-check):** closes the wave.

Dependencies are written as "needs X". Each task ships its tests and updates the READMEs, `docs/product-spec.md` and this change (an "As built" note in the design when the build departs from it) in the same PR. A task is ticked only when it is fully done. Every bug fix found on the way gets a regression test in the same commit.

Common rules:
- Cognitive complexity: frontend ≤ 12, C++ ≤ 15 (engine clang-tidy; desktop `apps/desktop/scripts/check_complexity.sh`).
- Linux CI builds with clang and libstdc++: include `<algorithm>` and friends, and give `std::max`/`std::min` arguments of the same type.
- Web e2e runs on a unique `E2E_PORT` in 4800–4899.
- Disk: one desktop build directory at a time; delete build directories, captures and test results when a task ends.

Suggested PRs, in order:
1. W1-E1–E3 (policy v18);
2. W1-E4–E9;
3. W1-W and W1-D in parallel;
4. W1-X;
5. W2-E;
6. W2-W and W2-D;
7. W2-X;
8. W3-E;
9. W3-W and W3-D;
10. W3-X.

## Wave 1: builder and terminal

### W1-E. Engine, bindings and API

- [x] W1-E1 Time in force, execution policy v18 (design D6, D8):
  - IDL: `Reason` 38–40; `OrderView.tif`, `tifBars`, `expireStep`; optional `tif`/`tifBars` on `PlaceOrderCommand` and `PlaceOptionOrderCommand`; regenerate with `engine/idl/gen.py` (`--check` clean);
  - `Order` fields, the command checks (`invalid_time_in_force`) and the advance rules: `bars` expiry after the close phase, `ioc`/`fok` only in the next open phase (perpetual and option), FOK all or nothing after the orders served before it;
  - C ABI `tl_place_order_tif` and `tl_place_option_order_tif` (`kAbiVersion` stays 1; `c_api.h` docs);
  - checkpoint format 6 (appended fields; format 5 refused);
  - the state digest appends the fields;
  - run-file actions `tif`/`tifBars` (`src/run_file.cpp` schema and the shared applier).

  Tests (`tests/test_policy_v18.cpp`):
  - each row of the D6 table;
  - every refusal;
  - an IOC limit that the open does not reach, which is not filled intrabar;
  - a FOK limit with too little capacity left fills nothing;
  - a bracket entry with `bars` gets `gtc` children;
  - an option IOC limit at the open.
- [x] W1-E2 Player-built OCO groups, needs E1 (design D7):
  - `LinkOco` (CommandType 11), `Order.link_group`, `tl_link_oco`, the run-file action `{"k":"oco","a","b"}`, `OrderView.linkGroup`;
  - first-fill cancels the sibling (`oco_sibling_filled`), and reduce-only exit pairs reduce the sibling;
  - same-bar priority by phase, then v16 order;
  - dissolved by a cancel or an expiry, kept by `MoveOrder`;
  - `invalid_oco` refusals.

  Tests:
  - a buy-stop and sell-stop pair on one wide bar fills exactly one;
  - a perpetual and option pair;
  - a partial reduce-only exit;
  - a linked bracket parent keeps its children's group.
- [x] W1-E3 Policy v18 goldens and history, needs E1 and E2:
  - regenerate the frozen state digests with `tools/regen_golden.py --write` and confirm WASM matches;
  - a property test that every existing golden's **event** digest under v18 equals its v17 value;
  - new run golden `m01-tif-oco` (listed in `index.json`; `tools/make_run_files.py` adds its run file);
  - the engine README "Policy v18" section and the golden history line;
  - the `kExecutionPolicyVersion` comment.
- [x] W1-E4 What-if extensions, report-only (design D4):
  - `horizon` (multi-expiry analytics at the first expiry: the tick-grid bisection for breakevens, `unboundedAbove`);
  - `heatmap` (capped at 61 × 25);
  - `holdings: true` with the `risk` block (`deltaUnits`, `deltaNotional`, `gamma1pct`, `vegaPoint`, `thetaBar`, `fundingNext`, the `*Bp` fields, `limits`);
  - IDL reply shapes.

  Tests:
  - breakevens against a brute-force tick scan;
  - calendar and diagonal bounds;
  - the heatmap at time 0 equals `today`;
  - `holdings` equals the explicit legs;
  - OPT06 no look-ahead extended to the new blocks;
  - purity.

  New fixtures `whatif-calendar` and `whatif-heatmap`, replayed by every golden reader.
- [x] W1-E5 `tl_combo_preview` limit legs (`type`, `premium`, `tif` per leg; optional request fields). Tests: it matches a real placement leg by leg for limit legs and each TIF, and leaves state and digest unchanged.
- [x] W1-E6 Calendar margin flag (design D5): `OptionSpec.calendar_margin` (valid only with `spread_margin`), cross-expiry pairing order, `missionRules.calendarMargin`.

  Property tests:
  - a calendar short needs 0;
  - a diagonal needs ≤ the strike gap × units;
  - an earlier-expiring long never covers;
  - pairing is order-independent;
  - with the flag off, `option_book` is identical on m13–m17.
- [x] W1-E7 `MissionRules.risk_limits` (`deltaBp`, `gammaBp`, `vegaBp`, `thetaBp`, all optional) in the mission schema and the state view, plus the `limits` flags in the what-if `risk` block. Test: missions without limits serialize as before.
- [x] W1-E8 Bindings, needs E1–E7:
  - WASM exports and worker requests (`placeOrder` with `tif`, `linkOco`, `runWhatif` with `horizon`/`heatmap`/`holdings`), feature-detected so an older pinned build hides the new controls;
  - Python `Run.place_order(..., tif=, tif_bars=)`, `Run.place_option_order(..., tif=)`, `Run.link_oco`, the what-if options;
  - parity tests;
  - a log with `roll`/`adjust` tags replays to the untagged digest (Python and WASM).
- [x] W1-E9 API and policy cache, needs E3:
  - `services/api` replays v18 logs (the new actions pass its validation);
  - `tools/build_policy_cache.py` builds v17;
  - `tests/test_policies.py` replays a v17 run under the cache and a v18 run under the current engine;
  - the OpenAPI snapshot is unchanged (or updated and explained);
  - the API README notes the policy window (v16–v18 with the default 3).

### W1-W. Web

- [x] W1-W1 Time in force in the tickets (perpetual and option), needs E8: GTC, "day" (N = 86,400 / `stepSeconds`), N bars, IOC and FOK where allowed (disabled with the reason elsewhere). The orders panel shows each order's TIF and expiry bar, and the event feed names `time_in_force` and `invalid_time_in_force`.

  Unit tests for the view model and reason texts. E2E: an IOC limit that does not fill.
- [x] W1-W2 OCO linking, needs E8: select two working orders in the orders panel, then "Link OCO" with a confirmation; a link mark on both orders and on their chart lines; `invalid_oco` explained; the event feed names the sibling cancel.

  E2E: a breakout pair where one fills and the other is cancelled.
- [x] W1-W3 Trailing stop, easier to find and taught (design D10):
  - the Stop · Trailing · Target tabs;
  - "Protect with: fixed stop / trailing stop" in the new-order ticket, with `plan.trail`;
  - the planned-trailing notice after the fill, never sent without the click;
  - the chart label with the rule, the trigger and the best-close marker;
  - a new `orders-execution` step with a `trail` check (Vitest against `stops.ts`; `test_academy_numbers.py` through a scripted engine run);
  - the `trailing-stop` glossary example with a check;
  - m07's card links the term.

  E2E: a planned trailing stop placed after the fill.
- [x] W1-W4 Builder limit legs and combo states, needs E5 and E8 (design D2):
  - per-leg market or limit (default mid rounded toward the passive side) and one TIF per combo (IOC default);
  - the analysis at the limit premiums;
  - the `working`, `complete`, `incomplete` and `void` states, with the working notice ("Cancel working legs") and the existing incomplete notice.

  Unit tests with a fake worker.
- [x] W1-W5 Multi-expiry legs, needs E4: free legs on up to two expiries; calendar and diagonal templates and their reverse versions; the `horizon` analysis labeled "at the first expiry, at today's implied volatility level"; the payoff chart with the horizon curve.

  E2E: a limit calendar built.
- [x] W1-W6 Roll and adjust, needs W4 and W5 (design D3):
  - "Roll" on an option position or combo (next or chosen expiry, same or re-centred strikes);
  - "Close leg" and "Add hedge leg" on a live combo;
  - the one ordering rule (buys, perpetual, sells), the combo id kept, `roll` and `adjust` tags;
  - the what-if of the holdings after the change, and a confirmation when a short becomes uncovered;
  - journal and debrief grouping keep one combo row.

  E2E: the calendar rolled, then adjusted with a hedge leg.
- [x] W1-W7 P&L heatmap, needs E4: a panel in the builder and the terminal lab (price × bars to the last expiry); a diverging palette checked in the colorblind setting; the value of the focused or hovered cell; a table view for screen readers; axe.
- [ ] W1-W8 Portfolio view and risk limits, needs E4 and E7 (design D9):
  - the panel and rail summary from `holdings`;
  - warnings for exceeded limits (never blocking);
  - the briefing lists risk limits;
  - "Hedge delta" (a −`deltaUnits` perpetual ticket rounded toward zero, the plan dialog, a confirmation).

  E2E: a risk-limit warning and a hedge ticket.
- [x] W1-W9 Web README (tickets, OCO, builder, heatmap, portfolio, trailing), new glossary terms used by wave 1 (`time-in-force`, `ioc`, `fok`, `oco`, `calendar-spread`, `diagonal-spread`, `heatmap`, `portfolio-greeks`, `risk-limit`, `delta-hedge`), and the e2e list.

### W1-D. Desktop

- [x] W1-D1 Engine adapter, needs E8: the new C calls and what-if fields on the `EngineHost` worker; `storage::Action` gains `tif`, `tifBars` and the `oco` action (replayed); the new fixtures in `tradelab_desktop_golden_tests`; the v18 goldens.
- [x] W1-D2 Terminal: the TIF selector, "Link OCO" in the orders panel with link marks, the Stop · Trailing · Target tabs, "Protect with", the planned-trailing notice, the chart's trailing label and best-close marker. Viewmodel tests from the web vectors; a headless keyboard flow.
- [x] W1-D3 Builder: limit legs, combo states, two expiries, calendar and diagonal templates, roll and adjust, with ports of `combos`, `roll` and the ordering rule checked by web parity fixtures; a headless flow.
- [x] W1-D4 Heatmap and portfolio panels (scene painter grid, colorblind palette, help-line value, table view for screen readers; warnings; hedge ticket), plus the briefing's risk limits.
- [ ] W1-D5 Windowed captures for every new panel on macOS and Linux (lavapipe; say so if docker is unavailable); reference images regenerated only for intended changes (`TRADELAB_UPDATE_REFERENCES=1`, revert unchanged rewrites); desktop README.

### W1-X. Playtest and cross-check

- [x] W1-X1 Same numbers: one m16 seed and one m17 seed, played with the same actions on web and desktop, must give identical results for:
  - an IOC limit condor;
  - an OCO pair;
  - a calendar on m17 built, rolled and adjusted;
  - the heatmap cells at three points;
  - the portfolio totals;
  - the final digests.

  Recorded in the design ("As built: wave 1 cross-check and playtest"): an IOC limit condor (nothing filled) and an OCO pair on m16, a calendar built, rolled and hedged on m17, its heatmap cells, the portfolio totals, the debriefs and both final digests identical.
- [x] W1-X2 Playtest of both clients (PT-BR and en; web at 1024 and 1440 px, desktop at 1280x800 and 1024x768): every new control as a new player, the numbers checked by hand (a calendar's debit and horizon breakevens, a roll's net premium, `gamma1pct` from the shown gamma, a hedge quantity). Fixes come with regression tests.
- [x] W1-X3 `openspec validate --all --strict`; complexity within targets; only the documented state-digest change, with event digests unchanged; root README, `docs/product-spec.md` (orders, builder, portfolio) and the engine README.

## Wave 2: missions m18–m25

### W2-E. Engine

- [x] W2-E1 Generator specs and missions m18–m25 (design D11, D12) in `missions.cpp`, needs W1-E3, W1-E6 and W1-E7:
  - existing capabilities only (`kGeneratorVersion` stays 5, or a recorded G5 decision);
  - chains, margin flags, risk limits, objectives (new `ObjectiveCode`s), reveal labels;
  - one golden each, with every earlier golden's event digest unchanged;
  - rubrics (two questions each, `reviewed: false`) with the D12 numbers.
- [x] W2-E2 Learnability and realism, needs E1: `tests/test_mission_learnability_d.cpp` with the D12 bands (reference players in `tests/learnability.hpp`: band hedger, carry holder at 2× and 5×, event straddle timings, calendar, managed exits, cooled-down / planned / single-entry players and their trap counterparts), realism pooled over 25 missions, `tools/market_study.py` rows. Record tuning as "As built"; bands are never widened silently.
- [x] W2-E3 Scoring v14, report-only (design D13), needs E1:
  - `revenge_trade`, `fomo_entry` and `averaging_down` findings with evidence;
  - `debrief.episodes`;
  - `risk_limit_exceeded`;
  - the comparison rules `delta_hedge`, `enter_before_release`/`exit_before_release`, short perpetual legs, multi-expiry legs, `managed_exit`, with declarations for m18–m22.

  Tests:
  - each rule fires on a scripted run and stays silent on a near miss;
  - no look-ahead;
  - comparison rows reproduce scripted runs;
  - every existing golden's score is byte-identical.
- [x] W2-E4 IDL and bindings for the scoring v14 shapes (`FindingCode`s, `ComparisonStrategy`s, episodes); Python tests; engine README (scoring v14, m18–m25, history line).

### W2-W. Web

- [x] W2-W0 Progress v4 (design D20), with every key: the campaign `order` snapshot, `drillReviews`, `drillHistory`, `dailyChallenges`, `guessHistory`; `migrateProgress` from v1–v3. Unit tests: migration, and an active pre-v4 campaign keeps its 17-mission order.
- [x] W2-W1 Curriculum (design D14), needs W0:
  - the `management`, `advanced` (elective, `campaign: false`) and `psychology` stages;
  - the 21-mission campaign order;
  - elective assessment from finished non-assisted free-play and daily runs;
  - the map draws branches.

  Unit tests:
  - no passed stage becomes locked;
  - no lock the engine cannot enforce;
  - elective missions are never campaign legs.
- [x] W2-W2 Lessons (design D12 lesson bullets):
  - `vol-gamma-scalping`, `carry-funding`, `vol-events`, `options-calendars`, `management-scaling` and `mind-biases`;
  - PT-BR and en;
  - new check kinds `hedge`, `scalp`, `calendar`, `scale` (with `trail` from W1-W3);
  - option numbers only as engine `whatif` checks (`academyEngine.test.ts`, `test_academy_numbers.py`);
  - no mission id.
- [x] W2-W3 Glossary terms (about 24): `gamma-scalping`, `delta-neutral`, `rehedge`, `carry`, `basis`, `synthetic-forward`, `put-call-parity`, `implied-move`, `event-volatility`, `term-structure`, `scale-in`, `scale-out`, `breakeven-stop`, `partial-take-profit`, `averaging-down`, `cooldown` and the wave 1 terms not yet added; examples with checks.
- [x] W2-W4 Missions on the web, needs W2-E1 and W2-E3:
  - `missions.ts` texts;
  - tutorial cards with the new art (`scalp`, `carry`, `parity`, `crush`, `calendar`, `scale`, `trail`, `cooldown`), 3–5 cards each, no spoiler words;
  - `lessons.ts` texts checked against the rubrics;
  - the briefing's risk limits and calendar margin rule;
  - the debrief's bias findings with evidence, the episodes view and the comparison rows.

  E2E: each of m18–m25 played to the debrief (`e2e/missions-batch-d.spec.ts`).
- [x] W2-W5 Web README (missions, stages, progress v4).

### W2-D. Desktop

- [x] W2-D1 Generator and content: new lessons, glossary terms, the stages' structure and the campaign order into `strings.csv`/`content.json`; drift check.
- [x] W2-D2 Progress v4 in the SQLite progress row (a v3 row reads with defaults; an active campaign keeps its order).
- [x] W2-D3 Missions m18–m25: menu, campaign, curriculum branches, briefing (risk limits, calendar margin), tutorials with the new art as display lists (`viewmodel/game/tutorial_art`, no label crossing a line in either locale), debrief (biases with evidence, episodes, comparison); headless and windowed flows; the playtest extended to 25 missions.
- [ ] W2-D4 Windowed reference captures on macOS and Linux; desktop README.

### W2-X. Playtest and cross-check

- [x] W2-X1 Same numbers: m18–m25 at seed 3 with the same actions on both clients (dry runs, fills, debrief, findings with evidence, episodes, comparison). Recorded in the design.
- [x] W2-X2 Playtest each mission with a sensible and a reckless approach in both locales; finance checked by hand (a rehedge quantity, a day's funding, the straddle breakevens and implied move, a calendar's debit, the remaining risk after a partial exit, the bias evidence numbers); no spoiler in a title, briefing, card or lesson.
- [x] W2-X3 Root README ("What can the player predict?" for m18–m25, the features table, the curriculum), `docs/product-spec.md` (curriculum, campaign, debrief), `openspec validate --all --strict`, complexity.

## Wave 3: practice formats

### W3-E. Engine

- [ ] W3-E1 Coach review (design D17), needs W2-E3:
  - `tl_coach_review(run)` (refused while active), `CoachMomentCode` in the IDL, Python `Run.coach_review`, worker `coach`;
  - replay of the log on a fresh state with rules at each step from the state of that step only;
  - at most 9 warn and 3 good moments.

  Tests:
  - each code on a scripted run;
  - no look-ahead (bars after `s` altered);
  - purity.

  A new fixture kind `coach` (two fixtures, listed in `index.json`, replayed by native, WASM, Python and the desktop); the engine README fixture table.

  Status: done on the engine, WASM and Python; open only for the desktop's golden reader (W3-D1), which still refuses the kind `coach`.
- [x] W3-E2 Round markets `r01-trend`, `r02-range`, `r03-breakout`, `r04-event` (design D19): existing capabilities, `"practice": "round"` in `tl_missions`, one golden each, realism bands pooled with the missions; menus and the daily pool skip them (a test).

### W3-W. Web

- [x] W3-W1 Daily drills (design D15), needs W2-W0:
  - `content/drills/*.json` with the schema validator;
  - mulberry32/FNV-1a generation;
  - the choice of items from due lesson reviews, then `drillReviews`, then weak skills;
  - a soft two-minute timer with a "no timer" setting;
  - the end screen with lesson links.

  Tests:
  - 200 sessions per template checked by the verifier;
  - `test_drill_numbers.py` for option kinds;
  - numbers in `explain` texts come from params or the answer;
  - scheduling with a fake clock;
  - parity fixture `drills.json`.

  E2E: a session.
- [x] W3-W2 Daily challenge (design D16), needs W2-W0: date-to-mission-and-seed (`DAILY_POOL_V1`, parity fixture `daily.json` over 400 dates), the run with no advisor or perks and the `daily` link, the first and best attempt, the 90-day history, no streak.

  E2E: two attempts on a mocked date.
- [x] W3-W3 Guess rounds (design D19), needs W3-E2 and W2-W0: the round flow (commit, then reveal), `guessRound.ts` integer scoring (parity fixture `guess.json`), "one sample", the calibration table after 20 rounds, `guessHistory`.

  E2E: five rounds.
- [x] W3-W4 Coach mode (design D17), needs W3-E1: "Review with the coach" from the debrief, the chart cut at the moment's bar, moment texts per code (PT-BR, en), "Retry from here" on warn moments, axe.
- [x] W3-W5 Mistake replay (design D18), needs W4: "Retry from here" on findings with a step and on coach moments; the assisted rewind with link `mistake`; the terminal banner; the repeated or avoided comparison at the retry's debrief; excluded from progress, points, campaign and journal statistics (tests).

  E2E: a mistake retried with the assisted label.
- [x] W3-W6 Menu entries (Drills, Daily, Guess), web README, e2e list.

### W3-D. Desktop

- [ ] W3-D1 Engine adapter: `tl_coach_review`, the round markets and the `coach` fixtures.
- [ ] W3-D2 Drills, Daily challenge and Guess screens, with ports of `drills`, `daily` and `guessRound` checked by web parity fixtures; keyboard and screen reader; headless flows.
- [ ] W3-D3 Coach overlay on the debrief and mistake replay (assisted link `mistake`, banner, comparison); headless flows.
- [ ] W3-D4 Windowed captures on macOS and Linux; the playtest extended to the practice formats; desktop README.

### W3-X. Playtest and cross-check

- [ ] W3-X1 Same numbers on both clients:
  - the drill items of one date and session;
  - the daily mission and seed for 10 dates;
  - five guess rounds' scores;
  - the coach moments and a mistake retry of an m23 run (seed 3).

  Recorded in the design.
- [ ] W3-X2 Playtest as a new player in both locales: a drill with wrong answers (the explanations checked by hand), the daily challenge twice, ten guess rounds (scores recomputed by hand), coach mode on a reckless run (each moment checked against the chart up to its bar: no future used), a retry that avoids the mistake and one that repeats it.
- [ ] W3-X3 Root README (practice formats, progress v4), `docs/product-spec.md` (game modes, metrics), `openspec validate --all --strict`, complexity, every earlier golden unchanged except the documented v18 state digests; build directories, captures and test results deleted.
