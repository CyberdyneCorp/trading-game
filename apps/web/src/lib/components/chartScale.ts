// Price-axis helpers for the chart (pure, unit-tested).
//
// The scale follows the candles. A level (entry, stop, target, liquidation, order) widens it only
// when it is near the candles; a far level would squash them into a thin band, so it is pinned to
// the pane edge instead with an arrow pointing to where it really is.

export const NEAR_FRACTION = 0.5; // a level within half the candle span of the candles is "near"
export const LABEL_H = 16; // height of a price tag on the axis

export function candleRange(lows: number[], highs: number[]): [number, number] {
	let lo = Infinity;
	let hi = -Infinity;
	for (const l of lows) lo = Math.min(lo, l);
	for (const h of highs) hi = Math.max(hi, h);
	return [lo, hi];
}

/** Scale bounds: candles plus the levels near them, padded by 8%. */
export function priceRange(lo: number, hi: number, levels: number[]): [number, number] {
	const span = Math.max(hi - lo, hi * 0.01);
	const reach = span * NEAR_FRACTION;
	let a = lo;
	let b = hi;
	for (const p of levels) {
		if (p > 0 && p >= lo - reach && p <= hi + reach) {
			a = Math.min(a, p);
			b = Math.max(b, p);
		}
	}
	const pad = (b - a) * 0.08 || 1;
	return [a - pad, b + pad];
}

export type Pin = 'above' | 'below' | null;

/** Whether a level is off the visible scale, and on which side. */
export function pinOf(price: number, lo: number, hi: number): Pin {
	if (price > hi) return 'above';
	if (price < lo) return 'below';
	return null;
}

/**
 * Vertical tag positions that never overlap: tags keep their order and are pushed apart by at
 * least LABEL_H, staying inside [top, bottom] when there is room.
 */
export function spreadLabels(ys: number[], top: number, bottom: number): number[] {
	const order = ys.map((y, i) => ({ y, i })).sort((p, q) => p.y - q.y);
	const out = new Array<number>(ys.length);
	let prev = -Infinity;
	for (const { y, i } of order) {
		const placed = Math.max(y, prev + LABEL_H, top + LABEL_H / 2);
		out[i] = placed;
		prev = placed;
	}
	// If the stack ran past the bottom, slide it back up (keeping the spacing).
	const overflow = Math.max(0, ...order.map(({ i }) => out[i] - (bottom - LABEL_H / 2)));
	if (overflow > 0) {
		let next = Infinity;
		for (const { i } of [...order].reverse()) {
			out[i] = Math.min(out[i] - overflow, next - LABEL_H);
			next = out[i];
		}
	}
	return out;
}

/** Whether an axis grid label at `y` would overlap one of the level tags centred at `tagYs`. */
export function hitsTag(y: number, tagYs: number[]): boolean {
	return tagYs.some((t) => Math.abs(t - y) < LABEL_H);
}
