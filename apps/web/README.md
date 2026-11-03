# TradeLab web client

SvelteKit 3 + Svelte 5 (runes), TypeScript strict. MVVM: `src/lib/engine` (WASM worker bridge), `src/lib/game` (view-models, i18n, sizing, debrief analysis), `src/lib/components` (views).

```bash
npm install
npm run engine:build   # needs Emscripten; writes src/lib/engine/wasm/tradelab.mjs (gitignored)
npm run dev
npm run check && npm test && npm run test:e2e
```

Behavior contract: `openspec/changes/add-web-client/`. Slice deviations are listed in its `design.md`.
