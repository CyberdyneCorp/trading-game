import { describe, expect, it } from 'vitest';
import {
	DownloadError,
	downloadPack,
	parseContentRange,
	rangeHeader,
	receivedBytes,
	sha256Hex,
	useBackgroundFetch,
	type PartialStore
} from './download.ts';

class MemoryStore implements PartialStore {
	chunks = new Map<string, Uint8Array[]>();
	async partialChunks(d: string) {
		return [...(this.chunks.get(d) ?? [])];
	}
	async savePartialChunk(d: string, i: number, b: Uint8Array) {
		const list = this.chunks.get(d) ?? [];
		list[i] = b;
		this.chunks.set(d, list);
	}
	async clearPartial(d: string) {
		this.chunks.delete(d);
	}
}

const FILE = new Uint8Array(10_000).map((_, i) => (i * 31) % 251);

interface ServerOptions {
	/** Fail (network error) on these request numbers (1-based). */
	failOn?: number[];
	ignoreRange?: boolean;
	body?: Uint8Array;
}

function server(opts: ServerOptions = {}) {
	const ranges: string[] = [];
	const body = opts.body ?? FILE;
	const f = (async (_url: string, init?: RequestInit) => {
		const range = new Headers(init?.headers).get('range') ?? '';
		ranges.push(range);
		if (opts.failOn?.includes(ranges.length)) throw new TypeError('network error');
		const m = /^bytes=(\d+)-(\d+)$/.exec(range);
		if (opts.ignoreRange || !m) return new Response(body.slice() as BlobPart, { status: 200 });
		const a = Number(m[1]);
		const b = Math.min(Number(m[2]), body.length - 1);
		return new Response(body.slice(a, b + 1) as BlobPart, { status: 206, headers: { 'Content-Range': `bytes ${a}-${b}/${body.length}` } });
	}) as typeof fetch;
	return { f, ranges };
}

async function job(store: PartialStore, f: typeof globalThis.fetch, extra: object = {}) {
	return downloadPack({ url: 'https://x/p.tlpack', digest: await sha256Hex(FILE), size: FILE.length, store, fetch: f, chunkBytes: 4096, ...extra });
}

describe('range helpers', () => {
	it('asks for the next chunk, clamped to the file size', () => {
		expect(rangeHeader(0, 10_000, 4096)).toBe('bytes=0-4095');
		expect(rangeHeader(8192, 10_000, 4096)).toBe('bytes=8192-9999');
	});

	it('parses Content-Range', () => {
		expect(parseContentRange('bytes 0-4095/10000')).toEqual({ start: 0, end: 4095, total: 10000 });
		expect(parseContentRange('bytes 5-9/*')).toEqual({ start: 5, end: 9, total: null });
		expect(parseContentRange(null)).toBeNull();
		expect(parseContentRange('items 0-1/2')).toBeNull();
	});
});

describe('downloadPack', () => {
	it('downloads in ranges and returns the verified bytes', async () => {
		const store = new MemoryStore();
		const { f, ranges } = server();
		const progress: number[] = [];
		const bytes = await job(store, f, { onProgress: (n: number) => progress.push(n) });
		expect(bytes).toEqual(FILE);
		expect(ranges).toEqual(['bytes=0-4095', 'bytes=4096-8191', 'bytes=8192-9999']);
		expect(progress).toEqual([0, 4096, 8192, 10_000]);
	});

	it('an interrupted download keeps its chunks and resumes with Range from where it stopped', async () => {
		const store = new MemoryStore();
		const first = server({ failOn: [2] });
		const err = await job(store, first.f).catch((e) => e);
		expect(err).toBeInstanceOf(DownloadError);
		expect((err as DownloadError).reason).toBe('interrupted');
		expect(await receivedBytes(store, await sha256Hex(FILE))).toBe(4096);

		const second = server();
		expect(await job(store, second.f)).toEqual(FILE);
		expect(second.ranges).toEqual(['bytes=4096-8191', 'bytes=8192-9999']);
	});

	it('accepts a server that ignores Range (200 with the whole file), replacing stored chunks', async () => {
		const store = new MemoryStore();
		await job(store, server({ failOn: [2] }).f).catch(() => undefined);
		const { f, ranges } = server({ ignoreRange: true });
		expect(await job(store, f)).toEqual(FILE);
		expect(ranges).toHaveLength(1);
	});

	it('a file that does not match the catalog SHA-256 is refused and its partial data dropped', async () => {
		const store = new MemoryStore();
		const tampered = FILE.slice();
		tampered[5000] ^= 0xff;
		const err = await job(store, server({ body: tampered }).f).catch((e) => e);
		expect((err as DownloadError).reason).toBe('hash_mismatch');
		expect(store.chunks.size).toBe(0);
	});

	it('a file of another size is refused as a size mismatch', async () => {
		const store = new MemoryStore();
		const err = await job(store, server({ body: FILE.slice(0, 9000), ignoreRange: true }).f).catch((e) => e);
		expect((err as DownloadError).reason).toBe('size_mismatch');
	});

	it('an HTTP error stops the download and keeps what arrived', async () => {
		const store = new MemoryStore();
		let n = 0;
		const ok = server().f;
		const f = (async (u: string, i?: RequestInit) => (++n === 2 ? new Response('no', { status: 503 }) : ok(u, i))) as typeof fetch;
		const err = await job(store, f).catch((e) => e);
		expect((err as DownloadError).reason).toBe('http');
		expect(await receivedBytes(store, await sha256Hex(FILE))).toBe(4096);
	});

	it('pausing (abort) reports aborted and keeps the chunks', async () => {
		const store = new MemoryStore();
		const abort = new AbortController();
		const ok = server().f;
		let n = 0;
		const f = (async (u: string, i?: RequestInit) => {
			if (++n === 2) {
				abort.abort();
				throw new DOMException('aborted', 'AbortError');
			}
			return ok(u, i);
		}) as typeof fetch;
		const err = await job(store, f, { signal: abort.signal }).catch((e) => e);
		expect((err as DownloadError).reason).toBe('aborted');
		expect(await receivedBytes(store, await sha256Hex(FILE))).toBe(4096);
	});
});

describe('useBackgroundFetch', () => {
	const manager = { fetch: async () => ({}), get: async () => null };
	it('only for an installed, controlled app with the API, and only for a fresh download', () => {
		expect(useBackgroundFetch({ manager, controlled: true, standalone: true }, 0)).toBe(true);
		expect(useBackgroundFetch({ manager, controlled: true, standalone: true }, 10)).toBe(false); // resume = Range
		expect(useBackgroundFetch({ manager: null, controlled: true, standalone: true }, 0)).toBe(false);
		expect(useBackgroundFetch({ manager, controlled: false, standalone: true }, 0)).toBe(false);
		expect(useBackgroundFetch({ manager, controlled: true, standalone: false }, 0)).toBe(false);
	});
});
