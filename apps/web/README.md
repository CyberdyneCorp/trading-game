# TradeLab web client

SvelteKit 3 + Svelte 5 (runes), TypeScript strict, static build (`adapter-static`) with an offline service worker. The C++ engine runs as WebAssembly inside a Web Worker; the UI only ever receives the state up to the current step.

Behavior contract: `openspec/changes/add-web-client/` (web-client + terminal-ui specs). Deviations of the current slice are listed in its `design.md`.

## Run

Requires Node 22 and Emscripten (`brew install emscripten`, or emsdk with `emcmake` on `PATH`) for the engine build.

```bash
npm install
npm run engine:build   # compiles engine/ to WASM -> src/lib/engine/wasm/tradelab.mjs (gitignored)
npm run engine:size    # fails when that module is 400 KB gzipped or more (CI runs it)
npm run engine:types   # regenerates src/lib/engine/types.ts from engine/idl/tradelab.idl
npm run dev            # http://localhost:5173 ; append ?seed=123 for a reproducible market
npm run build && npm run preview   # production build incl. service worker (offline mode)
```

Rebuild the engine (`npm run engine:build`) after any change under `engine/`, and regenerate the types (`npm run engine:types`) after any change to `engine/idl/tradelab.idl`; CI fails on stale generated types.

## Test

```bash
npm run check      # svelte-check (app + service worker)
npm test           # Vitest unit tests (src/lib/game/*.test.ts)
npm run test:e2e   # Playwright: builds, serves on :4173 (override with E2E_PORT), runs e2e/ in Chromium and WebKit
npm run budget:size  # after a build: initial JS + WASM must stay under 1 MB gzipped
```

`PLAYWRIGHT_CHROMIUM_PATH=/path/to/chromium` reuses a local Chromium instead of `npx playwright install chromium` (WebKit: `npx playwright install webkit`). Playwright projects: `chromium` runs every spec except the deploy and budget specs; `webkit` runs `a11y`, `browser-support` and `storage` (Playwright's WebKit cannot emulate offline through a service worker); `deploy` runs `deploy.spec.ts` alone after both, because its simulated deploy (`scripts/previewDeploy.ts`, a `vite preview`-only plugin behind `/__e2e-deploy/<id>`) changes the service-worker script for every browser. `--project=chromium` runs one project. `budgets` runs `e2e/budgets.spec.ts` last and alone, so other workers don't compete for the CPU it measures. It holds the performance budgets (mission ready < 3 s, p95 advance latency < 100 ms, a 5,000-bar pack ready < 3 s and zooming at 60 fps); `CPU_THROTTLE=3` runs it on a slowed-down CPU to check the headroom. CI (`.github/workflows/web.yml`) runs engine build, check, unit, build, the size budget and E2E.

## Layout (MVVM)

```
src/
  routes/+page.svelte      single page; switches menu / briefing / terminal / debrief / curriculum / journal
  service-worker.ts        precache of the versioned build, offline shell, update on request
  lib/engine/              model bridge: engine.worker.ts (WASM host, loads an engine build by URL), client.ts, protocol.ts, types.ts (generated from
                           engine/idl: `npm run engine:types`; never edit by hand), defaults.ts, build.ts (this version's content-hashed
                           engine module), reveal.ts (reveal gate), wasm/
  lib/game/                view-models (session, autoplay, settings, pwa, i18n, audio, sound, csvImport — *.svelte.ts rune classes),
                           IndexedDB storage, packStore (OPFS packs, quota, eviction), engineCache (engine pinning),
                           browserSupport (unsupported-browser check), and pure helpers (sizing, stops, optionsMath, calibration,
                           analysis, debrief, format, news, hotkeys, tutorials, advisor, points, perks, curriculum, campaign, journal, candle/chart
                           pattern catalogs, patternOutcome and packs) with *.test.ts next to them
  lib/components/          views: Terminal, Chart (+ chartPanes.ts), OrderTicket (+ TrailField: trailing stop by price or percent), OptionTicket, OptionChain,
                           RightRail, TopBar, AdvisorPanel, Briefing (+ PerkPanel), Tutorial (+ TutorialArt), Debrief (+ RegimeReveal), MissionMenu, dialogs,
                           PatternSheet (+ PatternAnimation), HelpOverlay, CsvImport, CampaignPanel, CampaignOutcome,
                           CurriculumMap, Journal; chartDrag.ts (stop/target line drag)
e2e/                       Playwright: mission.spec.ts (gameplay), missions-batch-a.spec.ts (m06–m08 to the debrief), missions-batch-b.spec.ts (m09–m12 to the debrief, announced calendar, campaign report), campaign.spec.ts (campaign, curriculum, journal), perks.spec.ts (intel perks), engine-rules.spec.ts (percent trailing stop, margin tiers), engine-rules-v17.spec.ts (gaps filled with bars without trading, mark-price note),
                           packs.spec.ts (CSV import, practice packs), branch-pack.spec.ts (packer-built branch and synthetic packs),
                           offline.spec.ts (OFF01-web), storage.spec.ts (no future bars on the main thread or in storage, checkpoints),
                           updates.spec.ts (evicted pack, old-engine resume), deploy.spec.ts (update mid-mission),
                           a11y.spec.ts (axe), keyboard.spec.ts (keyboard-only play, screen-reader names), browser-support.spec.ts, budgets.spec.ts (performance); helpers.ts
scripts/                   check-budgets.mjs (size budget), check-wasm-size.mjs (WASM gzip budget), previewDeploy.ts (E2E deploy simulator, `vite preview` only),
                           build-engine.sh (Emscripten), make-icons.py (PWA icons)
static/                    manifest.webmanifest, icons
```

Game rules, fills, scoring and the debrief come from the engine; the seeded reveal of a built-in mission (regime spans, event outcomes) comes from `tl_mission_reveal` through the worker's `reveal` request, which the worker refuses while the run is active (`src/lib/engine/reveal.ts`, checked by `reveal.test.ts`), and the debrief draws it as a regime band, a signals-by-regime table (`RegimeReveal`) and a per-release table (`ReleaseReveal`: consensus, drawn outcome and size, gap, reaction, positioned before or after, implied against realized move); the client formats engine decimal strings for display and never computes authoritative state (ticket previews in `sizing.ts`/`optionsMath.ts` mirror engine formulas for display only). Runs persist as an append-only action log in IndexedDB (`tradelab` DB v3: `runs`, `actions`, `meta`, `packs`, `checkpoints`); every 20 actions the engine's checkpoint (its `RunState`, no scenario bars) is stored as the run's latest, with the option series seen so far (kind, strike and expiry, named in the shown locale; an expired series is no longer in the restored state), and resume restores it and replays only the actions after it (or the whole log when the checkpoint was written by another engine build, whose checkpoint format may differ, or is refused). A run record also keeps its start options (advice allowance, intel perks; campaign capital and leverage cap), its campaign link and its last equity, and `meta.progress` (version 2, `migrateProgress` reads older shapes) keeps the tutorials seen, the intel perks already explained, the advisor point balance (`points.ts`: start 3, cap 10, settled once per run id; a resumable run is settled, earning nothing, when another run is started), the campaign run, ended campaign runs and the missions assessed in a campaign (`campaign.ts`, `curriculum.ts`; a campaign mission is settled once per run id from the engine's final equity, and a left one from its last equity; a leg that reached its debrief also stores the engine's trade and cost facts, which `campaignReport.ts` adds up for the capstone's `CampaignReport`).

## Mission rules, lesson questions and assisted practice

The briefing renders the engine's `missionRules` (task 4.1): objectives (`objectives.ts`, PT-BR and en text per engine code, numbers in the locale), permissions and limits; the rail's liquidation warning uses the mission's distance, and the ticket (new order or protective stop) needs a ticked confirmation for a stop beyond liquidation when the mission asks for it. `LessonQuiz` shows the mission's lesson questions (`tl_lesson` through the worker's `lesson` request; texts in `lessons.ts`, checked against `engine/rubrics/lessons.json` by `lessons.test.ts`); each answer is a logged `lesson` action (an engine command), so perk toggles on the briefing replay it onto the restarted run. The debrief adds `LessonResults` (answers and key) and `DecisionStrip` (the engine's act/hold/wait records). `AssistPanel` (terminal and debrief) rewinds to a chosen bar (`rewindPrefix` in `assisted.ts` rebuilds the run from its own actions) or starts the mission on another seed; lesson answers given on the briefing are kept by every rewind, even one to the first decision; the new run record carries `assisted`, starts without the advisor (the terminal hides the advisor panel, as for practice packs), and is skipped by `finishedMissions`, points and campaign settlement. E2E: `e2e/scoring-v11.spec.ts`.

## Campaign, curriculum map and journal

The menu offers the campaign (missions in `campaignMissions()` order, capital carried through the engine's `startingBalance` start option, bankruptcy or less than `CAMPAIGN_MIN_CAPITAL` left ends the campaign run; ending it by choice asks first and leaves its mission in progress), free play (any mission, its own rules), the curriculum map (`CurriculumMap.svelte`: stage statuses from `curriculum.ts`, progress summary) and the journal (`Journal.svelte`: every run's plan entries from the `runs` store, newest first, filterable by mission; a run replaced by another before its end is stored as `left`). The curriculum's unlock rules are data in `curriculum.ts`; in a campaign mission a locked "leverage above 2×" becomes the engine's `leverageCap` start option, and the briefing lists what stays locked and which stage unlocks it.

## Intel perks

`perks.ts` holds the rules: Economic calendar at 1, Fast wire I at 2 and Fast wire II at 4 distinct built-in missions finished (`GameSession.finishedCount`, from the runs played to the end; imported packs do not count). A free-play run starts with every unlocked perk on; `PerkPanel` on the briefing toggles them (`GameSession.setPerk` restarts the not-yet-played run on the same seed with the new `newsLead`/`calendar` start options and updates its record), shows each locked perk's requirement, and explains a newly unlocked perk once (`progress.perksIntroduced`, written when the terminal opens). Campaign missions and pack runs start without perks. `NewsPanel` shows the announced calendar (`upcoming`: kind, bar and consensus, for every player), early headlines (EARLY, "public in N bars") and the calendar perk; auto-advance pauses when a headline arrives early (`newsArrived`). The debrief and the journal label runs whose start options used a perk "with intel" (`perksUsed`, `withIntel`).

## Imported markets (CSV and packs)

"Import CSV" in the menu opens a wizard: pick a file, confirm what the engine cannot know (asset, pair, venue, bar interval, timestamp unit, volume unit, leverage cap; the unit and interval are guessed from the first rows), validate, then create a practice mission. The engine (`tl_csv_validate`, `tl_pack_build_csv`) does all the checking, so the errors are the same on every client; the wizard only translates their codes (`csvIssueText`, `csvMetaText` for refused declared values, `packReasonText` for build refusals). The worker hands the CSV text to the engine through the WASM heap (`withText` in `engine.worker.ts`), not as a `ccall` string, which Emscripten copies onto its 64 KB stack; any string argument that can grow past a few KB must do the same. If an engine call traps, the worker drops the module and loads a fresh one on the next request. The result is a private `.tlpack`, masked to a price base of 100 and signed by this device's key, whose seed is created once and kept in the `meta` store. Packs are verified by the engine before they are installed by digest (file in OPFS, index in the `packs` store; see below) and again when a run starts or resumes. A run on a pack records `packDigest`, and resuming replays its log on that pack. "Load pack" installs a `.tlpack` file only if it is signed by this device or by a key pinned in `PINNED_PUBLISHER_KEYS` (`src/lib/game/packs.ts`, empty until there is a publisher key). Mid-run, the UI shows these markets as KNOWN and never shows the real asset or prices; the debrief shows the reveal from the engine (the CSV-import source and transformation note are translated by `revealTexts` in `src/lib/game/debrief.ts`). A synthetic pack (`marketMode` `synthetic`, e.g. from `packer generate`) has no real market behind it: the briefing calls it a synthetic pack and the debrief says it was generated, with no transformation note. "Play again" after a pack run starts the same pack on a new seed (`GameSession.playAgain`). Practice runs start without the advisor and never change the advisor point balance. When the report lists gaps, the wizard offers to join the bars (default) or fill each gap with bars without trading (engine policy v17, metadata `gapFill`: indicators skip them or carry the last close forward); the chart draws them as shaded slots with a dashed line at the carried close, the readout and the accessible table say "no trading", and a legend counts them with the gap policy in its tooltip. The briefing states whether positions are marked at the last price (`contract.markFromLastPrice`), and the option ticket refuses a series inside its settlement window (`settling`, reason `series_settling`).

## Offline storage, updates and supported browsers

- **Packs** are installed by digest: the bytes go to the Origin Private File System (`packs/<digest>.tlpack`), the index to the IndexedDB `packs` store (bytes stay in the index where OPFS cannot write files). The practice section shows the space used and available (`storage.estimate()`); an install that does not fit is refused with the space missing. On every start each index entry is checked against its file: a missing or truncated file (evicted by the browser) is shown as "not on this device" with how to get it back, and neither a new run nor a resume starts from it (`packStore.ts`).
- **Engine pinning:** the engine is a content-hashed module (`_app/immutable/assets/tradelab.<hash>.mjs`). A run records the build it started on and the page copies that module into the `tradelab-engines-v1` cache, which the service worker keeps when a new version activates. After an update an unfinished run resumes on its own build, also offline, and the next run starts on the new one; builds no unfinished run needs are deleted when the menu refreshes (`engineCache.ts`). A run whose build is gone resumes on the current engine with a notice.
- **Updates** install in the background and are offered only in the menu ("Nova versão disponível"), never mid-mission.
- **Unsupported browsers:** without WebAssembly or Ed25519 in WebCrypto the page shows which feature is missing instead of the game (`browserSupport.ts`).
