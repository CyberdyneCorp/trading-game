# Tasks: add-web-client

## 1. Scaffold
- [ ] 1.1 SvelteKit + TS strict + Vitest + Playwright + axe; bundle-size and perf budget checks in CI
- [x] 1.2 Design tokens, fonts (license-checked), CRT effect layer with toggles

## 2. Engine integration
- [ ] 2.1 Worker + typed protocol from IDL; leak test (no future data on main thread)
- [ ] 2.2 IndexedDB persistence (runs, commands, checkpoints, packs); reload test

## 3. Phase 1 vertical slice
- [x] 3.1 Briefing → terminal → debrief for one synthetic perp mission
- [x] 3.2 Canvas chart (candles, volume, entry/stop/TP/liquidation lines) + accessible table
- [x] 3.3 Perp ticket (market, stop, TP; leverage; live margin/liquidation preview; warnings)
- [x] 3.4 Right rail, fills, plan prompt, wait action, advance controls, keyboard shortcuts
- [ ] 3.5 BFF sign-in against staging; session-expired path

## 4. Phase 2 MVP
- [ ] 4.1 Limit, trailing, bracket/OCO in ticket
- [x] 4.2 Option chain, option ticket with payoff diagram and Greeks
- [x] 4.3 Indicators UI with warm-up state
- [ ] 4.4 Journal, lessons runner, curriculum map, progress, catalog + download
- [ ] 4.5 CSV import wizard
- [ ] 4.6 All required states; colorblind palette; i18n PT-BR + en-US
- [ ] 4.7 E2E: full mission, UX01, rejection reasons, liquidation flow

## 5. Offline PWA
- [ ] 5.1 Web manifest, icons, service worker precache (shell, fonts, i18n, versioned engine WASM); API network-only
- [ ] 5.2 OPFS pack store with verify-then-install, quota display, `storage.persist()`, eviction detection
- [ ] 5.3 Resumable pack downloads (Range; Background Fetch where available)
- [ ] 5.4 Engine-version pinning for unfinished runs + cache GC; update prompt after mission
- [ ] 5.5 Offline UX: unavailable catalog/sync states, offline profile badge, iOS Add-to-Home-Screen hint
- [ ] 5.6 Playwright offline E2E (context.setOffline): OFF01-web, evicted pack, update mid-mission, old-engine resume
