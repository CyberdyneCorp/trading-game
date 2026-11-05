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
- **PWA:** hand-written `src/service-worker.ts` (SvelteKit 3 `$app/manifest`) instead of Workbox: precache of the versioned build, network-first navigations with cached-shell fallback, cache-first assets, `SKIP_WAITING` only on the player's request from the menu. The WASM engine is embedded in the JS chunks (single file), so it is versioned with the build; keeping older engine builds for unfinished runs is still open (a run recorded under another execution policy shows a warning).
- ~~Brackets sent by the client after the fill~~ — resolved: the engine owns brackets/OCO (execution-policy v2).
- ~~Scoring in TypeScript~~ — resolved: `engine/scoring` (scoring version 2) computes dimensions, findings, lesson, per-trade R and Brier calibration from the command log; plans and answers are engine commands (`RecordPlan`, `AnswerQuestion`, execution policy v4). The client only renders codes as text. Plan free text stays in a local journal and is never scored.
- **Mission texts:** titles/taglines are neutral; what the scenario was designed to teach is revealed only in the debrief.
- **Imports:** SvelteKit 3 uses `#lib/...` subpath imports with explicit extensions.
- **Adapter:** `adapter-static` until the BFF lands (then `adapter-node`).
- **Chart:** custom Canvas2D renderer as designed (D3); the 5,000-bar budget is not yet measured. The accessible table (`ChartTable.svelte`, `chartA11y.ts`) holds every closed bar in pages of 50 instead of a virtualized list; it is visually hidden until opened ("Table") or focused. The canvas box is a keyboard slider over the closed bars (`aria-valuetext` carries the focused candle's announcement).
- **Rejection numbers:** the engine's command result carries a reason code only, so the client adds the figures it already shows (ticket margin estimate, available balance, position margin, initial margin at the account leverage) to `insufficient_margin`, `margin_below_initial` and `leverage_above_cap` (`rejections.ts`); they are display estimates, not engine values.
- **% of equity sizing:** read as exposure (notional = that share of equity), not as margin, so the same percentage means the same market exposure at any leverage.
- **Download interrupted:** there is no catalog download yet; the state is shown when a `.tlpack` file cannot be read or is a valid tar cut short (`packLooksCut`: headers parse but an entry's data or the end marker is missing), instead of the engine's generic `malformed_container`.
- **Shortcuts:** the key map is per viewer in localStorage (`tradelab.keymap.v1`, like the display settings), validated on load (duplicates or reserved keys fall back to the defaults).
- **CI browsers:** Playwright runs Chromium only; WebKit and axe checks are still open (tasks 1.1).
- **Engine protocol:** TypeScript types are hand-written against the C-ABI JSON, not generated from an IDL.
- **Packs:** stored by digest in an IndexedDB `packs` store (not OPFS yet), verified by the engine in the worker (Monocypher Ed25519 and the zstd decoder compiled into the WASM module) instead of WebCrypto Ed25519, so every client shares one verifier. Only private packs exist so far: CSV imports signed by a per-device key whose seed lives in the `meta` store. The pinned publisher key list (`PINNED_PUBLISHER_KEYS`) is empty until there is a publisher key; quota display, eviction detection, downloads and Range resumption are still open.

## Risks
- Canvas text rendering of pixel fonts across DPRs → bitmap atlas at integer scales.
- Safari WASM + worker quirks → Playwright WebKit in CI.
- Browser storage eviction → persistent-storage request, eviction detection, unsynced-run warnings; offline is reliable for installed PWAs, best-effort in a plain tab.
