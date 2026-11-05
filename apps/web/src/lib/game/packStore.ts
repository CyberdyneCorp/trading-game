// Installed packs (spec web-client "Persistent offline storage", design D4a): the bytes go to the
// Origin Private File System as `packs/<digest>.tlpack`, the index (metadata) to IndexedDB. Where
// OPFS cannot write files (older Safari), the bytes stay in the IndexedDB index entry instead.
//
// Browsers may evict offline data. On start every index entry is checked against its bytes: a
// missing or truncated file marks the pack evicted, so it is shown as not on the device and no run
// starts from partial data (the engine also verifies the digest and signature on every start).

import type { StoredPack } from './storage.ts';

/** Where pack bytes live: a directory of files (OPFS) or nothing (IndexedDB only). */
export interface BlobDir {
	write(name: string, bytes: Uint8Array): Promise<void>;
	read(name: string): Promise<Uint8Array | null>;
	remove(name: string): Promise<void>;
}

/** The index side, implemented by RunStore. */
export interface PackIndex {
	savePack(p: StoredPack): Promise<void>;
	pack(digest: string): Promise<StoredPack | null>;
	packs(): Promise<StoredPack[]>;
	deletePack(digest: string): Promise<void>;
}

/** A pack as the menu lists it: metadata only, and whether its bytes are still on the device. */
export type PackEntry = Omit<StoredPack, 'bytes'> & { evicted: boolean };

/** Index for a session without IndexedDB (e.g. some private modes): packs last until the tab closes. */
export class MemoryPackIndex implements PackIndex {
	private rows = new Map<string, StoredPack>();
	async savePack(p: StoredPack): Promise<void> {
		this.rows.set(p.digest, p);
	}
	async pack(digest: string): Promise<StoredPack | null> {
		return this.rows.get(digest) ?? null;
	}
	async packs(): Promise<StoredPack[]> {
		return [...this.rows.values()].sort((a, b) => a.addedAt - b.addedAt);
	}
	async deletePack(digest: string): Promise<void> {
		this.rows.delete(digest);
	}
}

const fileName = (digest: string) => `${digest}.tlpack`;

function entry(p: StoredPack, evicted: boolean): PackEntry {
	const { bytes: _bytes, ...meta } = p;
	return { ...meta, evicted };
}

export class PackLibrary {
	constructor(
		private index: PackIndex,
		private dir: BlobDir | null
	) {}

	/** 'opfs' when pack files go to the Origin Private File System, else 'idb'. */
	get backend(): 'opfs' | 'idb' {
		return this.dir ? 'opfs' : 'idb';
	}

	/** Bytes first, index last: an index entry never points at a file that was not written. */
	async install(meta: Omit<StoredPack, 'bytes' | 'location' | 'size'>, bytes: Uint8Array): Promise<PackEntry> {
		const size = bytes.length;
		if (this.dir) {
			try {
				await this.dir.write(fileName(meta.digest), bytes);
				const p: StoredPack = { ...meta, location: 'opfs', size };
				await this.index.savePack(p);
				return entry(p, false);
			} catch {
				// OPFS refused the write (quota, unsupported): keep the bytes in IndexedDB instead.
			}
		}
		const p: StoredPack = { ...meta, location: 'idb', size, bytes };
		await this.index.savePack(p);
		return entry(p, false);
	}

	/** The pack's bytes, or null when they are gone or truncated (evicted). */
	async bytes(digest: string): Promise<Uint8Array | null> {
		const p = await this.index.pack(digest);
		return p ? this.read(p) : null;
	}

	/** Every installed pack, oldest first, each checked against its bytes. */
	async list(): Promise<PackEntry[]> {
		const out: PackEntry[] = [];
		for (const p of await this.index.packs()) out.push(entry(p, (await this.read(p)) === null));
		return out;
	}

	async remove(digest: string): Promise<void> {
		const p = await this.index.pack(digest);
		await this.index.deletePack(digest);
		if (p?.location === 'opfs' && this.dir) await this.dir.remove(fileName(digest)).catch(() => undefined);
	}

	private async read(p: StoredPack): Promise<Uint8Array | null> {
		const bytes = p.location === 'opfs' ? await this.dir?.read(fileName(p.digest)).catch(() => null) : p.bytes;
		if (!bytes || bytes.length === 0) return null;
		return p.size == null || bytes.length === p.size ? bytes : null;
	}
}

/** The OPFS `packs/` directory, or null when this browser cannot write OPFS files from a page. */
export async function opfsPackDir(storage: StorageManager | undefined = globalThis.navigator?.storage): Promise<BlobDir | null> {
	if (!storage?.getDirectory || typeof FileSystemFileHandle === 'undefined' || !('createWritable' in FileSystemFileHandle.prototype)) {
		return null;
	}
	try {
		const root = await storage.getDirectory();
		const dir = await root.getDirectoryHandle('packs', { create: true });
		return {
			async write(name, bytes) {
				const file = await dir.getFileHandle(name, { create: true });
				const w = await file.createWritable();
				await w.write(bytes as BufferSource);
				await w.close();
			},
			async read(name) {
				try {
					const file = await (await dir.getFileHandle(name)).getFile();
					return new Uint8Array(await file.arrayBuffer());
				} catch {
					return null; // NotFoundError: the file was evicted or removed
				}
			},
			async remove(name) {
				await dir.removeEntry(name);
			}
		};
	} catch {
		return null;
	}
}

// ---- quota (spec web-client "Persistent offline storage": show used and available quota) ----

export interface Quota {
	usage: number;
	quota: number;
}

export async function storageQuota(storage: StorageManager | undefined = globalThis.navigator?.storage): Promise<Quota | null> {
	if (!storage?.estimate) return null;
	const e = await storage.estimate().catch(() => null);
	return e && typeof e.quota === 'number' ? { usage: e.usage ?? 0, quota: e.quota } : null;
}

/** Bytes missing for an install of `size` bytes (0 when it fits, or when the quota is unknown). */
export function quotaShortfall(q: Quota | null, size: number): number {
	if (!q) return 0;
	return Math.max(0, q.usage + size - q.quota);
}

/** Byte count for people: KB / MB / GB with one decimal, in the player's locale (decimal comma in PT-BR). */
export function formatBytes(n: number, locale: string): string {
	const units = ['B', 'KB', 'MB', 'GB', 'TB'];
	let v = Math.max(0, n);
	let i = 0;
	while (v >= 1024 && i < units.length - 1) {
		v /= 1024;
		i++;
	}
	const digits = i === 0 ? 0 : 1;
	return `${v.toLocaleString(locale, { minimumFractionDigits: digits, maximumFractionDigits: digits })} ${units[i]}`;
}
