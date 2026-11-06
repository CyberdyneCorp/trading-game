import { expect, test, type Page } from '@playwright/test';

// The pack catalog served by services/api (add-cyberdyne-identity-and-backend task 3.1). Runs only with
// E2E_API_PORT (see playwright.config.ts): the app is built with VITE_CATALOG_URL=/api/v1/scenarios and
// `vite preview` proxies /api to a locally started API, so every byte below comes from the API.
// The service worker is blocked so page.route sees the pack requests (it never caches /api/* either).
test.use({ serviceWorkers: 'block' });

const PACK = '**/api/v1/packs/*';

async function openCatalog(page: Page) {
	const listing = page.waitForResponse((r) => new URL(r.url()).pathname === '/api/v1/scenarios');
	await page.getByTestId('open-catalog').click();
	expect((await listing).status()).toBe(200);
	await expect(page.getByRole('heading', { name: 'Catálogo de pacotes' })).toBeVisible();
	// The static sample catalog: Markets A, B and C (liquidity shock).
	await expect(page.getByTestId('catalog-entry')).toHaveCount(3);
}

const entryA = (page: Page) => page.getByTestId('catalog-entry').filter({ hasText: 'Mercado A · 1h' });

test('the API serves the catalog and packs by digest with Range and a strong ETag', async ({ request }) => {
	const res = await request.get('/api/v1/scenarios');
	expect(res.headers()['cache-control']).toBe('no-store');
	const catalog = await res.json();
	expect(catalog.schemaVersion).toBe(1);
	const a = catalog.packs.find((p: { id: string }) => p.id === 'sample-a-1h');
	expect(a.file).toBe(`packs/${a.digest}`);

	const part = await request.get(`/api/v1/packs/${a.digest}`, { headers: { Range: 'bytes=0-9' } });
	expect(part.status()).toBe(206);
	expect(part.headers()['content-range']).toBe(`bytes 0-9/${a.size}`);
	expect(part.headers()['etag']).toBe(`"${a.digest}"`);
	expect((await part.body()).length).toBe(10);
	const past = await request.get(`/api/v1/packs/${a.digest}`, { headers: { Range: `bytes=${a.size}-` } });
	expect(past.status()).toBe(416);
});

test('download from the API, interrupted, resumes with Range after a reload and installs', async ({ page }) => {
	const errors: string[] = [];
	page.on('pageerror', (e) => errors.push(String(e)));
	const answers: string[] = [];
	page.on('response', (r) => {
		if (r.url().includes('/api/v1/packs/')) answers.push(`${r.status()} ${r.headers()['content-range'] ?? ''}`);
	});
	await page.goto('/?seed=5');
	await openCatalog(page);
	await expect(entryA(page)).toContainText('4.740 barras');

	// The connection drops on the second byte range: the first 64 KB are kept.
	const ranges: string[] = [];
	await page.route(PACK, async (route) => {
		ranges.push(route.request().headers()['range'] ?? '');
		if (ranges.length === 2) return route.abort('internetdisconnected');
		return route.continue();
	});
	await entryA(page).getByTestId('catalog-download').click();
	await expect(entryA(page).getByTestId('download-state')).toContainText('Download interrompido em 64 KB de');
	expect(ranges).toEqual(['bytes=0-65535', 'bytes=65536-77311']);
	await page.unroute(PACK);

	// After a reload the download resumes from byte 65536; the API answers both ranges with 206.
	await page.reload();
	await openCatalog(page);
	const resumed: string[] = [];
	await page.route(PACK, (route) => (resumed.push(route.request().headers()['range'] ?? ''), route.continue()));
	await entryA(page).getByTestId('catalog-resume').click();
	await expect(entryA(page).getByTestId('catalog-installed')).toHaveText('INSTALADO');
	expect(resumed).toEqual(['bytes=65536-77311']);
	expect(answers).toEqual(['206 bytes 0-65535/77312', '206 bytes 65536-77311/77312']);

	// Verified by size, SHA-256 and the engine's signature check, then playable like a static-catalog pack.
	await entryA(page).getByTestId('catalog-play').click();
	await expect(page.getByRole('heading', { name: 'Treino · pacote do catálogo' })).toBeVisible();
	expect(errors).toEqual([]);
});
