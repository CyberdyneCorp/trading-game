// Pack catalog (web-client tasks 4.4, 5.3, 5.5; spec web-client "Installable offline PWA").
//
// The catalog is a static JSON manifest listing packs published as static files. It is read from
// CATALOG_URL (build-time `VITE_CATALOG_URL`; the backend catalog endpoint comes later and only
// changes this URL). The manifest is only a list: it never grants trust. Every downloaded pack is
// checked against the manifest's SHA-256, then verified by the engine against the pinned publisher
// keys before it is installed (verify-then-install).

import type { Locale } from './i18n.svelte.ts';
import { PACK_MAX_BYTES } from './packs.ts';
import type { Quota } from './packStore.ts';

export const CATALOG_SCHEMA = 1;

/** Where the catalog manifest is read from; relative URLs resolve against the page. */
export const CATALOG_URL: string = import.meta.env?.VITE_CATALOG_URL || '/catalog/catalog.json';

export type LocalText = Partial<Record<Locale, string>>;

export interface CatalogEntry {
	id: string;
	/** Absolute URL of the .tlpack (resolved against the manifest's URL). */
	url: string;
	/** SHA-256 of the whole pack (lowercase hex): the pack store key. */
	digest: string;
	size: number;
	scenarioId: string;
	keyId: string;
	intervalSeconds: number;
	bars: number;
	leverageCap: number;
	title: LocalText;
	summary: LocalText;
}

export interface Catalog {
	entries: CatalogEntry[];
}

const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v);
const posInt = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v) && v > 0;
const str = (v: unknown, max = 200): v is string => typeof v === 'string' && v.length > 0 && v.length <= max;

function localText(v: unknown): LocalText | null {
	if (!isObj(v)) return null;
	const out: LocalText = {};
	if (str(v['pt-BR'], 400)) out['pt-BR'] = v['pt-BR'];
	if (str(v.en, 400)) out.en = v.en;
	return out['pt-BR'] || out.en ? out : null;
}

function entry(v: unknown, base: string): CatalogEntry | null {
	if (!isObj(v) || !str(v.id, 80) || !str(v.file, 300) || !str(v.scenarioId, 80) || !str(v.keyId, 80)) return null;
	if (typeof v.digest !== 'string' || !/^[0-9a-f]{64}$/.test(v.digest)) return null;
	if (!posInt(v.size) || v.size > PACK_MAX_BYTES || !posInt(v.intervalSeconds) || !posInt(v.bars) || !posInt(v.leverageCap)) return null;
	const title = localText(v.title);
	if (!title) return null;
	let url: string;
	try {
		url = new URL(v.file, base).href;
	} catch {
		return null;
	}
	const { id, digest, size, scenarioId, keyId, intervalSeconds, bars, leverageCap } = v;
	return { id, url, digest, size, scenarioId, keyId, intervalSeconds, bars, leverageCap, title, summary: localText(v.summary) ?? {} };
}

/**
 * Reads a catalog manifest fetched from `base` (its absolute URL). A wrong schema version or shape
 * refuses the whole catalog (null); a malformed entry is skipped, so one bad row never hides the rest.
 */
export function parseCatalog(json: unknown, base: string): Catalog | null {
	if (!isObj(json) || json.schemaVersion !== CATALOG_SCHEMA || !Array.isArray(json.packs)) return null;
	const entries = json.packs.map((p) => entry(p, base)).filter((e): e is CatalogEntry => e !== null);
	return { entries: entries.filter((e, i) => entries.findIndex((x) => x.digest === e.digest) === i) };
}

/** Text in the player's language, else the other one. */
export function localized(text: LocalText, locale: Locale): string {
	return text[locale] ?? text['pt-BR'] ?? text.en ?? '';
}

export type EntryStatus = 'installed' | 'partial' | 'none';

export function entryStatus(e: CatalogEntry, installed: ReadonlySet<string>, partial: ReadonlyMap<string, number>): EntryStatus {
	if (installed.has(e.digest)) return 'installed';
	return (partial.get(e.digest) ?? 0) > 0 ? 'partial' : 'none';
}

/**
 * Bytes still missing to download `remaining` bytes and keep the installed copy (the partial
 * chunks are dropped once the pack is installed, so the peak is about twice the remaining size).
 * 0 = it fits; an unknown quota never blocks.
 */
export function spaceShortfall(q: Quota | null, remaining: number): number {
	if (!q || !(q.quota > 0)) return 0;
	const need = remaining * 2;
	const free = Math.max(0, q.quota - q.usage);
	return need > free ? need - free : 0;
}

/** "1,2 MB" / "98 KB" in the player's locale. */
export function fmtBytes(n: number, locale: Locale): string {
	const mb = n / (1024 * 1024);
	if (mb >= 1) return `${mb.toLocaleString(locale, { maximumFractionDigits: 1 })} MB`;
	return `${Math.max(1, Math.round(n / 1024)).toLocaleString(locale)} KB`;
}
