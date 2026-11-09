// P&L heatmap view model (add-practice-and-advanced-strategies D4, W1-W7): the request the panel sends
// and how a cell is shown. Every cell value is the engine's (tl_run_option_whatif's `heatmap`).

import type { WhatIfHeatmapView, WhatIfLegInput, WhatIfRequest } from '#lib/engine/types.ts';
import { fmtMoney, fmtPrice } from './format.ts';
import type { Locale } from './i18n.svelte.ts';

export const HEATMAP_POINTS = 21;
export const HEATMAP_MAX_ROWS = 9;
/** The engine's caps (design D4): 61 prices x 25 times. */
export const ENGINE_MAX_POINTS = 61;
export const ENGINE_MAX_TIMES = 25;

/** Bars from now to show: 0, then evenly up to `maxBars` (the last expiry), whole bars, at most `rows`. */
export function heatmapTimes(maxBars: number, rows = HEATMAP_MAX_ROWS): number[] {
	const n = Math.max(0, Math.floor(maxBars));
	const k = Math.min(rows, ENGINE_MAX_TIMES, n + 1);
	if (k <= 1) return [0];
	return [...new Set(Array.from({ length: k }, (_, i) => Math.round((i * n) / (k - 1))))];
}

/** The price range: the forward ±15%, on the tick. */
export function heatmapRange(forward: number, tick: number): { lo: string; hi: string } {
	const decimals = Math.max(0, Math.round(-Math.log10(tick)));
	const snap = (v: number) => (Math.round(v / tick) * tick).toFixed(decimals);
	return { lo: snap(forward * 0.85), hi: snap(forward * 1.15) };
}

/** The run what-if request of a heatmap: the legs (or the holdings), the prices and the times. */
export function heatmapRequest(legs: readonly WhatIfLegInput[], forward: number, tick: number, maxBars: number, holdings = false): WhatIfRequest {
	const { lo, hi } = heatmapRange(forward, tick);
	return {
		legs: [...legs],
		...(holdings ? { holdings: true } : {}),
		heatmap: { lo, hi, points: HEATMAP_POINTS, bars: heatmapTimes(maxBars) }
	};
}

/** The largest |P&L| on the grid (the colour scale's end). */
export function heatmapScale(m: Pick<WhatIfHeatmapView, 'pnl'>): number {
	return Math.max(1e-9, ...m.pnl.flat().map((v) => Math.abs(Number(v))));
}

/**
 * A cell's colour (a diverging palette): the gain colour (--up) or the loss colour (--down), mixed into
 * the background by |P&L| / scale. The colorblind setting swaps --up/--down to blue/orange, so the map
 * follows it with no colour of its own.
 */
export function cellStyle(pnl: number, scale: number): string {
	const share = Math.round(Math.min(1, Math.abs(pnl) / scale) * 85) + (pnl === 0 ? 0 : 10);
	const color = pnl >= 0 ? 'var(--up)' : 'var(--down)';
	return `background: color-mix(in srgb, ${color} ${share}%, var(--bg, #081010))`;
}

/** The text a cell is read with: price, bars and P&L in the player's locale. */
export function cellParams(m: WhatIfHeatmapView, t: number, p: number, locale: Locale): { price: string; bars: number; pnl: string } {
	return { price: fmtPrice(m.prices[p], locale), bars: m.times[t], pnl: fmtMoney(m.pnl[t][p], locale, true) };
}
