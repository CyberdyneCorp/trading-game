// Catalog screen view-model (web-client tasks 4.4, 5.3, 5.5): reads the catalog manifest, downloads
// packs with resumable Range requests (Background Fetch for an installed app where available),
// checks size and SHA-256, then hands the bytes to the session's verify-then-install.
//
// Offline, or when the manifest cannot be read, the last catalog read online is shown with its
// downloads disabled ("needs a connection"), and installed packs stay playable.

import type { PackCheck } from '#lib/engine/types.ts';
import { CATALOG_URL, entryStatus, parseCatalog, spaceShortfall, type CatalogEntry, type EntryStatus } from './catalog.ts';
import {
	backgroundId,
	BACKGROUND_CACHE,
	DownloadError,
	downloadPack,
	sha256Hex,
	useBackgroundFetch,
	type BackgroundEnv,
	type BackgroundFetchManagerLike,
	type DownloadFailure,
	type PartialStore
} from './download.ts';
import { storageQuota, type Quota } from './packStore.ts';

export interface CatalogStore extends PartialStore {
	partialSizes(): Promise<Map<string, number>>;
	cachedCatalog(): Promise<{ url: string; json: unknown } | null>;
	saveCatalog(url: string, json: unknown): Promise<void>;
}

/** What the catalog needs from the game session. */
export interface CatalogHost {
	readonly installedDigests: ReadonlySet<string>;
	readonly catalogStore: CatalogStore | null;
	installPack(bytes: Uint8Array, label: string, origin: 'catalog'): Promise<PackCheck>;
	online(): boolean;
}

/** 'ready': fresh from the network; 'offline' / 'unavailable': last known catalog (maybe none). */
export type CatalogStatus = 'idle' | 'loading' | 'ready' | 'offline' | 'unavailable';

export type DownloadPhase = 'downloading' | 'background' | 'verifying' | 'failed' | 'refused' | 'noSpace';

export interface DownloadView {
	phase: DownloadPhase;
	received: number;
	size: number;
	/** failed: why the download stopped; refused: the engine's pack reason; noSpace: bytes missing. */
	reason?: DownloadFailure | string;
	shortfall?: number;
}

export class CatalogModel {
	status = $state<CatalogStatus>('idle');
	entries = $state<CatalogEntry[]>([]);
	/** Bytes already received of unfinished downloads, by digest. */
	partial = $state<Map<string, number>>(new Map());
	downloads = $state<Record<string, DownloadView>>({});
	quota = $state<Quota | null>(null);
	private aborts = new Map<string, AbortController>();
	private listening = false;

	constructor(
		private host: CatalogHost,
		private url: string = CATALOG_URL,
		private doFetch: typeof fetch = (...a) => fetch(...a)
	) {}

	statusOf(e: CatalogEntry): EntryStatus {
		return entryStatus(e, this.host.installedDigests, this.partial);
	}

	async load(): Promise<void> {
		this.status = 'loading';
		this.listenBackground();
		const store = this.host.catalogStore;
		this.partial = (await store?.partialSizes().catch(() => null)) ?? new Map();
		this.quota = await storageQuota();
		const abs = new URL(this.url, typeof location === 'undefined' ? 'http://localhost/' : location.href).href;
		const fresh = this.host.online() ? await this.fetchCatalog(abs) : null;
		if (fresh) {
			this.entries = fresh.entries;
			this.status = 'ready';
			await this.collectBackground();
			return;
		}
		const cached = await store?.cachedCatalog().catch(() => null);
		this.entries = (cached && parseCatalog(cached.json, cached.url)?.entries) || [];
		this.status = this.host.online() ? 'unavailable' : 'offline';
	}

	private async fetchCatalog(abs: string) {
		try {
			const res = await this.doFetch(abs, { cache: 'no-store' });
			if (!res.ok) return null;
			const json: unknown = await res.json();
			const catalog = parseCatalog(json, abs);
			if (catalog) await this.host.catalogStore?.saveCatalog(abs, json).catch(() => undefined);
			return catalog;
		} catch {
			return null;
		}
	}

	/** Starts or resumes a download; installs the pack when it is complete and verified. */
	async download(e: CatalogEntry, label: string): Promise<void> {
		const store = this.host.catalogStore;
		if (!store || this.busy(e)) return;
		const received = this.partial.get(e.digest) ?? 0;
		const shortfall = spaceShortfall(this.quota, e.size - received);
		if (shortfall > 0) return this.set(e, { phase: 'noSpace', received, size: e.size, shortfall });
		if (useBackgroundFetch(await backgroundEnv(), received) && (await this.startBackground(e, label))) return;
		const abort = new AbortController();
		this.aborts.set(e.digest, abort);
		this.set(e, { phase: 'downloading', received, size: e.size });
		try {
			const bytes = await downloadPack({
				url: e.url,
				digest: e.digest,
				size: e.size,
				store,
				fetch: this.doFetch,
				signal: abort.signal,
				onProgress: (n) => this.set(e, { phase: 'downloading', received: n, size: e.size })
			});
			await this.install(e, bytes, label);
		} catch (err) {
			const reason: DownloadFailure = err instanceof DownloadError ? err.reason : 'interrupted';
			this.set(e, { phase: 'failed', received: this.downloads[e.digest]?.received ?? 0, size: e.size, reason });
		} finally {
			this.aborts.delete(e.digest);
			await this.refreshPartial();
		}
	}

	/** Stops a foreground download; what arrived stays stored for a resume. */
	pause(e: CatalogEntry): void {
		this.aborts.get(e.digest)?.abort();
	}

	busy(e: CatalogEntry): boolean {
		const p = this.downloads[e.digest]?.phase;
		return p === 'downloading' || p === 'verifying' || p === 'background';
	}

	private async install(e: CatalogEntry, bytes: Uint8Array, label: string): Promise<void> {
		this.set(e, { phase: 'verifying', received: e.size, size: e.size });
		const check = await this.host.installPack(bytes, label, 'catalog');
		await this.host.catalogStore?.clearPartial(e.digest).catch(() => undefined);
		if (check.ok) {
			const rest = { ...this.downloads };
			delete rest[e.digest];
			this.downloads = rest;
		} else {
			this.set(e, { phase: 'refused', received: e.size, size: e.size, reason: check.reason });
		}
	}

	private set(e: CatalogEntry, v: DownloadView): void {
		this.downloads = { ...this.downloads, [e.digest]: v };
	}

	private async refreshPartial(): Promise<void> {
		this.partial = (await this.host.catalogStore?.partialSizes().catch(() => null)) ?? this.partial;
	}

	// ---- Background Fetch (installed app, fresh downloads only) ----

	private labels = new Map<string, string>();

	private async startBackground(e: CatalogEntry, label: string): Promise<boolean> {
		const env = await backgroundEnv();
		try {
			await env.manager?.fetch(backgroundId(e.digest), [e.url], { title: label, downloadTotal: e.size });
		} catch {
			return false; // e.g. quota or a duplicate id: fall back to the foreground download
		}
		this.labels.set(e.digest, label);
		this.set(e, { phase: 'background', received: 0, size: e.size });
		return true;
	}

	private listenBackground(): void {
		if (this.listening || typeof navigator === 'undefined' || !navigator.serviceWorker) return;
		this.listening = true;
		navigator.serviceWorker.addEventListener('message', (ev: MessageEvent) => {
			const d = ev.data as { type?: string; id?: string; ok?: boolean } | null;
			if (d?.type !== 'bgfetch' || !d.id) return;
			const e = this.entries.find((x) => backgroundId(x.digest) === d.id);
			if (!e) return;
			if (d.ok) void this.collectBackground();
			else this.set(e, { phase: 'failed', received: 0, size: e.size, reason: 'interrupted' });
		});
	}

	/** Installs packs a background fetch finished (also after the app was closed meanwhile). */
	async collectBackground(): Promise<void> {
		if (typeof caches === 'undefined') return;
		const cache = await caches.open(BACKGROUND_CACHE).catch(() => null);
		if (!cache) return;
		for (const e of this.entries) {
			const hit = await cache.match(e.url).catch(() => undefined);
			if (!hit) continue;
			await cache.delete(e.url);
			const bytes = new Uint8Array(await hit.arrayBuffer());
			const ok = bytes.length === e.size && (await sha256Hex(bytes)) === e.digest;
			if (ok) await this.install(e, bytes, this.labels.get(e.digest) ?? e.id);
			else this.set(e, { phase: 'failed', received: 0, size: e.size, reason: 'hash_mismatch' });
		}
	}
}

async function backgroundEnv(): Promise<BackgroundEnv> {
	const none: BackgroundEnv = { manager: null, controlled: false, standalone: false };
	if (typeof navigator === 'undefined' || !navigator.serviceWorker) return none;
	const reg = await navigator.serviceWorker.getRegistration().catch(() => undefined);
	const manager = (reg as unknown as { backgroundFetch?: BackgroundFetchManagerLike } | undefined)?.backgroundFetch ?? null;
	return { manager, controlled: !!navigator.serviceWorker.controller, standalone: matchMedia('(display-mode: standalone)').matches };
}
