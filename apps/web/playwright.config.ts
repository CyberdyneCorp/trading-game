import { defineConfig } from '@playwright/test';

// Local runs may point at an already-installed Chromium via PLAYWRIGHT_CHROMIUM_PATH.
const executablePath = process.env.PLAYWRIGHT_CHROMIUM_PATH || undefined;
// E2E_PORT lets parallel checkouts run their own preview server instead of reusing another one.
const port = Number(process.env.E2E_PORT || 4173);
const viewport = { width: 1440, height: 900 };
const chromium = { browserName: 'chromium' as const, viewport, launchOptions: { executablePath } };

export default defineConfig({
	testDir: 'e2e',
	timeout: 60_000,
	use: { baseURL: `http://localhost:${port}` },
	projects: [
		// Every spec except the simulated deploy, which changes what the server returns to everyone.
		{ name: 'chromium', testIgnore: /(deploy|budgets)\.spec\.ts/, use: chromium },
		// Safari's engine (design risk "Safari WASM + worker quirks"): accessibility (incl. a full
		// mission to the debrief), browser support, engine isolation and checkpoints. Offline specs stay
		// Chromium-only: Playwright's WebKit cannot emulate offline navigations through a service worker.
		{
			name: 'webkit',
			testMatch: /(a11y|browser-support|storage)\.spec\.ts/,
			use: { browserName: 'webkit', viewport }
		},
		// Runs alone after the others: the deploy switch is server-wide (scripts/previewDeploy.ts).
		{ name: 'deploy', testMatch: /deploy\.spec\.ts/, dependencies: ['chromium', 'webkit'], use: chromium },
		// Timing budgets run last, alone, so other workers don't compete for the CPU they measure.
		{ name: 'budgets', testMatch: /budgets\.spec\.ts/, dependencies: ['deploy'], use: chromium }
	],
	webServer: { command: `npm run build && npx vite preview --port ${port} --strictPort`, port, reuseExistingServer: true, timeout: 120_000 }
});
