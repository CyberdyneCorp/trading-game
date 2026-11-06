import { fileURLToPath } from 'node:url';
import { defineConfig } from '@playwright/test';

// Local runs may point at an already-installed Chromium via PLAYWRIGHT_CHROMIUM_PATH.
const executablePath = process.env.PLAYWRIGHT_CHROMIUM_PATH || undefined;
// E2E_PORT lets parallel checkouts run their own preview server instead of reusing another one.
const port = Number(process.env.E2E_PORT || 4173);
const viewport = { width: 1440, height: 900 };
const chromium = { browserName: 'chromium' as const, viewport, launchOptions: { executablePath } };
const preview = `npm run build && npx vite preview --port ${port} --strictPort`;

// E2E_API_PORT switches to the API run: it starts services/api on that port (serving the sample catalog
// in static/catalog), builds the app with VITE_CATALOG_URL=/api/v1/scenarios, proxies /api to the API
// from `vite preview` and runs catalog-api.spec.ts alone. Needs uv and the native engine library.
const apiPort = process.env.E2E_API_PORT ? Number(process.env.E2E_API_PORT) : null;
const catalogDir = fileURLToPath(new URL('./static/catalog', import.meta.url));

const apiRun = apiPort && {
	projects: [{ name: 'api', testMatch: /catalog-api\.spec\.ts/, use: chromium }],
	webServer: [
		{
			command: `uv run --python 3.12 uvicorn tradelab_api.main:app --host 127.0.0.1 --port ${apiPort}`,
			cwd: fileURLToPath(new URL('../../services/api', import.meta.url)),
			env: { TRADELAB_PACKS_DIR: catalogDir },
			url: `http://127.0.0.1:${apiPort}/api/v1/health`,
			reuseExistingServer: false,
			timeout: 120_000
		},
		{
			command: preview,
			env: { VITE_CATALOG_URL: '/api/v1/scenarios', TRADELAB_API_PROXY: `http://127.0.0.1:${apiPort}` },
			port,
			reuseExistingServer: false,
			timeout: 120_000
		}
	]
};

const staticRun = {
	projects: [
		// Every spec except the simulated deploy, which changes what the server returns to everyone,
		// and the API run (E2E_API_PORT).
		{ name: 'chromium', testIgnore: /(deploy|budgets|catalog-api)\.spec\.ts/, use: chromium },
		// Safari's engine (design risk "Safari WASM + worker quirks"): accessibility (incl. a full
		// mission to the debrief), browser support, engine isolation and checkpoints. Offline specs stay
		// Chromium-only: Playwright's WebKit cannot emulate offline navigations through a service worker.
		{
			name: 'webkit',
			testMatch: /(a11y|browser-support|storage)\.spec\.ts/,
			use: { browserName: 'webkit' as const, viewport }
		},
		// Runs alone after the others: the deploy switch is server-wide (scripts/previewDeploy.ts).
		{ name: 'deploy', testMatch: /deploy\.spec\.ts/, dependencies: ['chromium', 'webkit'], use: chromium },
		// Timing budgets run last, alone, so other workers don't compete for the CPU they measure.
		{ name: 'budgets', testMatch: /budgets\.spec\.ts/, dependencies: ['deploy'], use: chromium }
	],
	webServer: { command: preview, port, reuseExistingServer: true, timeout: 120_000 }
};

export default defineConfig({
	testDir: 'e2e',
	timeout: 60_000,
	use: { baseURL: `http://localhost:${port}` },
	...(apiRun || staticRun)
});
