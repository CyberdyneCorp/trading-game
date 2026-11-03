import { defineConfig } from '@playwright/test';

// Local runs may point at an already-installed Chromium via PLAYWRIGHT_CHROMIUM_PATH.
const executablePath = process.env.PLAYWRIGHT_CHROMIUM_PATH || undefined;

export default defineConfig({
	testDir: 'e2e',
	timeout: 60_000,
	use: { baseURL: 'http://localhost:4173', viewport: { width: 1440, height: 900 }, launchOptions: { executablePath } },
	webServer: { command: 'npm run build && npx vite preview --port 4173 --strictPort', port: 4173, reuseExistingServer: true, timeout: 120_000 }
});
