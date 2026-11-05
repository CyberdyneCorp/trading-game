import { describe, expect, it } from 'vitest';
import type { PackCheck } from '#lib/engine/types.ts';
import { CatalogModel, type CatalogHost, type CatalogStore } from './catalog.svelte.ts';
import { sha256Hex } from './download.ts';

const URL_ = 'https://tradelab.example/catalog/catalog.json';
const PACK = new Uint8Array(100_000).map((_, i) => (i * 7) % 256);

class MemoryStore implements CatalogStore {
	chunks = new Map<string, Uint8Array[]>();
	catalog: { url: string; json: unknown } | null = null;
	async partialChunks(d: string) {
		return [...(this.chunks.get(d) ?? [])];
	}
	async savePartialChunk(d: string, i: number, b: Uint8Array) {
		const l = this.chunks.get(d) ?? [];
		l[i] = b;
		this.chunks.set(d, l);
	}
	async clearPartial(d: string) {
		this.chunks.delete(d);
	}
	async partialSizes() {
		return new Map([...this.chunks].map(([d, l]) => [d, l.reduce((n, c) => n + c.length, 0)]));
	}
	async cachedCatalog() {
		return this.catalog;
	}
	async saveCatalog(url: string, json: unknown) {
		this.catalog = { url, json };
	}
}

class Host implements CatalogHost {
	installed = new Set<string>();
	store = new MemoryStore();
	isOnline = true;
	installs: string[] = [];
	accept = true;
	get installedDigests() {
		return this.installed;
	}
	get catalogStore() {
		return this.store;
	}
	async installPack(bytes: Uint8Array, label: string): Promise<PackCheck> {
		if (!this.accept) return { ok: false, reason: 'untrusted_key', detail: '' };
		const digest = await sha256Hex(bytes);
		this.installed.add(digest);
		this.installs.push(label);
		return { ok: true, reason: '', detail: '' };
	}
	online() {
		return this.isOnline;
	}
}

async function manifest() {
	const digest = await sha256Hex(PACK);
	return {
		schemaVersion: 1,
		packs: [
			{
				id: 'a', file: `packs/${digest}.tlpack`, digest, size: PACK.length, scenarioId: 'csv-a', keyId: 'k', intervalSeconds: 3600,
				bars: 100, leverageCap: 5, title: { 'pt-BR': 'Mercado A', en: 'Market A' }
			}
		]
	};
}

/** Serves the manifest and the pack (with Range); `failPackRequest` makes that pack request fail. */
function server(json: unknown, opts: { manifestStatus?: number; failPackRequest?: number } = {}) {
	let packRequests = 0;
	const f = (async (url: string, init?: RequestInit) => {
		if (url === URL_) return new Response(JSON.stringify(json), { status: opts.manifestStatus ?? 200 });
		if (++packRequests === opts.failPackRequest) throw new TypeError('network');
		const m = /^bytes=(\d+)-(\d+)$/.exec(new Headers(init?.headers).get('range') ?? '')!;
		const a = Number(m[1]);
		const b = Math.min(Number(m[2]), PACK.length - 1);
		return new Response(PACK.slice(a, b + 1) as BlobPart, { status: 206, headers: { 'Content-Range': `bytes ${a}-${b}/${PACK.length}` } });
	}) as typeof fetch;
	return f;
}

describe('CatalogModel', () => {
	it('lists the catalog, downloads, verifies and installs a pack', async () => {
		const host = new Host();
		const m = new CatalogModel(host, URL_, server(await manifest()));
		await m.load();
		expect(m.status).toBe('ready');
		const e = m.entries[0];
		expect(m.statusOf(e)).toBe('none');
		await m.download(e, 'Mercado A');
		expect(host.installs).toEqual(['Mercado A']);
		expect(m.statusOf(e)).toBe('installed');
		expect(m.downloads[e.digest]).toBeUndefined();
		expect(host.store.chunks.size).toBe(0); // partial chunks dropped once installed
	});

	it('Download interrupted: keeps the received bytes, offers a resume that continues from them', async () => {
		const host = new Host();
		const json = await manifest();
		const m = new CatalogModel(host, URL_, server(json, { failPackRequest: 2 }));
		await m.load();
		const e = m.entries[0];
		await m.download(e, 'A');
		expect(m.downloads[e.digest]).toMatchObject({ phase: 'failed', reason: 'interrupted', received: 64 * 1024 });
		expect(m.statusOf(e)).toBe('partial');
		expect(m.partial.get(e.digest)).toBe(64 * 1024);

		const resumed = new CatalogModel(host, URL_, server(json)); // e.g. after a reload
		await resumed.load();
		expect(resumed.statusOf(e)).toBe('partial');
		await resumed.download(resumed.entries[0], 'A');
		expect(resumed.statusOf(e)).toBe('installed');
	});

	it('a pack the engine refuses is not installed and shows the reason', async () => {
		const host = new Host();
		host.accept = false;
		const m = new CatalogModel(host, URL_, server(await manifest()));
		await m.load();
		await m.download(m.entries[0], 'A');
		expect(m.downloads[m.entries[0].digest]).toMatchObject({ phase: 'refused', reason: 'untrusted_key' });
		expect(host.installed.size).toBe(0);
	});

	it('Not yet downloaded, offline: shows the last catalog seen, without fetching', async () => {
		const host = new Host();
		const json = await manifest();
		await new CatalogModel(host, URL_, server(json)).load(); // online once: the manifest is kept
		host.isOnline = false;
		let fetched = 0;
		const m = new CatalogModel(host, URL_, (async () => (fetched++, new Response('{}'))) as typeof fetch);
		await m.load();
		expect(m.status).toBe('offline');
		expect(fetched).toBe(0);
		expect(m.entries).toHaveLength(1);
	});

	it('catalog unavailable (server error or bad manifest) is a state, not an error page', async () => {
		const host = new Host();
		const m = new CatalogModel(host, URL_, server(await manifest(), { manifestStatus: 503 }));
		await m.load();
		expect(m.status).toBe('unavailable');
		expect(m.entries).toEqual([]);
		const bad = new CatalogModel(host, URL_, server({ schemaVersion: 99 }));
		await bad.load();
		expect(bad.status).toBe('unavailable');
	});

	it('does not start a download that would exceed the storage quota', async () => {
		const host = new Host();
		const m = new CatalogModel(host, URL_, server(await manifest()));
		await m.load();
		m.quota = { usage: 990_000, quota: 1_000_000 };
		await m.download(m.entries[0], 'A');
		expect(m.downloads[m.entries[0].digest]).toMatchObject({ phase: 'noSpace', shortfall: 190_000 });
		expect(host.installs).toEqual([]);
	});
});
