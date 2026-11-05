// Resumable pack downloads (web-client task 5.3; design "Downloads use HTTP Range so they resume").
//
// A pack is fetched in fixed-size byte ranges and every received chunk is committed to IndexedDB
// before the next request, so a lost connection, a closed tab or a crash keeps what arrived; the
// next attempt asks only for the missing bytes. A server that ignores Range (200 with the whole
// file) is accepted too. The complete file must match the catalog's size and SHA-256 before it is
// handed to the engine for verification; a mismatch drops the partial data.

/** Bytes per Range request (each chunk is one IndexedDB write). */
export const CHUNK_BYTES = 64 * 1024;

/** Where partial downloads live between attempts (the `meta` store, see storage.ts). */
export interface PartialStore {
	/** Chunks already received for `digest`, in order. */
	partialChunks(digest: string): Promise<Uint8Array[]>;
	savePartialChunk(digest: string, index: number, bytes: Uint8Array): Promise<void>;
	clearPartial(digest: string): Promise<void>;
}

export type DownloadFailure = 'interrupted' | 'http' | 'size_mismatch' | 'hash_mismatch' | 'aborted';

export class DownloadError extends Error {
	constructor(
		readonly reason: DownloadFailure,
		readonly received = 0
	) {
		super(`download ${reason}`);
	}
}

export interface DownloadJob {
	url: string;
	digest: string;
	size: number;
	store: PartialStore;
	fetch?: typeof fetch;
	chunkBytes?: number;
	signal?: AbortSignal;
	onProgress?: (received: number, size: number) => void;
}

/** `bytes=a-b` for the next chunk after `received` bytes. */
export function rangeHeader(received: number, size: number, chunk: number): string {
	return `bytes=${received}-${Math.min(size, received + chunk) - 1}`;
}

/** `bytes a-b/total` -> numbers; null when absent or malformed. */
export function parseContentRange(h: string | null): { start: number; end: number; total: number | null } | null {
	const m = h ? /^bytes (\d+)-(\d+)\/(\d+|\*)$/.exec(h.trim()) : null;
	if (!m) return null;
	return { start: Number(m[1]), end: Number(m[2]), total: m[3] === '*' ? null : Number(m[3]) };
}

export function concat(chunks: Uint8Array[]): Uint8Array {
	const out = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
	let at = 0;
	for (const c of chunks) {
		out.set(c, at);
		at += c.length;
	}
	return out;
}

export async function sha256Hex(bytes: Uint8Array): Promise<string> {
	const d = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes as BufferSource));
	return [...d].map((b) => b.toString(16).padStart(2, '0')).join('');
}

/** Bytes already stored for a download (0 when none). */
export async function receivedBytes(store: PartialStore, digest: string): Promise<number> {
	return (await store.partialChunks(digest)).reduce((n, c) => n + c.length, 0);
}

interface Step {
	chunk: Uint8Array;
	/** True when the server sent the whole file (Range ignored): it replaces the stored chunks. */
	whole: boolean;
}

// One request for the bytes after `received`; throws DownloadError on anything unusable.
async function fetchStep(job: DownloadJob, received: number): Promise<Step> {
	const doFetch = job.fetch ?? fetch;
	let res: Response;
	let body: Uint8Array;
	try {
		res = await doFetch(job.url, { headers: { Range: rangeHeader(received, job.size, job.chunkBytes ?? CHUNK_BYTES) }, signal: job.signal, cache: 'no-store' });
		if (!res.ok) throw new DownloadError('http', received);
		body = new Uint8Array(await res.arrayBuffer());
	} catch (e) {
		if (e instanceof DownloadError) throw e;
		throw new DownloadError(job.signal?.aborted ? 'aborted' : 'interrupted', received);
	}
	if (res.status === 200) {
		if (body.length !== job.size) throw new DownloadError('size_mismatch', received);
		return { chunk: body, whole: true };
	}
	const range = parseContentRange(res.headers.get('content-range'));
	const fits = range && range.start === received && range.end - range.start + 1 === body.length && body.length > 0;
	if (!fits || (range.total !== null && range.total !== job.size)) throw new DownloadError('size_mismatch', received);
	return { chunk: body, whole: false };
}

/**
 * Downloads (or resumes) a pack and returns its verified bytes. Stored chunks are kept on
 * 'interrupted', 'aborted' and 'http' so a later call resumes; they are dropped on a size or hash
 * mismatch, which would otherwise repeat forever.
 */
export async function downloadPack(job: DownloadJob): Promise<Uint8Array> {
	const chunks = await storedChunks(job);
	let received = chunks.reduce((n, c) => n + c.length, 0);
	job.onProgress?.(received, job.size);
	while (received < job.size) {
		const step = await fetchStep(job, received).catch(async (e: unknown) => {
			if (e instanceof DownloadError && e.reason === 'size_mismatch') await job.store.clearPartial(job.digest);
			throw e;
		});
		if (step.whole) {
			await job.store.clearPartial(job.digest);
			chunks.length = 0;
			received = 0;
		}
		await job.store.savePartialChunk(job.digest, chunks.length, step.chunk);
		chunks.push(step.chunk);
		received += step.chunk.length;
		job.onProgress?.(received, job.size);
	}
	const bytes = concat(chunks);
	if ((await sha256Hex(bytes)) !== job.digest) {
		await job.store.clearPartial(job.digest);
		throw new DownloadError('hash_mismatch', 0);
	}
	return bytes;
}

// The chunks stored by an earlier attempt; more bytes than the file holds means stale data.
async function storedChunks(job: DownloadJob): Promise<Uint8Array[]> {
	const chunks = await job.store.partialChunks(job.digest);
	if (chunks.reduce((n, c) => n + c.length, 0) <= job.size) return chunks;
	await job.store.clearPartial(job.digest);
	return [];
}

/** Minimal shape of the Background Fetch API (not in the TypeScript DOM library). */
export interface BackgroundFetchManagerLike {
	fetch(id: string, requests: string[], options?: { title?: string; downloadTotal?: number }): Promise<unknown>;
	get(id: string): Promise<unknown>;
}

export interface BackgroundEnv {
	manager: BackgroundFetchManagerLike | null;
	/** A service worker controls the page (it receives the finished download). */
	controlled: boolean;
	/** Installed app (display-mode standalone): the case where a download outliving the tab matters. */
	standalone: boolean;
}

/**
 * Background Fetch is used where available, for an installed app controlled by the service worker,
 * and only for a fresh download: a partial one resumes in the foreground with Range, since a
 * background fetch cannot start from stored bytes.
 */
export function useBackgroundFetch(env: BackgroundEnv, received: number): boolean {
	return env.manager !== null && env.controlled && env.standalone && received === 0;
}

/** Background fetch id of a pack download (also the key the service worker reports back). */
export const backgroundId = (digest: string) => `pack-${digest}`;
export const BACKGROUND_CACHE = 'tradelab-downloads-v1';
