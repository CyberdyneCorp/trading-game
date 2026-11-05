// Accessible chart (spec terminal-ui "Accessible chart"): the paged table of every closed bar, the
// text summary and the announcement of a keyboard-focused candle. Pure and unit-tested. The input is
// only the bars the engine already released, so nothing here can reach a bar after the current step.

import { fmtNum, fmtPct, fmtPrice } from '#lib/game/format.ts';
import { t, type Locale } from '#lib/game/i18n.svelte.ts';

/** O, H, L, C, V of one closed bar. */
export type Ohlcv = readonly [number, number, number, number, number];

export const TABLE_PAGE = 50;

export interface BarPage {
	/** First and one-past-last bar index of the page. */
	start: number;
	end: number;
	page: number;
	pages: number;
}

/**
 * Rows of page `page` (null: the last page, which ends with the current closed bar, UX01). Pages are
 * cut from the latest bar backwards, so the last page is always full and a new bar slides it by one.
 */
export function barPage(total: number, page: number | null, size = TABLE_PAGE): BarPage {
	const pages = Math.max(1, Math.ceil(total / size));
	const p = page == null ? pages - 1 : Math.min(Math.max(0, page), pages - 1);
	const end = total - (pages - 1 - p) * size;
	return { start: Math.max(0, end - size), end: Math.max(0, end), page: p, pages };
}

function moveText(open: number, close: number, locale: Locale): string {
	if (!(open > 0) || close === open) return t('chart.flat');
	const pct = fmtPct(Math.abs(close - open) / open, locale);
	return close > open ? t('chart.up', { pct }) : t('chart.down', { pct });
}

/** What a screen reader announces when a candle gets keyboard focus. */
export function candleText(i: number, b: Ohlcv, patterns: string[], locale: Locale): string {
	const base = t('chart.candle', {
		i,
		o: fmtPrice(String(b[0]), locale),
		h: fmtPrice(String(b[1]), locale),
		l: fmtPrice(String(b[2]), locale),
		c: fmtPrice(String(b[3]), locale),
		v: fmtNum(b[4], locale, 0),
		move: moveText(b[0], b[3], locale)
	});
	return patterns.length ? `${base} ${t('chart.candlePatterns', { names: patterns.join(', ') })}` : base;
}

const RECENT = 20;

/** One paragraph describing the chart so far: the latest bar, the move since the first, the recent range. */
export function chartSummary(bars: readonly Ohlcv[], locale: Locale): string {
	if (bars.length === 0) return t('chart.noData');
	const last = bars[bars.length - 1];
	const recent = bars.slice(-RECENT);
	const lo = Math.min(...recent.map((b) => b[2]));
	const hi = Math.max(...recent.map((b) => b[1]));
	return t('chart.summary', {
		n: bars.length,
		i: bars.length - 1,
		close: fmtPrice(String(last[3]), locale),
		move: moveText(last[0], last[3], locale),
		total: moveText(bars[0][3], last[3], locale),
		k: recent.length,
		lo: fmtPrice(String(lo), locale),
		hi: fmtPrice(String(hi), locale)
	});
}

/** Bar index after a navigation key on the focused chart, or null for a key the chart does not use. */
export function stepFocus(key: string, at: number | null, total: number, page = 10): number | null {
	if (total === 0) return null;
	const from = at ?? total - 1;
	const moves: Record<string, number> = {
		ArrowLeft: from - 1,
		ArrowRight: from + 1,
		PageUp: from - page,
		PageDown: from + page,
		Home: 0,
		End: total - 1
	};
	if (!(key in moves)) return null;
	return Math.min(Math.max(0, moves[key]), total - 1);
}
