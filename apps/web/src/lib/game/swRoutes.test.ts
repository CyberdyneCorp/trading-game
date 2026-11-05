import { describe, expect, it } from 'vitest';
import { isCatalog, rangeResponse, swRoute } from './swRoutes.ts';

const origin = 'https://tradelab.example';
const url = (path: string, base = origin) => new URL(path, base);

describe('swRoute', () => {
	it('serves static same-origin GETs from the cache and navigations network-first', () => {
		expect(swRoute('GET', 'no-cors', url('/_app/immutable/app.js'), origin)).toBe('cache');
		expect(swRoute('GET', 'navigate', url('/?seed=3'), origin)).toBe('navigate');
	});

	it('regression: never caches the API (was cache-first for every same-origin GET)', () => {
		expect(swRoute('GET', 'cors', url('/api/v1/progress'), origin)).toBe('network');
		expect(swRoute('GET', 'navigate', url('/api/v1/auth/callback?code=x'), origin)).toBe('network');
		expect(swRoute('GET', 'cors', url('/api'), origin)).toBe('network');
		expect(swRoute('GET', 'no-cors', url('/apiary.png'), origin)).toBe('cache');
	});

	it('leaves non-GET and cross-origin requests to the network', () => {
		expect(swRoute('POST', 'cors', url('/api/v1/sync/batches'), origin)).toBe('network');
		expect(swRoute('GET', 'cors', url('/x.js', 'https://cdn.example'), origin)).toBe('network');
	});
});

describe('catalog', () => {
	it('never caches the catalog manifest or packs (a Range response cannot be cached, the manifest must be fresh)', () => {
		expect(swRoute('GET', 'cors', url('/catalog/catalog.json'), origin)).toBe('network');
		expect(swRoute('GET', 'cors', url('/catalog/packs/abc.tlpack'), origin)).toBe('network');
		expect(swRoute('GET', 'no-cors', url('/catalogue.png'), origin)).toBe('cache');
	});

	it('keeps the catalog out of the install precache', () => {
		expect(isCatalog('/catalog/catalog.json')).toBe(true);
		expect(isCatalog('/catalog/packs/x.tlpack')).toBe(true);
		expect(isCatalog('/icons/icon-192.png')).toBe(false);
		expect(isCatalog('/catalogue.json')).toBe(false);
	});

	// Playtest: the build manifest lists assets without a leading slash ("catalog/catalog.json"), so
	// the precache filter let the manifest and every sample pack into the install cache.
	it('regression: recognizes the build manifest asset paths, which have no leading slash', () => {
		expect(isCatalog('catalog/catalog.json')).toBe(true);
		expect(isCatalog('catalog/packs/x.tlpack')).toBe(true);
		expect(isCatalog('icons/icon-192.png')).toBe(false);
	});
});

describe('audio', () => {
	it('routes /audio/* to the media handler (not the install precache)', () => {
		expect(swRoute('GET', 'no-cors', url('/audio/game-1.mp3'), origin)).toBe('media');
	});

	it('serves byte ranges of a cached file as 206, whole file without Range, 416 when out of range', async () => {
		const full = () => new Response(new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]), { headers: { 'Content-Type': 'audio/mpeg' } });
		const part = await rangeResponse(full(), 'bytes=2-5');
		expect(part.status).toBe(206);
		expect(part.headers.get('Content-Range')).toBe('bytes 2-5/10');
		expect([...new Uint8Array(await part.arrayBuffer())]).toEqual([2, 3, 4, 5]);
		const open = await rangeResponse(full(), 'bytes=7-');
		expect([...new Uint8Array(await open.arrayBuffer())]).toEqual([7, 8, 9]);
		const tail = await rangeResponse(full(), 'bytes=-2');
		expect(tail.headers.get('Content-Range')).toBe('bytes 8-9/10');
		expect((await rangeResponse(full(), null)).status).toBe(200);
		expect((await rangeResponse(full(), 'bytes=20-')).status).toBe(416);
	});
});
