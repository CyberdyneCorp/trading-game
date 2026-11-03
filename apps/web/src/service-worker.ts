/// <reference no-default-lib="true"/>
/// <reference lib="esnext" />
/// <reference lib="webworker" />
// Offline PWA (spec web-client "Installable offline PWA", "Safe web updates").
// Precaches the versioned build (app shell, fonts, WASM engine embedded in the JS chunks) so the
// game starts with no network after one online visit. A new version installs in the background
// and waits; the page activates it only when the player confirms (never mid-mission).

import { self } from '$app/service-worker';
import { assets, immutable, prerendered } from '$app/manifest';
import { version } from '$app/env';
import { swRoute } from '#lib/game/swRoutes.ts';

const CACHE = `tradelab-${version}`;
const SHELL = new URL('./', self.registration.scope).href;
const FILES = [...immutable, ...assets, ...prerendered].map((f) => new URL(f.path, self.registration.scope).href);

self.addEventListener('install', (event) => {
	event.waitUntil(caches.open(CACHE).then((cache) => cache.addAll([...new Set([SHELL, ...FILES])])));
});

self.addEventListener('activate', (event) => {
	event.waitUntil(
		(async () => {
			for (const key of await caches.keys()) if (key !== CACHE) await caches.delete(key);
			await self.clients.claim();
		})()
	);
});

self.addEventListener('message', (event) => {
	if (event.data === 'SKIP_WAITING') void self.skipWaiting();
});

async function navigate(request: Request): Promise<Response> {
	try {
		const fresh = await fetch(request);
		if (fresh.ok) return fresh;
	} catch {
		// offline: fall back to the cached shell below
	}
	return (await caches.match(SHELL)) ?? Response.error();
}

async function cacheFirst(request: Request): Promise<Response> {
	const hit = await caches.match(request);
	if (hit) return hit;
	const res = await fetch(request);
	if (res.ok && new URL(request.url).origin === self.location.origin) {
		const cache = await caches.open(CACHE);
		await cache.put(request, res.clone());
	}
	return res;
}

self.addEventListener('fetch', (event) => {
	const { request } = event;
	const route = swRoute(request.method, request.mode, new URL(request.url), self.location.origin);
	if (route === 'network') return;
	event.respondWith(route === 'navigate' ? navigate(request) : cacheFirst(request));
});
