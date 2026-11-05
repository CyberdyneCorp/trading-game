# Design: add-web-client

## Decisions

### D1. Stack
SvelteKit (Svelte 5 runes) + TypeScript strict + Vite; `adapter-node` deployed on Coolify at `tradelab[-dev].coolify.cyberdynecorp.ai`; its server reverse-proxies `/api/*` to the API service so the BFF cookie is host-only and first-party. Vitest for units, Playwright for E2E + axe accessibility checks.

### D2. MVVM
Target layout:
- `lib/engine/` — worker + typed protocol (types generated from engine IDL).
- `lib/viewmodels/` — Svelte runes stores per screen (TerminalVM, TicketVM, ChainVM, DebriefVM). No business logic in components.
- `lib/views/` — components only bind to VMs.
- `lib/services/` — api client (BFF), persistence (IndexedDB via `idb`), sync.

As built (Phase 1/2 slice):
- `src/lib/engine/` — `engine.worker.ts` (hosts the WASM module), `client.ts` (promise-based, strictly ordered requests), `protocol.ts` + `types.ts` (hand-written types mirroring the C-ABI JSON; IDL generation pending), `wasm/` (build output of `scripts/build-engine.sh`, gitignored).
- `src/lib/game/` — view-models as Svelte 5 rune classes: `session.svelte.ts` (`GameSession`: screen flow, action log, persistence, replay), `autoplay.svelte.ts`, `settings.svelte.ts`, `pwa.svelte.ts`, `i18n.svelte.ts`; plus pure, unit-tested helpers (`sizing`, `stops`, `optionsMath`, `calibration`, `analysis`, `format`, `news`, `candlePatterns`, `chartPatterns`) and `storage.ts` (raw IndexedDB, no `idb` dependency).
- `src/lib/components/` — views (Terminal, Chart + `chartPanes.ts`, OrderTicket, OptionTicket, OptionChain, RightRail, TopBar, Briefing, Debrief, MissionMenu, PlanDialog, CalibrationDialog, PatternSheet, HelpOverlay, …). Terminal-local UI state (open dialogs, ticket draft) lives in components; game rules live in the engine.
- `src/service-worker.ts`, `src/routes/+page.svelte` (single page switching screens), `e2e/` (Playwright).
- No BFF client or sync yet.

### D3. Chart rendering
Custom Canvas2D renderer (pixel-snapped candles, bitmap font for labels) instead of a charting library: full control of retro look, no license questions, and the data model is simple (OHLCV + lines). WebGL is a later optimization only if the 5,000-bar budget fails. Accessible table rendered as hidden DOM, kept in sync with the current step. Decision revisited after the Phase 0 spike (product-spec §20.4).

### D4. CRT effects
CSS/Canvas post-effects (scanline overlay, subtle bloom via filter) behind a settings toggle; disabled under `prefers-reduced-motion`.

### D4a. Offline PWA
- `@vite-pwa/sveltekit` (Workbox) with a precache manifest per build; app shell, fonts, i18n and the engine `.wasm` are content-hashed. API calls are network-only (never cached by the SW) so stale catalog/progress data is never served as fresh.
- Packs: OPFS files keyed by digest, verified (SHA-256 + Ed25519) before being marked installed; index of installed packs in IndexedDB. Downloads use HTTP Range so they resume; Background Fetch where supported, foreground otherwise.
- Engine pinning: engine builds are published under versioned paths (`/engine/<version>/engine.wasm`); the SW keeps every version referenced by an unfinished run in a dedicated cache and garbage-collects the rest after sync.
- Update UX: `registerType: 'prompt'`; "New version ready, reload after this mission".
- iOS/iPadOS Safari: storage of non-installed sites can be evicted after ~7 days without use; the UI pushes "Add to Home Screen" before the first pack download on iOS. The native iPad app remains the recommended offline client there.

### D5. i18n
`svelte-i18n` or Paraglide (compile-time) — pick Paraglide for tree-shaken catalogs; PT-BR default.

As built: a small typed catalog in `src/lib/game/i18n.svelte.ts` (locales `pt-BR` default and `en`, keys type-checked against the PT-BR catalog, `{param}` interpolation); mission texts in `missions.ts`, pattern texts in `candlePatterns.ts`/`chartPatterns.ts`. The locale is stored with the display settings and only affects formatting. Revisit Paraglide when catalogs grow.

## Phase 1 slice: deliberate deviations (to close before MVP)

- ~~Persistence in localStorage~~ — resolved: IndexedDB (`runs`, `actions[runId, seq]`, `meta`); each action is committed in its own transaction before the engine sees it; resume replays the gap-free prefix; v1 localStorage saves are migrated.
- **PWA:** hand-written `src/service-worker.ts` (SvelteKit 3 `$app/manifest`) instead of Workbox: precache of the versioned build, network-first navigations with cached-shell fallback, cache-first assets (searching every cache), `SKIP_WAITING` only on the player's request from the menu.
- **Engine pinning (as built):** instead of `/engine/<version>/engine.wasm`, Vite emits the Emscripten module (WASM embedded) as a content-hashed asset (`_app/immutable/assets/tradelab.<hash>.mjs`, `src/lib/engine/build.ts`), and the worker loads it by URL (`init`/`useEngine`). A run records its build path (`engine`) and the page copies the module into `tradelab-engines-v1`, a cache the service worker keeps on activation; resume loads the run's build (offline too) and a new run switches back to the current one. When the menu refreshes, every pinned build other than the current one and the resumable run's is deleted (`engineCache.ts`). The page, not the service worker, does pinning and GC: it knows which run is unfinished. A run recorded before pinning, or whose build is gone, resumes on the current build with a notice (and the execution-policy warning when the policy differs).
- **Checkpoints:** every 20 logged actions the engine's opaque checkpoint (`tl_checkpoint`: `RunState` only, no scenario bars) is stored with the action count, digest and engine build (`checkpoints` store, latest per run, pruned when the run can no longer resume). Resume restores it (`tl_run_restore`/`tl_run_restore_pack`, hex passed through the heap) only on the same build and only if the restored digest matches, then replays the tail; otherwise it replays the whole log. The engine's checkpoint format (`kCheckpointFormat`, 5 since lesson answers and decision records) is a property of the build: a checkpoint is only offered to the build that wrote it, so a pinned older build resumes from its own checkpoints and a run moved to the current build (its pinned build gone) replays its whole log instead of feeding it another format, which the engine would refuse anyway. The checkpoint also keeps the option series seen so far (`EventNames`: kind, strike and expiry, named when shown so a language switch renames them), so a series that expired before it, and is no longer in the restored state, keeps its name in the event feed and the debrief after such a resume.
- ~~Brackets sent by the client after the fill~~ — resolved: the engine owns brackets/OCO (execution-policy v2).
- ~~Scoring in TypeScript~~ — resolved: `engine/scoring` (scoring version 2) computes dimensions, findings, lesson, per-trade R and Brier calibration from the command log; plans and answers are engine commands (`RecordPlan`, `AnswerQuestion`, execution policy v4). The client only renders codes as text. Plan free text stays in a local journal and is never scored.
- **Mission texts:** titles/taglines are neutral; what the scenario was designed to teach is revealed only in the debrief.
- **Imports:** SvelteKit 3 uses `#lib/...` subpath imports with explicit extensions.
- **Adapter:** `adapter-static` until the BFF lands (then `adapter-node`).
- **Chart:** custom Canvas2D renderer as designed (D3). Candles and volume are drawn as one path per direction, and bars narrower than a pixel are merged per column (`drawnCandles`), which keeps zooming over 5,000 bars at 60 fps (median redraw, `e2e/budgets.spec.ts`). The screen-reader table holds the last 20 closed bars.
- **CI browsers and budgets:** Playwright runs every spec in Chromium and a subset in WebKit (axe, browser support, engine isolation and checkpoints, a full mission); offline specs stay Chromium-only because Playwright's WebKit cannot emulate offline navigations through a service worker. The update-mid-mission spec needs a new service-worker script, which browsers fetch without cookies or test headers, so `scripts/previewDeploy.ts` (a `vite preview`-only plugin) switches it server-wide and the spec runs alone in the `deploy` project. Budgets: `scripts/check-budgets.mjs` (gzipped initial JS + engine module) and `e2e/budgets.spec.ts` (mission ready, p95 advance latency, 5,000-bar pack ready and zoom redraw; the long pack is built in the page with a warm-up of 4,940 bars, which the wizard does not offer).
- **Supported browsers:** before loading the engine the page checks `WebAssembly.validate` and an Ed25519 `importKey` in WebCrypto (spec list) and shows the unsupported-browser message instead of the game. Packs are still verified by the engine's own Ed25519 (see Packs), so the WebCrypto check is a capability gate, not a verifier.
- **Engine protocol:** TypeScript types are hand-written against the C-ABI JSON, not generated from an IDL.
- **Packs:** installed by digest, bytes in OPFS `packs/<digest>.tlpack` (written before the IndexedDB index entry; in the index itself where OPFS cannot write files, e.g. older Safari), verified by the engine in the worker (Monocypher Ed25519 and the zstd decoder compiled into the WASM module) instead of WebCrypto Ed25519, so every client shares one verifier. Only private packs exist so far: CSV imports signed by a per-device key whose seed lives in the `meta` store. The pinned publisher key list (`PINNED_PUBLISHER_KEYS`) is empty until there is a publisher key; the menu shows used/available space (`storage.estimate()`) and refuses an install that does not fit, with the space missing; a pack whose file is missing or truncated is shown as not on this device and cannot start or resume. Downloads and Range resumption are still open (no catalog yet).

## Risks
- Canvas text rendering of pixel fonts across DPRs → bitmap atlas at integer scales.
- Safari WASM + worker quirks → Playwright WebKit in CI.
- Browser storage eviction → persistent-storage request, eviction detection, unsynced-run warnings; offline is reliable for installed PWAs, best-effort in a plain tab.
