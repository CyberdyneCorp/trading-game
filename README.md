# Cyberdyne TradeLab

Educational trading game: learn perpetual futures (long/short, leverage, funding, liquidation) and options in a retro terminal, on masked historical or synthetic markets. Virtual money only — no real orders, ever.

- Product spec: `docs/product-spec.md`
- Requirements (OpenSpec): `openspec/`
- Engine (C++20, deterministic): `engine/` — see `engine/README.md`
- Web client (SvelteKit): `apps/web/`

## Play locally

Requires Node 22+, CMake, Ninja and Emscripten (`brew install emscripten`).

```bash
cd apps/web
npm install
npm run engine:build   # compiles engine/ to WebAssembly
npm run dev            # http://localhost:5173
```

Keys: `Space` advance 1 bar · `Shift+Space` advance 5 · `B`/`S` ticket side · `X` close position · `?` help.
Add `?seed=123` to the URL to replay the same synthetic market.

## Status (Phase 1 vertical slice)

Three synthetic perpetual-futures missions (false breakout, funding squeeze, volatility shock) with market/limit/stop orders, reduce-only stop/take-profit, isolated margin up to the mission leverage cap, funding, liquidation, bankruptcy (game over), plan journal and a process-vs-outcome debrief. Options, accounts/sync, historical packs, desktop and iPad clients follow the roadmap in `docs/product-spec.md`.

## Tests

```bash
cd engine && cmake --preset native-debug && cmake --build --preset native-debug && ctest --preset native-debug
cd apps/web && npm test && npm run test:e2e
```
