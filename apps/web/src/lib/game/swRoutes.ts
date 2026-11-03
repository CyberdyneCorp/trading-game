// Service-worker routing (spec web-client "Installable offline PWA"). Pure so it can be unit-tested.
//   navigate: network first, cached shell offline
//   cache:    same-origin static GETs, cache first
//   network:  everything else, untouched — including /api/*, which must never be served from a
//             cache (session, sync and progress responses are per player and change).

export type SwRoute = 'navigate' | 'cache' | 'network';

export function swRoute(method: string, mode: RequestMode, url: URL, origin: string): SwRoute {
	if (method !== 'GET' || url.origin !== origin) return 'network';
	if (url.pathname === '/api' || url.pathname.startsWith('/api/')) return 'network';
	return mode === 'navigate' ? 'navigate' : 'cache';
}
