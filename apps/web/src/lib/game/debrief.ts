// Display helpers for the engine debrief (report-only numbers computed by engine/src/debrief.cpp).
// The client formats; it never recomputes attribution, costs or leverage advice.

import type { EngineTrade, GreekAttribution } from '#lib/engine/types.ts';
import { fmtPct, fmtPrice, num } from './format.ts';
import type { Locale } from './i18n.svelte';

export type GreekKey = 'delta' | 'gamma' | 'theta' | 'vega' | 'residual';

export interface GreekRow {
	key: GreekKey;
	value: number;
}

const GREEK_KEYS: GreekKey[] = ['delta', 'gamma', 'theta', 'vega', 'residual'];

/** Option P&L by Greek, in display order; empty when no option was traded. */
export function greekRows(g: GreekAttribution | null | undefined): GreekRow[] {
	if (!g) return [];
	return GREEK_KEYS.map((key) => ({ key, value: num(g[key]) }));
}

/** Leverage from basis points (10000 = 1x), locale-aware: "3,5x" / "3.5x". */
export function fmtLeverageBp(bp: number, locale: Locale): string {
	if (!Number.isFinite(bp)) return '—';
	return `${new Intl.NumberFormat(locale, { minimumFractionDigits: 1, maximumFractionDigits: 1 }).format(bp / 10000)}x`;
}

/** Closest the mark came to liquidation, as a share of the price; "—" when never in a position (-1). */
export function fmtLiqDistance(bp: number, locale: Locale): string {
	return bp < 0 ? '—' : fmtPct(bp / 10000, locale, 2);
}

export interface StopAdvice {
	stop: string;
	liq: string;
	/** Highest leverage that keeps the stop in front of liquidation; 0 = not even 1x. */
	lev: number;
	used: number;
}

/** For a perpetual trade whose stop sat at or beyond liquidation: the numbers of the advice. */
export function stopAdvice(tr: EngineTrade, locale: Locale): StopAdvice | null {
	if (tr.series !== 0 || !tr.stopBeyondLiq) return null;
	return { stop: fmtPrice(tr.stop, locale), liq: fmtPrice(tr.liqPrice, locale), lev: tr.safeLeverage ?? 0, used: tr.leverage ?? 0 };
}
