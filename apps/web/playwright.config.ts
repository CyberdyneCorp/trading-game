import { defineConfig } from '@playwright/test';

// Local runs may point at an already-installed Chromium via PLAYWRIGHT_CHROMIUM_PATH.
const executablePath = process.env.PLAYWRIGHT_CHROMIUM_PATH || undefined;
// E2E_PORT lets parallel checkouts run their own preview server instead of reusing another one.
const port = Number(process.env.E2E_PORT || 4173);

export default defineConfig({
	testDir: 'e2e',
	timeout: 60_000,
	use: { baseURL: `http://localhost:${port}`, viewport: { width: 1440, height: 900 }, launchOptions: { executablePath } },
	webServer: { command: `npm run build && npx vite preview --port ${port} --strictPort`, port, reuseExistingServer: true, timeout: 120_000 }
});
