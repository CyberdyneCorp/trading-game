import { describe, expect, it } from 'vitest';
import { enginePinned, enginesToKeep, gcEngines, pinEngine, type EngineCaches } from './engineCache.ts';
import { ENGINE_CACHE, keepCache } from './swRoutes.ts';
import { engineBuildLabel } from '#lib/engine/build.ts';

const ORIGIN = 'http://localhost';

/** Cache API fake: `add` succeeds only for URLs the "server" still has. */
function fakeCaches(server: Set<string>) {
	const stores = new Map<string, Map<string, Response>>();
	const caches: EngineCaches = {
		async open(name: string) {
			const m = stores.get(name) ?? new Map<string, Response>();
			stores.set(name, m);
			const key = (r: RequestInfo | URL) => new URL(typeof r === 'string' ? r : r instanceof URL ? r.href : r.url, ORIGIN).href;
			return {
				async match(r: RequestInfo | URL) {
					return m.get(key(r));
				},
				async add(r: RequestInfo | URL) {
					if (!server.has(new URL(key(r)).pathname)) throw new TypeError('Failed to fetch');
					m.set(key(r), new Response('engine'));
				},
				async keys() {
					return [...m.keys()].map((u) => new Request(u));
				},
				async delete(r: RequestInfo | URL) {
					return m.delete(key(r));
				}
			} as Pick<Cache, 'match' | 'add' | 'keys' | 'delete'>;
		}
	};
	return { caches, stores };
}

const V1 = '/_app/immutable/assets/tradelab.AAAA1111.mjs';
const V2 = '/_app/immutable/assets/tradelab.BBBB2222.mjs';

describe('engine pinning', () => {
	it('pins the build a run starts on, and it stays loadable after the server dropped it', async () => {
		const server = new Set([V1]);
		const { caches, stores } = fakeCaches(server);
		expect(await pinEngine(V1, caches)).toBe(true);
		server.delete(V1); // the app updated: v1 is gone from the server
		expect(await enginePinned(V1, caches)).toBe(true);
		expect(await pinEngine(V1, caches)).toBe(true); // already cached: no fetch needed
		expect([...stores.keys()]).toEqual([ENGINE_CACHE]);
	});

	it('reports failure when the build cannot be fetched (offline before it was cached)', async () => {
		const { caches } = fakeCaches(new Set());
		expect(await pinEngine(V1, caches)).toBe(false);
		expect(await pinEngine(V1, null)).toBe(false);
	});

	it('garbage-collects builds no unfinished run needs', async () => {
		const { caches } = fakeCaches(new Set([V1, V2]));
		await pinEngine(V1, caches);
		await pinEngine(V2, caches);
		expect(await gcEngines(enginesToKeep(V2, V1), caches)).toEqual([]); // a run still plays on v1
		expect(await gcEngines(enginesToKeep(V2, null), caches)).toEqual([V1]); // that run ended
		expect(await enginePinned(V1, caches)).toBe(false);
		expect(await enginePinned(V2, caches)).toBe(true);
	});

	it('keeps the current build and, when different, the active run build', () => {
		expect(enginesToKeep(V2, undefined)).toEqual([V2]);
		expect(enginesToKeep(V2, V2)).toEqual([V2]);
		expect(enginesToKeep(V2, V1)).toEqual([V2, V1]);
	});

	it('the service worker keeps the engine cache when a new version activates', () => {
		expect(keepCache(ENGINE_CACHE, 'tradelab-2')).toBe(true);
		expect(keepCache('tradelab-2', 'tradelab-2')).toBe(true);
		expect(keepCache('tradelab-1', 'tradelab-2')).toBe(false);
	});

	it('labels a build by its content hash', () => {
		expect(engineBuildLabel(V1)).toBe('AAAA1111');
		expect(engineBuildLabel('/src/lib/engine/wasm/tradelab.mjs')).toBe('tradelab.mjs');
	});
});
