# Tasks: add-web-client

## 1. Scaffold
- [ ] 1.1 SvelteKit + TS strict + Vitest + Playwright + axe; bundle-size and perf budget checks in CI — SvelteKit 3 + Svelte 5 + TS strict, Vitest units and Playwright E2E (Chromium only) run in `.github/workflows/web.yml`; axe checks, WebKit, bundle-size and performance budgets pending
- [x] 1.2 Design tokens, fonts (license-checked), CRT effect layer with toggles
- [ ] 1.3 Unsupported-browser message (no WebAssembly, no Ed25519 WebCrypto)

## 2. Engine integration
- [ ] 2.1 Worker + typed protocol from IDL; leak test (no future data on main thread) — worker (`src/lib/engine/engine.worker.ts`) and typed protocol (`protocol.ts`, `types.ts`, hand-written, not generated from an IDL) done; the engine C-ABI test asserts the state JSON carries exactly the bars up to the current step; a main-thread/message-log leak test is pending
- [ ] 2.2 IndexedDB persistence (runs, commands, checkpoints, packs); reload test — runs + per-action commits done (commit-before-engine, legacy migration, reload/resume tests); checkpoints and pack cache pending (no packs yet)

## 3. Phase 1 vertical slice
- [x] 3.1 Briefing → terminal → debrief for one synthetic perp mission
- [x] 3.2 Canvas chart (candles, volume, entry/stop/TP/liquidation lines) + accessible table
- [x] 3.3 Perp ticket (market, stop, TP; leverage; live margin/liquidation preview; warnings)
- [x] 3.4 Right rail, fills, plan prompt, wait action, advance controls, keyboard shortcuts
- [ ] 3.5 BFF sign-in against staging; session-expired path

## 4. Phase 2 MVP
- [x] 4.1 Limit, trailing, bracket/OCO in ticket — market/limit/stop entries with attached stop + target (engine-side OCO); trailing stop offered when protecting an open position
- [ ] 4.1a Ticket sizing by notional and by % of equity (today: by risk % with a stop, or by quantity)
- [x] 4.2 Option chain, option ticket with payoff diagram and Greeks
- [x] 4.3 Indicators UI with warm-up state
- [x] 4.3d Auto-advance mode (speeds, waits on dialogs, pauses on notable events, Space takes control)
- [x] 4.3e Music by mood (menu/game/tense with hysteresis, crossfade), win/loss stings, mute (`M`, top bar) and volumes persisted; audio cached on first play with byte ranges (`audioRules.ts`, `audio.svelte.ts`, `swRoutes.ts`)
- [x] 4.3c Multi-candle and chart patterns in the sheet; engine-detected patterns marked on the chart, linked to the sheet
- [x] 4.3b Bollinger overlay and candle pattern cheat sheet (P)
- [x] 4.3a Indicator panes (RSI/MACD/ADX selector, VWAP overlay) and stop/target tools (ATR/swing/% presets, R targets, mark on chart, chart preview, unprotected-position warning)
- [ ] 4.4 Journal, lessons runner, curriculum map, progress, catalog + download — plan theses are kept in a local journal and shown in the debrief timeline; journal screen, lessons, curriculum map, progress and catalog pending
- [ ] 4.5 CSV import wizard
- [ ] 4.6 All required states; colorblind palette; i18n PT-BR + en-US — colorblind palette and PT-BR/en catalogs done; states done: loading, engine error, indicator warming up, accepted vs filled, capacity-limited/partial fills flagged in the feed, liquidation warning, liquidated, margin call, option expired/settled, game over, offline, storage not durable; plain-language rejection/cancel reasons for every engine reason code (PT-BR/en, `reasonText`); pending: numbers inside rejection messages, no data, download interrupted, incompatible pack, session expired, sync pending, device conflict
- [ ] 4.7 E2E: full mission, UX01, rejection reasons, liquidation flow — full mission, EDU02, reload/resume, options, calibration, stop/target, patterns and auto-advance E2E done; UX01, rejection reasons and liquidation flow pending
- [ ] 4.8 Accessible chart: table of all closed bars, text summary, per-candle keyboard focus — today a screen-reader table of the last 20 closed bars
- [ ] 4.9 Remappable shortcuts; option-chain and journal shortcuts

## 5. Offline PWA
- [x] 5.1 Web manifest, icons, service worker precache (shell, fonts, i18n, versioned engine WASM); API network-only — `src/service-worker.ts`, routing in `src/lib/game/swRoutes.ts` (`/api/*` never cached; unit-tested)
- [ ] 5.2 OPFS pack store with verify-then-install, quota display, `storage.persist()`, eviction detection — `storage.persist()` requested at mission start and its result shown in settings; OPFS store, quota display and eviction detection pending
- [ ] 5.3 Resumable pack downloads (Range; Background Fetch where available)
- [ ] 5.4 Engine-version pinning for unfinished runs + cache GC; update prompt after mission — update prompt (menu only) and policy-mismatch warning done; keeping old engine builds for unfinished runs pending
- [ ] 5.5 Offline UX: unavailable catalog/sync states, offline profile badge, iOS Add-to-Home-Screen hint — OFFLINE badge (top bar + menu) and iOS Add-to-Home-Screen hint done; catalog/sync states and profile badge pending (no catalog, sync or profile yet)
- [ ] 5.6 Playwright offline E2E (context.setOffline): OFF01-web, evicted pack, update mid-mission, old-engine resume — OFF01-web (incl. offline reload/resume) done
