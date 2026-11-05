// Service-worker routing (spec web-client "Installable offline PWA"). Pure so it can be unit-tested.
//   navigate: network first, cached shell offline
//   cache:    same-origin static GETs, cache first
//   media:    /audio/* — not precached (19 MB); kept in its own cache after the first play and
//             served with byte ranges (media elements send Range requests; Safari needs 206)
//   network:  everything else, untouched — including /api/*, which must never be served from a
//             cache (session, sync and progress responses are per player and change), and
//             /catalog/*: the manifest must be fresh (offline, the app shows its own copy) and packs
//             are downloaded in byte ranges into IndexedDB, never into a cache.

import { BACKGROUND_CACHE } from './download.ts';

export type SwRoute = 'navigate' | 'cache' | 'media' | 'network';

export const AUDIO_CACHE = 'tradelab-audio-v1';
/** Engine builds pinned by unfinished runs (engineCache.ts); survives app updates. */
export const ENGINE_CACHE = 'tradelab-engines-v1';

/**
 * Caches an activating version keeps: its own, the audio cache, the pinned engine builds and the
 * Background Fetch results a catalog download left for the page to install.
 */
export function keepCache(name: string, current: string): boolean {
	return name === current || name === AUDIO_CACHE || name === ENGINE_CACHE || name === BACKGROUND_CACHE;
}

/** Audio files: a URL pathname ("/audio/…") or a build manifest asset path ("audio/…", no leading slash). */
export const isAudio = (path: string) => /(^|\/)audio\/[^/]+$/.test(path);

/**
 * Static pack catalog (manifest and .tlpack files): never precached, never cached. Takes a URL
 * pathname ("/catalog/…") or a build manifest asset path, which has no leading slash ("catalog/…").
 */
export const isCatalog = (path: string) => /^\/?catalog(\/|$)/.test(path);

export function swRoute(method: string, mode: RequestMode, url: URL, origin: string): SwRoute {
	if (method !== 'GET' || url.origin !== origin) return 'network';
	if (url.pathname === '/api' || url.pathname.startsWith('/api/')) return 'network';
	if (isCatalog(url.pathname)) return 'network';
	if (isAudio(url.pathname)) return 'media';
	return mode === 'navigate' ? 'navigate' : 'cache';
}

/**
 * Serves a byte range of a fully cached response: 206 with Content-Range for `bytes=a-b` /
 * `bytes=a-`, 416 when unsatisfiable, the whole response when there is no Range header.
 */
export async function rangeResponse(full: Response, range: string | null): Promise<Response> {
	if (!range) return full;
	const m = /^bytes=(\d*)-(\d*)$/.exec(range.trim());
	const body = await full.blob();
	const size = body.size;
	const start = m && m[1] !== '' ? Number(m[1]) : m && m[2] !== '' ? Math.max(0, size - Number(m[2])) : 0;
	const end = m && m[1] !== '' && m[2] !== '' ? Math.min(Number(m[2]), size - 1) : size - 1;
	if (!m || start >= size || start > end) {
		return new Response(null, { status: 416, headers: { 'Content-Range': `bytes */${size}` } });
	}
	const headers = new Headers(full.headers);
	headers.set('Content-Range', `bytes ${start}-${end}/${size}`);
	headers.set('Content-Length', String(end - start + 1));
	return new Response(body.slice(start, end + 1), { status: 206, headers });
}
