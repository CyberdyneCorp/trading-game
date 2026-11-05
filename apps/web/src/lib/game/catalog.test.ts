import { describe, expect, it } from 'vitest';
import { entryStatus, fmtBytes, localized, parseCatalog, spaceShortfall } from './catalog.ts';
import { sha256Hex } from './download.ts';
import { PINNED_PUBLISHER_KEYS } from './packs.ts';

// node:fs without @types/node (the web app does not depend on it); tests run in Node.
const NODE_FS = 'node:fs';
const { readFileSync } = (await import(/* @vite-ignore */ NODE_FS)) as { readFileSync(path: URL): Uint8Array };

const BASE = 'https://tradelab.example/catalog/catalog.json';
const D1 = 'a'.repeat(64);
const D2 = 'b'.repeat(64);

function row(over: Record<string, unknown> = {}) {
	return {
		id: 'p1', file: `packs/${D1}.tlpack`, digest: D1, size: 1000, scenarioId: 'csv-1', keyId: 'k',
		intervalSeconds: 3600, bars: 100, leverageCap: 5, title: { 'pt-BR': 'Mercado A', en: 'Market A' }, summary: { en: 'S' },
		...over
	};
}

describe('parseCatalog', () => {
	it('reads entries and resolves pack files against the manifest URL', () => {
		const c = parseCatalog({ schemaVersion: 1, packs: [row()] }, BASE)!;
		expect(c.entries).toHaveLength(1);
		expect(c.entries[0].url).toBe(`https://tradelab.example/catalog/packs/${D1}.tlpack`);
		expect(c.entries[0].summary).toEqual({ en: 'S' });
	});

	it('refuses a wrong schema version or shape as a whole', () => {
		expect(parseCatalog({ schemaVersion: 2, packs: [row()] }, BASE)).toBeNull();
		expect(parseCatalog({ schemaVersion: 1 }, BASE)).toBeNull();
		expect(parseCatalog('nope', BASE)).toBeNull();
	});

	it('skips malformed entries and duplicate digests, keeping the rest', () => {
		const packs = [
			row(),
			row({ id: 'dup' }),
			row({ digest: 'XYZ' }),
			row({ digest: D2, size: 0 }),
			row({ digest: D2, size: 17 * 1024 * 1024 }), // over the engine's pack limit
			row({ digest: D2, title: {} }),
			row({ digest: D2, id: 'p2' })
		];
		expect(parseCatalog({ schemaVersion: 1, packs }, BASE)!.entries.map((e) => e.id)).toEqual(['p1', 'p2']);
	});
});

describe('catalog helpers', () => {
	it('picks the player language, else the other one', () => {
		expect(localized({ 'pt-BR': 'A', en: 'B' }, 'en')).toBe('B');
		expect(localized({ en: 'B' }, 'pt-BR')).toBe('B');
	});

	it('classifies entries as installed, partial or not downloaded', () => {
		const e = parseCatalog({ schemaVersion: 1, packs: [row()] }, BASE)!.entries[0];
		expect(entryStatus(e, new Set([D1]), new Map())).toBe('installed');
		expect(entryStatus(e, new Set(), new Map([[D1, 10]]))).toBe('partial');
		expect(entryStatus(e, new Set(), new Map([[D1, 0]]))).toBe('none');
	});

	it('blocks a download that does not fit the quota and says how much is missing', () => {
		expect(spaceShortfall(null, 1000)).toBe(0);
		expect(spaceShortfall({ usage: 0, quota: 0 }, 1000)).toBe(0); // unknown quota never blocks
		expect(spaceShortfall({ usage: 500, quota: 10_000 }, 1000)).toBe(0);
		expect(spaceShortfall({ usage: 9_000, quota: 10_000 }, 1000)).toBe(1000);
	});

	it('formats sizes with the locale decimal mark', () => {
		expect(fmtBytes(1.5 * 1024 * 1024, 'pt-BR')).toBe('1,5 MB');
		expect(fmtBytes(1.5 * 1024 * 1024, 'en')).toBe('1.5 MB');
		expect(fmtBytes(101_888, 'en')).toBe('100 KB');
	});
});

describe('published sample catalog (static/catalog)', () => {
	const dir = new URL('../../../static/catalog/', import.meta.url);
	const json = JSON.parse(new TextDecoder().decode(readFileSync(new URL('catalog.json', dir))));
	const catalog = parseCatalog(json, BASE)!;

	it('is a valid catalog whose every row parses', () => {
		expect(catalog.entries.length).toBe(json.packs.length);
		expect(catalog.entries.length).toBeGreaterThan(0);
	});

	it('lists files whose size and SHA-256 match, signed by a pinned publisher key', async () => {
		const pinned = PINNED_PUBLISHER_KEYS.map((k) => k.id);
		for (const e of json.packs as { file: string; size: number; digest: string; keyId: string }[]) {
			const bytes = new Uint8Array(readFileSync(new URL(e.file, dir)));
			expect(bytes.length).toBe(e.size);
			expect(await sha256Hex(bytes)).toBe(e.digest);
			expect(pinned).toContain(e.keyId);
		}
	});

	it('has a pack larger than one download chunk, so resuming is exercised', () => {
		expect(catalog.entries.some((e) => e.size > 64 * 1024)).toBe(true);
	});
});
