// Pure helpers for scenario packs and the CSV import wizard (spec scenario-packs "Private CSV
// import"; web-client task 4.5). Validation itself lives in the engine (tl_csv_validate), so web,
// desktop and iPad report identical errors; this file only guesses defaults and formats.

import type { CsvMeta, TrustedKey } from '#lib/engine/types.ts';

/**
 * Development catalog key: signs the sample packs of the static catalog (`static/catalog`, built by
 * scripts/build-sample-catalog.py; its seed is kept outside the repository). It is replaced by the
 * production publisher key at the key ceremony (design D1).
 */
export const DEV_CATALOG_KEY: TrustedKey = {
	id: 'device-e83d839d424e574a',
	key: '99e219ba7f30d414730de4c0ff8972c218117e3a1279fe675de4cc0552acf889'
};

/**
 * Publisher keys pinned in this build (design D1 rotation list). A pack installs only when it is
 * signed by one of these or by this device (its own CSV imports).
 */
export const PINNED_PUBLISHER_KEYS: readonly TrustedKey[] = [DEV_CATALOG_KEY];

export function trustedKeys(device: TrustedKey | null): TrustedKey[] {
	return device ? [...PINNED_PUBLISHER_KEYS, device] : [...PINNED_PUBLISHER_KEYS];
}

/** Same limit as the engine (kCsvMaxBytes): refuse before reading a huge file into memory. */
export const CSV_MAX_BYTES = 4 * 1024 * 1024;
/** Same limit as the engine (PackLimits::max_pack_bytes). */
export const PACK_MAX_BYTES = 16 * 1024 * 1024;

export const INTERVALS: readonly { seconds: number; label: string }[] = [
	{ seconds: 60, label: '1m' },
	{ seconds: 300, label: '5m' },
	{ seconds: 900, label: '15m' },
	{ seconds: 3600, label: '1h' },
	{ seconds: 14400, label: '4h' },
	{ seconds: 86400, label: '1d' }
];

export const DEFAULT_META: CsvMeta = {
	asset: '',
	pair: '',
	venue: '',
	volumeUnit: 'base',
	intervalSeconds: 3600,
	timestampUnit: 's',
	leverageCap: 5
};

const MILLIS_THRESHOLD = 1e11; // same rule as the engine: seconds past year 5000 are milliseconds

/** First two data rows' timestamp cells (integers only), skipping the header. */
function firstTimestamps(csv: string): number[] {
	const out: number[] = [];
	for (const line of csv.split(/\r?\n/).slice(1)) {
		const cell = line.split(',')[0]?.replace(/"/g, '').trim() ?? '';
		if (!/^\d+$/.test(cell)) continue;
		out.push(Number(cell));
		if (out.length === 2) break;
	}
	return out;
}

/**
 * Suggested metadata from the file's first rows: the timestamp unit and the interval. Only a
 * starting point the player confirms; the engine still validates the declared values strictly.
 */
export function guessMeta(csv: string, base: CsvMeta = DEFAULT_META): CsvMeta {
	const ts = firstTimestamps(csv);
	if (ts.length < 2) return { ...base };
	const timestampUnit = ts[0] >= MILLIS_THRESHOLD ? 'ms' : 's';
	const step = (ts[1] - ts[0]) / (timestampUnit === 'ms' ? 1000 : 1);
	const known = INTERVALS.find((i) => i.seconds === step);
	return { ...base, timestampUnit, intervalSeconds: known ? known.seconds : base.intervalSeconds };
}

/** "15m", "1h", ... for a bar interval in seconds (the top bar and pack names). */
export function intervalLabel(seconds: number): string {
	return INTERVALS.find((i) => i.seconds === seconds)?.label ?? `${seconds}s`;
}

/** The player's name for an imported pack (shown only in the menu, never by the engine). */
export function defaultLabel(meta: CsvMeta, fileName: string): string {
	const interval = intervalLabel(meta.intervalSeconds);
	const name = meta.pair || meta.asset || fileName.replace(/\.[^.]+$/, '');
	return `${name} · ${interval}`.slice(0, 60);
}

export function isoUtc(seconds: number): string {
	return new Date(seconds * 1000).toISOString().replace('.000Z', 'Z');
}

/** 32 random bytes for the device signing seed (stored once per device). */
export function randomSeedBytes(): Uint8Array {
	const b = new Uint8Array(32);
	crypto.getRandomValues(b);
	return b;
}
