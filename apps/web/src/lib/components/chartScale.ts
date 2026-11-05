// Price-axis helpers for the chart (pure, unit-tested).
//
// The scale follows the candles. A level (entry, stop, target, liquidation, order) widens it only
// when it is near the candles; a far level would squash them into a thin band, so it is pinned to
// the pane edge instead with an arrow pointing to where it really is.

import { fmtNum, fmtPrice } from '#lib/game/format.ts';
import type { Locale } from '#lib/game/i18n.svelte.ts';

/** Cells of the screen-reader candle table: OHLC as prices and the volume, in the player's locale. */
export function candleCells(b: readonly number[], locale: Locale): string[] {
	return [...b.slice(0, 4).map((p) => fmtPrice(String(p), locale)), fmtNum(b[4], locale, 0)];
}

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
 * least LABEL_H, staying inside [top, bottom]. A tag only moves as far as its neighbours need, so a
 * stack overflowing one edge never drags a distant tag past the other. When even that has no room,
 * the tags share the pane evenly (closer than LABEL_H) rather than leave it.
 */
export function spreadLabels(ys: number[], top: number, bottom: number): number[] {
	const order = ys.map((y, i) => ({ y, i })).sort((p, q) => p.y - q.y);
	const out = new Array<number>(ys.length);
	const lo = top + LABEL_H / 2;
	const hi = bottom - LABEL_H / 2;
	if (order.length > 1 && (order.length - 1) * LABEL_H > hi - lo) {
		const step = (hi - lo) / (order.length - 1);
		order.forEach(({ i }, k) => (out[i] = lo + k * step));
		return out;
	}
	// Down from the top: each tag at its line, below the one above it.
	let prev = -Infinity;
	for (const { y, i } of order) {
		out[i] = Math.max(y, prev + LABEL_H, lo);
		prev = out[i];
	}
	// Up from the bottom: only the tags that ran past it (and the ones they then crowd) slide back.
	let next = hi + LABEL_H;
	for (const { i } of [...order].reverse()) {
		out[i] = Math.min(out[i], next - LABEL_H);
		next = out[i];
	}
	return out;
}

/** Whether an axis grid label at `y` would overlap one of the level tags centred at `tagYs`. */
export function hitsTag(y: number, tagYs: number[]): boolean {
	return tagYs.some((t) => Math.abs(t - y) < LABEL_H);
}

/** A drawn candle: OHLCV and the (fractional) slot index its center is drawn at. */
export type DrawnCandle = { at: number; o: number; h: number; l: number; c: number; v: number };

/**
 * Candles to draw for bars `[o, h, l, c, v]` at `slotW` pixels per bar. Narrower than a pixel,
 * consecutive bars that would share a column are merged (first open, highest high, lowest low,
 * last close, largest volume), so a 5,000-bar chart draws about one candle per pixel column.
 */
export function drawnCandles(bars: readonly (readonly number[])[], slotW: number): DrawnCandle[] {
	const n = slotW >= 1 ? 1 : Math.ceil(1 / slotW);
	const out: DrawnCandle[] = [];
	for (let i = 0; i < bars.length; i += n) {
		const group = bars.slice(i, i + n);
		let h = -Infinity;
		let l = Infinity;
		let v = 0;
		for (const b of group) {
			h = Math.max(h, b[1]);
			l = Math.min(l, b[2]);
			v = Math.max(v, b[4]);
		}
		out.push({ at: i + (group.length - 1) / 2, o: group[0][0], h, l, c: group[group.length - 1][3], v });
	}
	return out;
}
