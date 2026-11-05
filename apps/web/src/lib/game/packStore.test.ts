import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it } from 'vitest';
import { IDBFactory } from 'fake-indexeddb';
import { formatBytes, MemoryPackIndex, PackLibrary, playableDigests, quotaShortfall, storageQuota, type BlobDir, type PackEntry } from './packStore.ts';
import { openDb, RunStore, type StoredPack } from './storage.ts';

/** An in-memory OPFS directory; `evict` drops a file like the browser would. */
class MemoryDir implements BlobDir {
	files = new Map<string, Uint8Array>();
	failWrites = false;
	async write(name: string, bytes: Uint8Array) {
		if (this.failWrites) throw new DOMException('quota', 'QuotaExceededError');
		this.files.set(name, bytes.slice());
	}
	async read(name: string) {
		return this.files.get(name) ?? null;
	}
	async remove(name: string) {
		this.files.delete(name);
	}
}

const meta = (digest: string, addedAt = 1) => ({
	digest,
	scenarioId: `csv-${digest}`,
	label: digest,
	private: true,
	totalBars: 120,
	warmupBars: 20,
	leverageCap: 5,
	gaps: 0,
	addedAt
});

const BYTES = new Uint8Array([1, 2, 3, 4, 5]);

describe('PackLibrary', () => {
	let index: RunStore<unknown, unknown, unknown>;
	beforeEach(async () => {
		globalThis.indexedDB = new IDBFactory();
		index = new RunStore(await openDb());
	});

	it('writes the bytes to OPFS and only metadata to the IndexedDB index', async () => {
		const dir = new MemoryDir();
		const lib = new PackLibrary(index, dir);
		const e = await lib.install(meta('aa'), BYTES);
		expect(e).toMatchObject({ digest: 'aa', location: 'opfs', size: 5, evicted: false });
		expect(dir.files.get('aa.tlpack')).toEqual(BYTES);
		expect((await index.pack('aa'))?.bytes).toBeUndefined();
		expect(await lib.bytes('aa')).toEqual(BYTES);
		expect(lib.backend).toBe('opfs');
	});

	it('falls back to IndexedDB without OPFS, and when OPFS refuses the write', async () => {
		const plain = new PackLibrary(index, null);
		expect((await plain.install(meta('bb'), BYTES)).location).toBe('idb');
		expect(await plain.bytes('bb')).toEqual(BYTES);
		const dir = new MemoryDir();
		dir.failWrites = true;
		const lib = new PackLibrary(index, dir);
		expect((await lib.install(meta('cc'), BYTES)).location).toBe('idb');
		expect(await lib.bytes('cc')).toEqual(BYTES);
	});

	it('detects an evicted file and a truncated one; the rest stay installed', async () => {
		const dir = new MemoryDir();
		const lib = new PackLibrary(index, dir);
		await lib.install(meta('gone', 1), BYTES);
		await lib.install(meta('cut', 2), BYTES);
		await lib.install(meta('ok', 3), BYTES);
		dir.files.delete('gone.tlpack');
		dir.files.set('cut.tlpack', BYTES.slice(0, 2));
		const list = await lib.list();
		expect(list.map((p) => [p.digest, p.evicted])).toEqual([
			['gone', true],
			['cut', true],
			['ok', false]
		]);
		expect(await lib.bytes('gone')).toBeNull(); // no run starts from partial data
		expect(await lib.bytes('cut')).toBeNull();
		expect(list.every((p) => !('bytes' in p))).toBe(true);
	});

	it('reads packs stored before OPFS (bytes in the index, no location or size)', async () => {
		const legacy: StoredPack = { ...meta('old'), bytes: BYTES };
		await index.savePack(legacy);
		const lib = new PackLibrary(index, new MemoryDir());
		expect((await lib.list())[0]).toMatchObject({ digest: 'old', evicted: false });
		expect(await lib.bytes('old')).toEqual(BYTES);
	});

	it('remove deletes the index entry and the file', async () => {
		const dir = new MemoryDir();
		const lib = new PackLibrary(index, dir);
		await lib.install(meta('aa'), BYTES);
		await lib.remove('aa');
		expect(await lib.list()).toEqual([]);
		expect(dir.files.size).toBe(0);
	});

	it('works on an in-memory index when IndexedDB is unavailable', async () => {
		const lib = new PackLibrary(new MemoryPackIndex(), null);
		await lib.install(meta('mm'), BYTES);
		expect(await lib.bytes('mm')).toEqual(BYTES);
	});
});

describe('playableDigests', () => {
	// Catalog integration: an evicted catalog pack was listed as INSTALLED, with no way to download it again.
	it('regression: leaves evicted packs out, so the catalog offers their download again', () => {
		const row = (digest: string, evicted: boolean) => ({ digest, evicted }) as PackEntry;
		expect([...playableDigests([row('a', false), row('b', true)])]).toEqual(['a']);
	});
});

describe('quota', () => {
	it('reports usage and quota from the StorageManager estimate', async () => {
		const storage = { estimate: async () => ({ usage: 10, quota: 100 }) } as unknown as StorageManager;
		expect(await storageQuota(storage)).toEqual({ usage: 10, quota: 100 });
		expect(await storageQuota(undefined)).toBeNull();
	});

	it('computes the space missing for an install', () => {
		expect(quotaShortfall({ usage: 90, quota: 100 }, 5)).toBe(0);
		expect(quotaShortfall({ usage: 90, quota: 100 }, 25)).toBe(15);
		expect(quotaShortfall(null, 1e12)).toBe(0); // unknown quota: let the browser decide
	});

	it('formats byte counts with the locale decimal separator', () => {
		expect(formatBytes(512, 'pt-BR')).toBe('512 B');
		expect(formatBytes(1536, 'pt-BR')).toBe('1,5 KB');
		expect(formatBytes(1536, 'en')).toBe('1.5 KB');
		expect(formatBytes(5 * 1024 ** 3, 'pt-BR')).toBe('5,0 GB');
	});
});
