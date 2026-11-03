# Design: add-web-client

## Decisions

### D1. Stack
SvelteKit (Svelte 5 runes) + TypeScript strict + Vite; `adapter-node` deployed on Coolify at `tradelab[-dev].coolify.cyberdynecorp.ai`; its server reverse-proxies `/api/*` to the API service so the BFF cookie is host-only and first-party. Vitest for units, Playwright for E2E + axe accessibility checks.

### D2. MVVM
- `lib/engine/` — worker + typed protocol (types generated from engine IDL).
- `lib/viewmodels/` — Svelte runes stores per screen (TerminalVM, TicketVM, ChainVM, DebriefVM). No business logic in components.
- `lib/views/` — components only bind to VMs.
- `lib/services/` — api client (BFF), persistence (IndexedDB via `idb`), sync.

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

## Risks
- Canvas text rendering of pixel fonts across DPRs → bitmap atlas at integer scales.
- Safari WASM + worker quirks → Playwright WebKit in CI.
- Browser storage eviction → persistent-storage request, eviction detection, unsynced-run warnings; offline is reliable for installed PWAs, best-effort in a plain tab.
