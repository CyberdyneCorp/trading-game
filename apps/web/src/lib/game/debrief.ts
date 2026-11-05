// Display helpers for the engine debrief (report-only numbers computed by engine/src/debrief.cpp).
// The client formats; it never recomputes attribution, costs or leverage advice.

import type { EngineTrade, GreekAttribution, PackReveal, RegimeSignals, RegimeSpan, SignalTally } from '#lib/engine/types.ts';
import { fmtPct, fmtPrice, num } from './format.ts';
import { t, type Locale } from './i18n.svelte.ts';

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

// The fixed transformation note the engine writes into a CSV-import pack (engine/src/pack_build.cpp).
const CSV_TRANSFORM = /^prices x 100 \/ open of bar (\d+), half-even to 0\.01; volume x the inverse$/;

/** Reveal sentence and transformation note of a pack, in the player's language where the text is known. */
export function revealTexts(r: PackReveal, alias: string): { line: string; transform: string } {
	const params = {
		alias,
		pair: r.pair || '—',
		asset: r.asset || '—',
		venue: r.venue,
		from: r.periodFrom,
		to: r.periodTo,
		source: r.source === 'csv-import' ? t('debrief.sourceCsv') : r.source
	};
	const bar = CSV_TRANSFORM.exec(r.transformation)?.[1];
	return {
		line: t(r.venue ? 'debrief.revealPack' : 'debrief.revealPackNoVenue', params),
		transform: bar ? t('debrief.transformCsv', { bar }) : r.transformation
	};
}

export interface BandPiece extends RegimeSpan {
	/** Left edge and width as a share (0..1) of the decision window. */
	left: number;
	width: number;
}

/** The revealed regimes clipped to the run's decision window (first decision .. last step), for the band under the chart. */
export function regimeBand(spans: readonly RegimeSpan[], from: number, to: number): BandPiece[] {
	const total = to - from + 1;
	if (total <= 0) return [];
	return spans
		.map((s) => ({ ...s, lo: Math.max(s.start, from), hi: Math.min(s.start + s.bars - 1, to) }))
		.filter((s) => s.hi >= s.lo)
		.map(({ lo, hi, ...s }) => ({ ...s, start: lo, bars: hi - lo + 1, left: (lo - from) / total, width: (hi - lo + 1) / total }));
}

export type SignalKey = 'rsiFade' | 'bollingerFade' | 'macdFollow' | 'smaFollow';
export const SIGNAL_KEYS: SignalKey[] = ['rsiFade', 'bollingerFade', 'macdFollow', 'smaFollow'];

/** "3 de 5 (60%)" / "3 of 5 (60%)"; "—" without samples. */
export function fmtTally(tl: SignalTally, locale: Locale): string {
	if (tl.samples === 0) return '—';
	return t('regime.tally', { hits: tl.hits, n: tl.samples, pct: fmtPct(tl.hits / tl.samples, locale, 0) });
}

/** "Tendência de alta" / "Trend up"; ranges and other undirected regimes keep their name. */
export function regimeName(label: RegimeSpan['label'], direction = 0): string {
	const name = t(`regime.${label}`);
	if (direction === 0) return name;
	return `${name} ${t(direction > 0 ? 'regime.up' : 'regime.down')}`;
}

/** Rows of the signals-by-regime table: only spans where at least one signal fired. */
export function signalRows(rows: readonly RegimeSignals[] | undefined): RegimeSignals[] {
	return (rows ?? []).filter((r) => SIGNAL_KEYS.some((k) => r[k].samples > 0));
}
