# TradeLab web client

SvelteKit 3 + Svelte 5 (runes), TypeScript strict, static build (`adapter-static`) with an offline service worker. The C++ engine runs as WebAssembly inside a Web Worker; the UI only ever receives the state up to the current step.

Behavior contract: `openspec/changes/add-web-client/` (web-client + terminal-ui specs). Deviations of the current slice are listed in its `design.md`.

## Run

Requires Node 22 and Emscripten (`brew install emscripten`, or emsdk with `emcmake` on `PATH`) for the engine build.

```bash
npm install
npm run engine:build   # compiles engine/ to WASM -> src/lib/engine/wasm/tradelab.mjs (gitignored)
npm run dev            # http://localhost:5173 ; append ?seed=123 for a reproducible market
npm run build && npm run preview   # production build incl. service worker (offline mode)
```

Rebuild the engine (`npm run engine:build`) after any change under `engine/`.

## Test

```bash
npm run check      # svelte-check (app + service worker)
npm test           # Vitest unit tests (src/lib/game/*.test.ts)
npm run test:e2e   # Playwright: builds, serves on :4173 (override with E2E_PORT), runs e2e/ in Chromium
```

`PLAYWRIGHT_CHROMIUM_PATH=/path/to/chromium` reuses a local Chromium instead of `npx playwright install chromium`. CI (`.github/workflows/web.yml`) runs engine build, check, unit, build and E2E.

## Layout (MVVM)

```
src/
  routes/+page.svelte      single page; switches menu / briefing / terminal / debrief / curriculum / journal
  service-worker.ts        precache of the versioned build, offline shell, update on request
  lib/engine/              model bridge: engine.worker.ts (WASM host), client.ts, protocol.ts, types.ts, reveal.ts (reveal gate), wasm/
  lib/game/                view-models (session, autoplay, settings, pwa, i18n, audio, sound, csvImport — *.svelte.ts rune classes),
                           IndexedDB storage, and pure helpers (sizing, stops, optionsMath, calibration,
                           analysis, debrief, format, news, hotkeys, tutorials, advisor, points, perks, curriculum, lessons, catalog, download, campaign, journal, candle/chart
                           pattern catalogs, patternOutcome and packs) with *.test.ts next to them
  lib/components/          views: Terminal, Chart (+ chartPanes.ts), OrderTicket, OptionTicket, OptionChain,
                           RightRail, TopBar, AdvisorPanel, Briefing (+ PerkPanel), Tutorial (+ TutorialArt), Debrief (+ RegimeReveal), MissionMenu, dialogs,
                           PatternSheet (+ PatternAnimation), HelpOverlay, CsvImport, CampaignPanel, CampaignOutcome,
                           CurriculumMap, Journal, LessonRunner (+ LessonChart), Catalog; chartDrag.ts (stop/target line drag)
e2e/                       Playwright: mission.spec.ts (gameplay), missions-batch-a.spec.ts (m06–m08 to the debrief), missions-batch-b.spec.ts (m09–m12 to the debrief, announced calendar, campaign report), campaign.spec.ts (campaign, curriculum, journal), perks.spec.ts (intel perks),
                           packs.spec.ts (CSV import, practice packs), catalog.spec.ts (catalog, resumable download, Academy lesson),
                           offline.spec.ts (OFF01-web)
scripts/                   build-engine.sh (Emscripten), make-icons.py (PWA icons), build-sample-catalog.py (sample pack catalog)
static/                    manifest.webmanifest, icons, catalog/ (catalog.json + packs/<digest>.tlpack)
```

Game rules, fills, scoring and the debrief come from the engine; the seeded reveal of a built-in mission (regime spans, event outcomes) comes from `tl_mission_reveal` through the worker's `reveal` request, which the worker refuses while the run is active (`src/lib/engine/reveal.ts`, checked by `reveal.test.ts`), and the debrief draws it as a regime band, a signals-by-regime table (`RegimeReveal`) and a per-release table (`ReleaseReveal`: consensus, drawn outcome and size, gap, reaction, positioned before or after, implied against realized move); the client formats engine decimal strings for display and never computes authoritative state (ticket previews in `sizing.ts`/`optionsMath.ts` mirror engine formulas for display only). Runs persist as an append-only action log in IndexedDB (`tradelab` DB: `runs`, `actions`, `meta`, `packs`); resume replays the log through the deterministic engine. A run record also keeps its start options (advice allowance, intel perks; campaign capital and leverage cap), its campaign link and its last equity, and `meta.progress` (version 2, `migrateProgress` reads older shapes) keeps the tutorials seen, the intel perks already explained, the advisor point balance (`points.ts`: start 3, cap 10, settled once per run id; a resumable run is settled, earning nothing, when another run is started), the campaign run, ended campaign runs and the missions assessed in a campaign (`campaign.ts`, `curriculum.ts`; a campaign mission is settled once per run id from the engine's final equity, and a left one from its last equity; a leg that reached its debrief also stores the engine's trade and cost facts, which `campaignReport.ts` adds up for the capstone's `CampaignReport`).

## Campaign, curriculum map and journal

The menu offers the campaign (missions in `campaignMissions()` order, capital carried through the engine's `startingBalance` start option, bankruptcy or less than `CAMPAIGN_MIN_CAPITAL` left ends the campaign run; ending it by choice asks first and leaves its mission in progress), free play (any mission, its own rules), the curriculum map (`CurriculumMap.svelte`: stage statuses from `curriculum.ts`, progress summary) and the journal (`Journal.svelte`: every run's plan entries from the `runs` store, newest first, filterable by mission; a run replaced by another before its end is stored as `left`). The curriculum's unlock rules are data in `curriculum.ts`; in a campaign mission a locked "leverage above 2×" becomes the engine's `leverageCap` start option, and the briefing lists what stays locked and which stage unlocks it.

## Intel perks

`perks.ts` holds the rules: Economic calendar at 1, Fast wire I at 2 and Fast wire II at 4 distinct built-in missions finished (`GameSession.finishedCount`, from the runs played to the end; imported packs do not count). A free-play run starts with every unlocked perk on; `PerkPanel` on the briefing toggles them (`GameSession.setPerk` restarts the not-yet-played run on the same seed with the new `newsLead`/`calendar` start options and updates its record), shows each locked perk's requirement, and explains a newly unlocked perk once (`progress.perksIntroduced`, written when the terminal opens). Campaign missions and pack runs start without perks. `NewsPanel` shows the announced calendar (`upcoming`: kind, bar and consensus, for every player), early headlines (EARLY, "public in N bars") and the calendar perk; auto-advance pauses when a headline arrives early (`newsArrived`). The debrief and the journal label runs whose start options used a perk "with intel" (`perksUsed`, `withIntel`).

## Imported markets (CSV and packs)

"Import CSV" in the menu opens a wizard: pick a file, confirm what the engine cannot know (asset, pair, venue, bar interval, timestamp unit, volume unit, leverage cap; the unit and interval are guessed from the first rows), validate, then create a practice mission. The engine (`tl_csv_validate`, `tl_pack_build_csv`) does all the checking, so the errors are the same on every client; the wizard only translates their codes (`csvIssueText`, `csvMetaText` for refused declared values, `packReasonText` for build refusals). The worker hands the CSV text to the engine through the WASM heap (`withText` in `engine.worker.ts`), not as a `ccall` string, which Emscripten copies onto its 64 KB stack; any string argument that can grow past a few KB must do the same. If an engine call traps, the worker drops the module and loads a fresh one on the next request. The result is a private `.tlpack`, masked to a price base of 100 and signed by this device's key, whose seed is created once and kept in the `meta` store. Packs are verified by the engine before they are stored by digest in the `packs` store (IndexedDB stands in for OPFS for now) and again when a run starts or resumes. A run on a pack records `packDigest`, and resuming replays its log on that pack. "Load pack" installs a `.tlpack` file only if it is signed by this device or by a key pinned in `PINNED_PUBLISHER_KEYS` (`src/lib/game/packs.ts`, empty until there is a publisher key). Mid-run, the UI shows these markets as KNOWN and never shows the real asset or prices; the debrief shows the reveal from the engine (the CSV-import source and transformation note are translated by `revealTexts` in `src/lib/game/debrief.ts`). Practice runs start without the advisor and never change the advisor point balance.

## Academy lessons

Stages without a mission are taught by short lessons (`lessons.ts`; today Foundations: "Reading a bar"). Each lesson runs concept, demonstration, decisions and a transfer exercise on another market and window; a wrong answer explains and lets the player choose again, and the end screen shows the answers right on the first try. Charts (`LessonChart.svelte`) are labeled illustrative, scale from the closed bars only and hatch the unknown future; a screen-reader table lists the bars. The curriculum map starts and repeats lessons; completion is stored in `progress.lessonsDone` and passes the lesson's stage. Lessons never gate the campaign, so a lesson-only stage never blocks the stages after it.

## Pack catalog and downloads

"Pack catalog" in the menu reads a static manifest (`VITE_CATALOG_URL` at build time, default `/catalog/catalog.json`; the backend endpoint will only change this URL). Downloads (`download.ts`) fetch 64 KB byte ranges and commit each chunk to IndexedDB before the next request, so an interrupted, paused or reloaded download resumes where it stopped ("Resume download"); a server that ignores Range is accepted. The complete file must match the manifest's size and SHA-256, then the engine verifies its signature against `PINNED_PUBLISHER_KEYS` before it is stored in the `packs` store (verify-then-install); installed packs play from the catalog or the practice list. In an installed app with a service worker, a fresh download uses Background Fetch and the page installs it when it finishes. The quota is shown and a download that does not fit does not start. Offline, the last catalog seen is listed with downloads disabled; a server error shows "unavailable" with a retry. `/catalog/*` is never precached or cached by the service worker.

The sample packs are synthetic markets built by `scripts/build-sample-catalog.py --seed-file <seed>` (needs the native engine library). They are signed by the development catalog key (`DEV_CATALOG_KEY` in `packs.ts`), whose seed is kept outside the repository; rebuilding with another seed needs the new public key pinned. They use the CSV-import pack format (private packs) until the packer can sign publisher packs.
