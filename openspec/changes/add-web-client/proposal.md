# Proposal: add-web-client

## Why

Web is the first client to ship (Phase 1 vertical slice, Phase 2 MVP). It also establishes the retro terminal UX that desktop and iPad must reproduce natively, so the shared UI rules are specified here once.

## What Changes

- Shared, platform-neutral `terminal-ui` capability: layout regions, visual system, order-ticket behavior, required states, accessibility, i18n.
- `apps/web`: TypeScript + SvelteKit app running the WASM engine in a Web Worker, retro chart renderer, order ticket for perps and options, option chain, journal, debrief, lessons, settings, local persistence (IndexedDB) and sync through the BFF.
- Installable offline PWA: service worker caches the versioned app shell and WASM engine; downloaded packs live in persistent browser storage, so missions, lessons and debriefs work with no connection.

## Capabilities

### New Capabilities
- `terminal-ui`: client-independent UX contract (layout, visual tokens, states, accessibility, numeric display, i18n).
- `web-client`: web-specific behavior (worker isolation, persistence, offline PWA, keyboard, performance budgets, BFF session).

### Modified Capabilities
None.

## Non-goals

- Phone-optimized web layout beyond a usable read-only fallback (native apps own phones).
- Editorial admin UI.

## Impact

- New: `apps/web/` (SvelteKit, Vite, Vitest, Playwright).
- Depends on `add-deterministic-engine` (WASM), `add-scenarios-and-gameplay`, `add-cyberdyne-identity-and-backend` (BFF).
