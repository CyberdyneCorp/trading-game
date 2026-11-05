import { describe, expect, it } from 'vitest';
import { csvIssueText, i18n, packReasonText } from './i18n.svelte.ts';
import { DEFAULT_META, defaultLabel, guessMeta, intervalLabel, isoUtc, packLooksCut, PINNED_PUBLISHER_KEYS, trustedKeys } from './packs.ts';

// Every CSV issue code the engine can emit (engine/src/csv_import.cpp header).
const CSV_CODES = [
	'file_too_large', 'invalid_interval', 'empty_file', 'missing_column:volume', 'too_many_rows', 'too_few_rows',
	'line_too_long', 'column_count', 'invalid_timestamp', 'timestamp_unit', 'invalid_number', 'non_positive_price',
	'negative_volume', 'invalid_funding', 'high_inconsistent', 'low_inconsistent', 'duplicate_timestamp',
	'not_increasing', 'interval_mismatch'
];

describe('guessMeta', () => {
	it('suggests milliseconds and the interval from the first two rows', () => {
		const csv = 'time,open,high,low,close,volume\n1700000000000,1,1,1,1,1\n1700003600000,1,1,1,1,1\n';
		expect(guessMeta(csv)).toMatchObject({ timestampUnit: 'ms', intervalSeconds: 3600 });
	});

	it('suggests seconds and keeps the declared interval when the step is unusual', () => {
		const csv = 'time,open,high,low,close,volume\n"1700000000",1,1,1,1,1\n1700000007,1,1,1,1,1\n';
		expect(guessMeta(csv, { ...DEFAULT_META, intervalSeconds: 900 })).toMatchObject({ timestampUnit: 's', intervalSeconds: 900 });
	});

	it('leaves ISO dates and short files to the player', () => {
		expect(guessMeta('time,open\n2024-01-01,1\n2024-01-02,1\n')).toEqual(DEFAULT_META);
		expect(guessMeta('')).toEqual(DEFAULT_META);
	});
});

describe('pack helpers', () => {
	it('labels a pack by pair (or asset, or file name) and interval', () => {
		expect(defaultLabel({ ...DEFAULT_META, pair: 'BTC/USDT', intervalSeconds: 14400 }, 'x.csv')).toBe('BTC/USDT · 4h');
		expect(defaultLabel({ ...DEFAULT_META, asset: 'ETH' }, 'x.csv')).toBe('ETH · 1h');
		expect(defaultLabel(DEFAULT_META, 'my-data.csv')).toBe('my-data · 1h');
	});

	it('regression: the top bar names the market interval (a 15-minute import said "1h")', () => {
		expect(intervalLabel(900)).toBe('15m');
		expect(intervalLabel(3600)).toBe('1h');
		expect(intervalLabel(120)).toBe('120s');
	});

	it('trusts the pinned publishers plus this device', () => {
		const device = { id: 'device-0123456789abcdef', key: 'ab'.repeat(32) };
		expect(trustedKeys(device)).toEqual([...PINNED_PUBLISHER_KEYS, device]);
		expect(trustedKeys(null)).toEqual([...PINNED_PUBLISHER_KEYS]);
	});

	it('formats UTC seconds as ISO without milliseconds', () => {
		expect(isoUtc(1_700_000_000)).toBe('2023-11-14T22:13:20Z');
	});
});

describe('CSV issue and pack reason texts', () => {
	it('every engine CSV issue code reads as plain language in both locales', () => {
		for (const locale of ['pt-BR', 'en'] as const) {
			i18n.locale = locale;
			for (const code of CSV_CODES) expect(csvIssueText(code), `${locale} ${code}`).not.toMatch(/_/);
		}
		i18n.locale = 'en';
		expect(csvIssueText('missing_column:volume')).toBe('missing column volume');
		expect(csvIssueText('brand_new')).toBe('brand_new');
		expect(packReasonText('hash_mismatch')).toBe('a data file was changed (hash mismatch)');
		expect(packReasonText('brand_new')).toBe('brand_new');
		i18n.locale = 'pt-BR';
	});
});

/** A minimal ustar archive like the engine's tar_write: header, data padded to 512, two zero blocks. */
function tar(files: [string, number][]): Uint8Array {
	const blocks: number[] = [];
	for (const [name, size] of files) {
		const h = new Uint8Array(512);
		h.set([...name].map((c) => c.charCodeAt(0)), 0);
		h.set([...size.toString(8).padStart(11, '0')].map((c) => c.charCodeAt(0)), 124);
		h.set([...'ustar'].map((c) => c.charCodeAt(0)), 257);
		blocks.push(...h, ...new Uint8Array(Math.ceil(size / 512) * 512).fill(1));
	}
	return new Uint8Array([...blocks, ...new Uint8Array(1024)]);
}

describe('packLooksCut (download interrupted)', () => {
	const full = tar([['manifest.json', 700], ['manifest.sig', 64]]);

	it('a complete pack is not cut', () => {
		expect(packLooksCut(full)).toBe(false);
	});

	it('a pack cut inside an entry or before the end marker is cut', () => {
		expect(packLooksCut(full.subarray(0, 900))).toBe(true); // inside manifest.json
		expect(packLooksCut(full.subarray(0, 512 * 3))).toBe(true); // manifest.json whole, sig missing
		expect(packLooksCut(full.subarray(0, full.length - 1024))).toBe(true); // no end marker
	});

	it('a file that is not a pack is not called cut', () => {
		expect(packLooksCut(new Uint8Array(2048).fill(7))).toBe(false);
		expect(packLooksCut(new Uint8Array(0))).toBe(false);
		expect(packLooksCut(full.subarray(0, 100))).toBe(false); // not even one header
	});
});
