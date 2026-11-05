// Engine pinning (spec web-client "Safe web updates", offline-sync engine pinning; design D4a).
//
// Each app version ships one content-hashed engine module. A run records the build it started on
// and copies it into a dedicated cache that survives app updates (the service worker deletes only
// its per-version caches), so an unfinished run resumes on its own engine after an update, also
// offline. Builds no unfinished run needs are garbage-collected.

import { ENGINE_CACHE } from './swRoutes.ts';

/** The cache side of CacheStorage this module needs (a fake in unit tests). */
export interface EngineCaches {
	open(name: string): Promise<Pick<Cache, 'match' | 'add' | 'keys' | 'delete'>>;
}

const browserCaches = (): EngineCaches | null => (typeof caches === 'undefined' ? null : caches);

/** Keeps `path` (an engine build) in the engine cache; true when it is cached afterwards. */
export async function pinEngine(path: string, store: EngineCaches | null = browserCaches()): Promise<boolean> {
	if (!store) return false;
	try {
		const cache = await store.open(ENGINE_CACHE);
		if (!(await cache.match(path))) await cache.add(path);
		return true;
	} catch {
		return false; // offline before the build was ever cached, or no Cache API: resume falls back
	}
}

/** True when the engine build at `path` can be loaded without the network. */
export async function enginePinned(path: string, store: EngineCaches | null = browserCaches()): Promise<boolean> {
	if (!store) return false;
	try {
		return !!(await (await store.open(ENGINE_CACHE)).match(path));
	} catch {
		return false;
	}
}

/** Deletes every pinned build not in `keep` (paths); returns the paths removed. */
export async function gcEngines(keep: readonly string[], store: EngineCaches | null = browserCaches()): Promise<string[]> {
	if (!store) return [];
	const removed: string[] = [];
	try {
		const cache = await store.open(ENGINE_CACHE);
		for (const req of await cache.keys()) {
			const path = new URL(req.url).pathname;
			if (keep.includes(path)) continue;
			await cache.delete(req);
			removed.push(path);
		}
	} catch {
		// no Cache API: nothing was pinned
	}
	return removed;
}

/** The builds to keep: this version's and the one the resumable run (if any) started on. */
export function enginesToKeep(current: string, activeRunEngine: string | null | undefined): string[] {
	return activeRunEngine && activeRunEngine !== current ? [current, activeRunEngine] : [current];
}
