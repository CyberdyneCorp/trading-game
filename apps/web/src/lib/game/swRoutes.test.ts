import { describe, expect, it } from 'vitest';
import { swRoute } from './swRoutes.ts';

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
