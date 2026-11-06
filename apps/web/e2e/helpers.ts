import { expect, type Page } from '@playwright/test';

// Shared steps for the storage, update and accessibility specs.

export async function answerIfAsked(page: Page) {
	const dialog = page.getByRole('dialog', { name: /Pergunta de calibração/ });
	if (await dialog.count()) {
		await dialog.getByRole('button', { name: /Responder/ }).click();
		await expect(dialog).toBeHidden();
	}
}

/** Starts the first mission from the menu (skipping its tutorial, shown on a first start) and opens the terminal. */
export async function startFirstMission(page: Page) {
	await page.getByRole('button', { name: /Iniciar missão/ }).first().click();
	const next = page.getByRole('button', { name: /Pular tutorial|Abrir terminal/ }).first();
	if ((await next.innerText()).includes('Pular tutorial')) await next.click();
	await page.getByRole('button', { name: /Abrir terminal/ }).click();
	await expect(page.locator('.turn')).toBeVisible();
	await answerIfAsked(page);
}

/** A market order by quantity, sent without a plan. */
export async function marketOrder(page: Page) {
	await page.locator('label:has-text("Dimensionar por") select').selectOption('qty');
	await page.getByRole('button', { name: /Enviar ordem/ }).click();
	await page.getByRole('button', { name: /Enviar sem plano/ }).click();
	await expect(page.getByText(/aceita não significa executada/).first()).toBeVisible();
}

/** The "Barra N" counter of the top bar. */
export async function turn(page: Page): Promise<number> {
	return Number(await page.locator('.turn strong').innerText());
}

/** Advances one bar (answering a calibration question first) and waits until the bar counter moves. */
export async function step(page: Page) {
	await answerIfAsked(page);
	const before = await turn(page);
	await page.keyboard.press('Space');
	await expect.poll(async () => (await answerIfAsked(page), await turn(page))).toBeGreaterThan(before);
}

export async function playToEnd(page: Page) {
	for (let i = 0; i < 80 && !(await page.getByText(/Missão concluída|GAME OVER/).count()); i++) {
		await answerIfAsked(page);
		await page.keyboard.press('Shift+Space');
		await page.waitForTimeout(40);
	}
	await expect(page.getByText(/Missão concluída|GAME OVER/)).toBeVisible();
}

export async function waitForServiceWorker(page: Page) {
	await page.evaluate(async () => {
		await navigator.serviceWorker.ready;
	});
	// The first load is not controlled yet: reload once so the worker serves the page.
	await page.reload();
	await expect.poll(() => page.evaluate(() => !!navigator.serviceWorker.controller)).toBe(true);
}

/** Every row of every store of the game's IndexedDB database, by store name. */
export async function dumpDb(page: Page): Promise<Record<string, unknown[]>> {
	return page.evaluate(async () => {
		const db = await new Promise<IDBDatabase>((resolve, reject) => {
			const open = indexedDB.open('tradelab');
			open.onsuccess = () => resolve(open.result);
			open.onerror = () => reject(open.error);
		});
		const out: Record<string, unknown[]> = {};
		for (const name of Array.from(db.objectStoreNames)) {
			out[name] = await new Promise<unknown[]>((resolve, reject) => {
				const r = db.transaction(name).objectStore(name).getAll();
				r.onsuccess = () => resolve(r.result);
				r.onerror = () => reject(r.error);
			});
		}
		db.close();
		return out;
	});
}

/** Patches one row of a store (read, merge `patch`, write back). */
export async function patchRow(page: Page, store: string, key: IDBValidKey, patch: Record<string, unknown>) {
	await page.evaluate(
		async ({ store, key, patch }) => {
			const db = await new Promise<IDBDatabase>((resolve, reject) => {
				const open = indexedDB.open('tradelab');
				open.onsuccess = () => resolve(open.result);
				open.onerror = () => reject(open.error);
			});
			await new Promise<void>((resolve, reject) => {
				const tx = db.transaction(store, 'readwrite');
				const s = tx.objectStore(store);
				const r = s.get(key);
				r.onsuccess = () => s.put({ ...r.result, ...patch });
				tx.oncomplete = () => resolve();
				tx.onerror = () => reject(tx.error);
			});
			db.close();
		},
		{ store, key, patch }
	);
}

/**
 * Records every message between the page and the engine worker (as JSON text) in
 * `window.__toWorker` / `window.__fromWorker`. Call before the first navigation.
 */
export async function recordWorkerMessages(page: Page) {
	await page.addInitScript(() => {
		const w = window as unknown as { __toWorker: string[]; __fromWorker: string[] };
		w.__toWorker = [];
		w.__fromWorker = [];
		const text = (v: unknown) => JSON.stringify(v, (_k, x) => (x instanceof Uint8Array ? `<${x.length} bytes>` : x));
		const Base = window.Worker;
		window.Worker = class extends Base {
			constructor(url: string | URL, options?: WorkerOptions) {
				super(url, options);
				this.addEventListener('message', (e) => w.__fromWorker.push(text(e.data)));
			}
			postMessage(message: unknown, transfer?: unknown) {
				w.__toWorker.push(text(message));
				return super.postMessage(message, transfer as Transferable[]);
			}
		} as typeof Worker;
	});
}

export interface WorkerLog {
	toWorker: string[];
	fromWorker: string[];
}

export async function workerLog(page: Page): Promise<WorkerLog> {
	return page.evaluate(() => {
		const w = window as unknown as { __toWorker: string[]; __fromWorker: string[] };
		return { toWorker: [...w.__toWorker], fromWorker: [...w.__fromWorker] };
	});
}

/** Request kinds sent to the worker, in order. */
export const requestKinds = (log: WorkerLog) => log.toWorker.map((m) => (JSON.parse(m) as { req: { kind: string } }).req.kind);

/** The latest game state the worker sent (from a start, command or advance reply). */
export function lastState(log: WorkerLog): { step: number; digest: string; bars: string[][]; totalBars: number } | null {
	for (let i = log.fromWorker.length - 1; i >= 0; i--) {
		const reply = JSON.parse(log.fromWorker[i]) as { ok: boolean; res?: Record<string, unknown> };
		const res = reply.res;
		const state = (res?.state ?? res) as { bars?: unknown; digest?: string } | undefined;
		if (state && Array.isArray(state.bars) && typeof state.digest === 'string') return state as never;
	}
	return null;
}

/** Fixed hourly market of `rows` bars (deterministic, no randomness). */
export function csvText(rows: number): string {
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

const DEVICE_SEED = '42'.repeat(32);

/**
 * A private pack of 5,000 bars whose warm-up covers all but the last 60, so the chart starts with
 * 4,940 closed bars. Built in the page by this build's engine with this device's key (the wizard
 * does not expose the warm-up length), then installed through "Load pack" like any .tlpack file.
 */
export async function buildLongPack(page: Page): Promise<Buffer> {
	const bytes = await page.evaluate(
		async ({ csv, seed }) => {
			// Use a known device seed before the app creates one, so the pack is signed by "this device".
			const db = await new Promise<IDBDatabase>((resolve, reject) => {
				const open = indexedDB.open('tradelab');
				open.onsuccess = () => resolve(open.result);
				open.onerror = () => reject(open.error);
			});
			await new Promise<void>((resolve) => {
				const tx = db.transaction('meta', 'readwrite');
				tx.objectStore('meta').put({ key: 'deviceSeed', hex: seed });
				tx.oncomplete = () => resolve();
			});
			db.close();
			const sw = await (await fetch('/service-worker.js')).text();
			const path = /_app\/immutable\/assets\/tradelab\.[\w-]+\.mjs/.exec(sw)![0];
			type Mod = {
				cwrap: (n: string, r: string, a: string[]) => (...a: unknown[]) => number;
				UTF8ToString: (p: number) => string;
				HEAPU8: Uint8Array;
				_malloc: (n: number) => number;
			};
			const mod: Mod = await (await import(/* @vite-ignore */ `/${path}`)).default();
			const utf8 = new TextEncoder().encode(`${csv}\0`);
			const ptr = mod._malloc(utf8.length);
			mod.HEAPU8.set(utf8, ptr);
			const meta = { asset: 'BTC', pair: 'BTC/USDT', venue: '', volumeUnit: 'base', intervalSeconds: 3600, timestampUnit: 'ms', leverageCap: 5, warmupBars: 4940 };
			const res = JSON.parse(mod.UTF8ToString(mod.cwrap('tl_pack_build_csv', 'number', ['number', 'string', 'string'])(ptr, JSON.stringify(meta), seed)));
			if (!res.ok) throw new Error(`pack build failed: ${res.reason}`);
			const data = mod.cwrap('tl_blob_data', 'number', [])();
			const size = mod.cwrap('tl_blob_size', 'number', [])();
			return Array.from(mod.HEAPU8.slice(data, data + size));
		},
		{ csv: csvText(5000), seed: DEVICE_SEED }
	);
	return Buffer.from(bytes);
}

/** The device seed the e2e fixture packs are signed with (e2e/fixtures: branch, synthetic-m09, mark-v2). */
export const FIXTURE_DEVICE_SEED = '0e2e'.repeat(16);

/** Makes FIXTURE_DEVICE_SEED the browser's device seed, so packs signed with its device key install. */
export async function useDeviceSeed(page: Page) {
	await page.goto('/?seed=11');
	await expect(page.getByTestId('pack-file')).toBeAttached(); // the menu is up: the app has created its database
	await page.evaluate(
		(hex) =>
			new Promise<void>((resolve, reject) => {
				const open = indexedDB.open('tradelab');
				open.onerror = () => reject(open.error);
				open.onsuccess = () => {
					try {
						const tx = open.result.transaction('meta', 'readwrite');
						tx.objectStore('meta').put({ key: 'deviceSeed', hex });
						tx.oncomplete = () => {
							open.result.close();
							resolve();
						};
						tx.onerror = () => reject(tx.error);
					} catch (e) {
						reject(e);
					}
				};
			}),
		FIXTURE_DEVICE_SEED
	);
	await page.reload();
}
