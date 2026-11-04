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
  routes/+page.svelte      single page; switches menu / briefing / terminal / debrief
  service-worker.ts        precache of the versioned build, offline shell, update on request
  lib/engine/              model bridge: engine.worker.ts (WASM host), client.ts, protocol.ts, types.ts, wasm/
  lib/game/                view-models (session, autoplay, settings, pwa, i18n, audio, sound, csvImport — *.svelte.ts rune classes),
                           IndexedDB storage, and pure helpers (sizing, stops, optionsMath, calibration,
                           analysis, format, news, hotkeys, tutorials, candle/chart pattern catalogs
                           patternOutcome and packs) with *.test.ts next to them
  lib/components/          views: Terminal, Chart (+ chartPanes.ts), OrderTicket, OptionTicket, OptionChain,
                           RightRail, TopBar, Briefing, Tutorial (+ TutorialArt), Debrief, MissionMenu, dialogs,
                           PatternSheet (+ PatternAnimation), HelpOverlay, CsvImport; chartDrag.ts (stop/target line drag)
e2e/                       Playwright: mission.spec.ts (gameplay), packs.spec.ts (CSV import, practice packs),
                           offline.spec.ts (OFF01-web)
scripts/                   build-engine.sh (Emscripten), make-icons.py (PWA icons)
static/                    manifest.webmanifest, icons
```

Game rules, fills, scoring and the debrief come from the engine; the client formats engine decimal strings for display and never computes authoritative state (ticket previews in `sizing.ts`/`optionsMath.ts` mirror engine formulas for display only). Runs persist as an append-only action log in IndexedDB (`tradelab` DB: `runs`, `actions`, `meta`, `packs`); resume replays the log through the deterministic engine.

## Imported markets (CSV and packs)

"Import CSV" in the menu opens a wizard: pick a file, confirm what the engine cannot know (asset, pair, venue, bar interval, timestamp unit, volume unit, leverage cap; the unit and interval are guessed from the first rows), validate, then create a practice mission. The engine (`tl_csv_validate`, `tl_pack_build_csv`) does all the checking, so the errors are the same on every client; the wizard only translates their codes (`csvIssueText`). The result is a private `.tlpack`, masked to a price base of 100 and signed by this device's key, whose seed is created once and kept in the `meta` store. Packs are verified by the engine before they are stored by digest in the `packs` store (IndexedDB stands in for OPFS for now) and again when a run starts or resumes. A run on a pack records `packDigest`, and resuming replays its log on that pack. "Load pack" installs a `.tlpack` file only if it is signed by this device or by a key pinned in `PINNED_PUBLISHER_KEYS` (`src/lib/game/packs.ts`, empty until there is a publisher key). Mid-run, the UI shows these markets as KNOWN and never shows the real asset or prices; the debrief shows the reveal from the engine.
