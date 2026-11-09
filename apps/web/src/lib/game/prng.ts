// Deterministic draws for the practice formats (add-practice-and-advanced-strategies D15, D16, D19):
// FNV-1a 32 of a text seeds mulberry32, whose outputs are unsigned 32-bit integers. Everything is
// integer arithmetic (Math.imul, >>> 0), so the desktop's uint32_t port gives the same numbers; the
// parity fixtures drills.json, daily.json and guess.json pin them.

/** FNV-1a 32 of the UTF-8 bytes of `text`. */
export function fnv1a32(text: string): number {
	let h = 0x811c9dc5;
	for (const byte of new TextEncoder().encode(text)) {
		h ^= byte;
		h = Math.imul(h, 0x01000193) >>> 0;
	}
	return h >>> 0;
}

/** mulberry32: each call returns the next unsigned 32-bit output. */
export function mulberry32(seed: number): () => number {
	let a = seed >>> 0;
	return () => {
		a = (a + 0x6d2b79f5) >>> 0;
		let t = a;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return (t ^ (t >>> 14)) >>> 0;
	};
}

/** An index in [0, n): the next output modulo n (n ≥ 1). */
export const pick = (next: () => number, n: number): number => next() % n;

/** Whole days from 1970-01-01 to a civil date (proleptic Gregorian; H. Hinnant's days_from_civil). */
export function daysFromCivil(y: number, m: number, d: number): number {
	const yy = m <= 2 ? y - 1 : y;
	const era = Math.floor(yy / 400);
	const yoe = yy - era * 400;
	const mp = (m + 9) % 12;
	const doy = Math.floor((153 * mp + 2) / 5) + d - 1;
	const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
	return era * 146097 + doe - 719468;
}

const DATE = /^(\d{4})-(\d{2})-(\d{2})$/;

/** Whole civil days from `from` to `to` (both "YYYY-MM-DD"); NaN for a malformed date. */
export function daysBetween(from: string, to: string): number {
	const a = DATE.exec(from);
	const b = DATE.exec(to);
	if (!a || !b) return NaN;
	return daysFromCivil(+b[1], +b[2], +b[3]) - daysFromCivil(+a[1], +a[2], +a[3]);
}
