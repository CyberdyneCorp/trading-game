import { expect, test, type Page } from '@playwright/test';
import {
	dumpDb,
	lastState,
	marketOrder,
	patchRow,
	playToEnd,
	recordWorkerMessages,
	requestKinds,
	startFirstMission,
	step,
	turn,
	waitForServiceWorker,
	workerLog
} from './helpers.ts';

// Offline storage and safe updates (spec web-client "Persistent offline storage", "Safe web
// updates"; tasks 5.2, 5.4, 5.6): evicted pack, update mid-mission, old-engine resume.

const ENGINE_CACHE = 'tradelab-engines-v1';
const OLD_ENGINE = '/_app/immutable/assets/tradelab.OLDBUILD.mjs';

test.beforeEach(async ({ page }) => {
	await recordWorkerMessages(page);
});

/** An hourly market in millisecond timestamps (same shape as packs.spec.ts). */
function csvText(rows = 120): string {
	let out = 'timestamp,open,high,low,close,volume\n';
	let close = 30_000;
	for (let i = 0; i < rows; i++) {
		const open = close;
		close = Math.round((open + (((i * 7919) % 401) - 200) * 0.5) * 100) / 100;
		const high = (Math.max(open, close) + 12).toFixed(2);
		const low = (Math.min(open, close) - 9).toFixed(2);
		out += `${1_700_000_000_000 + i * 3_600_000},${open.toFixed(2)},${high},${low},${close.toFixed(2)},${(100 + (i % 37)).toFixed(1)}\n`;
	}
	return out;
}

async function opfsPackFiles(page: Page): Promise<string[]> {
	return page.evaluate(async () => {
		const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('packs', { create: true });
		const names: string[] = [];
		for await (const name of (dir as unknown as { keys(): AsyncIterable<string> }).keys()) names.push(name);
		return names;
	});
}

async function engineCacheEntries(page: Page): Promise<string[]> {
	return page.evaluate(async (name) => {
		const cache = await caches.open(name);
		return (await cache.keys()).map((r) => new URL(r.url).pathname);
	}, ENGINE_CACHE);
}

test('evicted pack: shown as not on the device offline, and its unfinished run does not start from partial data', async ({ page, context }) => {
	await page.goto('/?seed=11');
	await waitForServiceWorker(page);
	await expect(page.getByTestId('quota')).toContainText(/Espaço: .* usados de .*[0-9],[0-9] [KMGT]B/);
	await page.getByTestId('import-csv').click();
	await page.getByTestId('csv-file').setInputFiles({ name: 'btc-1h.csv', mimeType: 'text/csv', buffer: Buffer.from(csvText()) });
	await page.getByTestId('csv-pair').fill('BTC/USDT');
	await page.getByTestId('csv-validate').click();
	await page.getByTestId('csv-create').click();
	await expect(page.getByTestId('pack-row')).toHaveCount(1);
	// The bytes are an OPFS file; IndexedDB keeps only the index entry.
	const files = await opfsPackFiles(page);
	expect(files).toHaveLength(1);
	const pack = (await dumpDb(page)).packs[0] as { location: string; bytes?: unknown };
	expect(pack.location).toBe('opfs');
	expect(pack.bytes).toBeUndefined();

	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	await step(page);

	// The browser evicts the file while the app is closed; the player comes back offline.
	await page.evaluate(async (name) => {
		const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('packs');
		await dir.removeEntry(name);
	}, files[0]);
	await context.setOffline(true);
	await page.reload();
	await expect(page.getByTestId('pack-evicted')).toHaveText('NÃO ESTÁ NO DISPOSITIVO');
	await expect(page.getByTestId('pack-row')).toContainText('O navegador apagou os dados deste pacote');
	await expect(page.getByTestId('pack-start')).toHaveCount(0);
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.getByTestId('menu-alert')).toContainText('O pacote desta partida não está mais neste dispositivo');
	await expect(page.locator('.turn')).toHaveCount(0);
	expect(requestKinds(await workerLog(page))).not.toContain('startPack');

	// Removing it clears the row; the space shown is refreshed.
	await page.getByTestId('pack-row').getByRole('button', { name: 'Remover' }).click();
	await expect(page.getByTestId('pack-row')).toHaveCount(0);
	await context.setOffline(false);
});

test('regression: rewinding a run on an OPFS pack starts the assisted attempt from the pack file', async ({ page }) => {
	await page.goto('/?seed=11');
	await page.getByTestId('import-csv').click();
	await page.getByTestId('csv-file').setInputFiles({ name: 'btc-1h.csv', mimeType: 'text/csv', buffer: Buffer.from(csvText()) });
	await page.getByTestId('csv-pair').fill('BTC/USDT');
	await page.getByTestId('csv-validate').click();
	await page.getByTestId('csv-create').click();
	await page.getByTestId('pack-start').click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	for (let i = 0; i < 4; i++) await step(page);
	const before = await turn(page);
	// A slow OPFS read (as on a loaded CI runner): the assisted tag must not show over the attempt being
	// rewound while the pack file is still being read.
	await page.evaluate(() => {
		const getFile = FileSystemFileHandle.prototype.getFile;
		FileSystemFileHandle.prototype.getFile = async function () {
			await new Promise((r) => setTimeout(r, 500));
			return getFile.call(this);
		};
	});

	const assist = page.getByTestId('assist-panel');
	await assist.locator('summary').click();
	await assist.getByTestId('assist-turn').fill('2');
	await assist.getByTestId('assist-rewind').click();
	await expect(page.getByTestId('assisted-tag')).toBeVisible();
	expect(await turn(page)).toBeLessThan(before);
	expect(requestKinds(await workerLog(page)).filter((k) => k === 'startPack')).toHaveLength(2);
	const runs = (await dumpDb(page)).runs as { packDigest?: string; assisted?: unknown }[];
	const digest = (await dumpDb(page)).packs[0] as { digest: string };
	expect(runs.find((r) => r.assisted)?.packDigest).toBe(digest.digest);
	await step(page);
});

test('old-engine resume:an unfinished run resumes offline on the engine build it started with; the build is collected after the run', async ({ page, context }) => {
	await page.goto('/?seed=4242');
	await waitForServiceWorker(page);
	await startFirstMission(page);
	await marketOrder(page);
	await step(page);
	await step(page);
	const db = await dumpDb(page);
	const run = db.runs[0] as { runId: string; engine: string; digest: string };
	expect(run.engine).toMatch(/^\/_app\/immutable\/assets\/tradelab\.[\w-]+\.mjs$/);
	await expect.poll(() => engineCacheEntries(page)).toEqual([run.engine]); // pinned when the run started

	// The app updated since this run started: its build is no longer on the server, only in the
	// engine cache (here: the same bytes under the old build's path).
	await page.evaluate(
		async ({ cacheName, current, old }) => {
			const bytes = await (await fetch(current)).arrayBuffer();
			const cache = await caches.open(cacheName);
			await cache.put(old, new Response(bytes, { headers: { 'Content-Type': 'text/javascript' } }));
		},
		{ cacheName: ENGINE_CACHE, current: run.engine, old: OLD_ENGINE }
	);
	await patchRow(page, 'runs', run.runId, { engine: OLD_ENGINE });
	const before = await turn(page);

	await context.setOffline(true);
	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	await expect(page.getByText('Partida retomada no motor da versão em que começou (build OLDBUILD).')).toBeVisible();
	expect(await turn(page)).toBe(before);
	const log = await workerLog(page);
	const useEngine = log.toWorker.map((m) => JSON.parse(m).req).filter((r) => r.kind === 'useEngine');
	expect(useEngine.map((r) => new URL(r.engineUrl).pathname)).toEqual([OLD_ENGINE]);
	expect(lastState(log)!.digest).toBe(run.digest);

	await playToEnd(page);
	await page.getByRole('button', { name: /Voltar às missões/ }).click();
	// The run ended: its build is no longer needed and is collected; the current one stays.
	await expect.poll(() => engineCacheEntries(page)).toEqual([run.engine]);
	// A new run starts on this version's engine again.
	await startFirstMission(page);
	const kinds = (await workerLog(page)).toWorker.map((m) => JSON.parse(m).req).filter((r) => r.kind === 'useEngine');
	expect(new URL(kinds.at(-1).engineUrl).pathname).toBe(run.engine);
	await context.setOffline(false);
});

test('a run whose engine build is gone resumes on the current engine and says so', async ({ page }) => {
	await page.goto('/?seed=4242');
	await startFirstMission(page);
	await step(page);
	const run = (await dumpDb(page)).runs[0] as { runId: string };
	await patchRow(page, 'runs', run.runId, { engine: '/_app/immutable/assets/tradelab.GONE.mjs' });
	const before = await turn(page);
	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.getByText(/O motor desta partida \(build GONE\) não está mais neste dispositivo/)).toBeVisible();
	expect(await turn(page)).toBe(before);
	await step(page);
});

// A checkpoint is in the engine's checkpoint format (kCheckpointFormat), which belongs to the build
// that wrote it: a pinned build restores its own, and a run moved to another build replays its log.
test('a pinned build resumes from its own checkpoint; a run whose build is gone replays instead of restoring it', async ({ page }) => {
	await page.goto('/?seed=4242');
	await waitForServiceWorker(page);
	await startFirstMission(page);
	for (let i = 0; i < 21; i++) await step(page);
	await expect.poll(async () => ((await dumpDb(page)).checkpoints ?? []).length).toBe(1);
	const db = await dumpDb(page);
	const run = db.runs[0] as { runId: string; engine: string; digest: string };
	expect((db.checkpoints[0] as { engine: string }).engine).toBe(run.engine);
	await page.evaluate(
		async ({ cacheName, current, old }) => {
			const bytes = await (await fetch(current)).arrayBuffer();
			await (await caches.open(cacheName)).put(old, new Response(bytes, { headers: { 'Content-Type': 'text/javascript' } }));
		},
		{ cacheName: ENGINE_CACHE, current: run.engine, old: OLD_ENGINE }
	);
	await patchRow(page, 'runs', run.runId, { engine: OLD_ENGINE });
	await patchRow(page, 'checkpoints', run.runId, { engine: OLD_ENGINE });
	const before = await turn(page);

	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.getByText(/build OLDBUILD/)).toBeVisible();
	expect(await turn(page)).toBe(before);
	let log = await workerLog(page);
	expect(requestKinds(log)).toContain('restore');
	expect(requestKinds(log)).not.toContain('start');
	expect(lastState(log)!.digest).toBe(run.digest);

	// The pinned build is gone: the run resumes on the current build from its first bar.
	const gone = '/_app/immutable/assets/tradelab.GONE.mjs';
	await page.evaluate(async ({ cacheName, old }) => (await caches.open(cacheName)).delete(old), { cacheName: ENGINE_CACHE, old: OLD_ENGINE });
	await patchRow(page, 'runs', run.runId, { engine: gone });
	await patchRow(page, 'checkpoints', run.runId, { engine: gone });
	await page.reload();
	await page.getByRole('button', { name: /Continuar partida/ }).click();
	await expect(page.getByText(/O motor desta partida \(build GONE\) não está mais neste dispositivo/)).toBeVisible();
	expect(await turn(page)).toBe(before);
	log = await workerLog(page);
	expect(requestKinds(log)).toContain('start');
	expect(requestKinds(log)).not.toContain('restore');
	expect(lastState(log)!.digest).toBe(run.digest);
});
